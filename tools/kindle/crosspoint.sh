#!/bin/sh
# Kindle scriptlet: run CrossPoint.
#
# This binary's loop() never returns, and tapping a book gives no way to stop
# it: left alone it would hold the panel and the user would have to reboot. So
# it runs for a bounded time and is then asked to stop. main_kindle.cpp catches
# SIGTERM and leaves the loop, so the panel is left in a readable state rather
# than frozen mid-frame.
#
# Everything is captured. A binary that has never executed is far more likely
# to die than to work, and the log is the whole point of the run.
BASE=/mnt/us/crosspoint
LOG=/mnt/us/crosspoint-run.log
RUN_SECONDS=60

{
    echo "=== CrossPoint run: $(date) ==="
    echo "uname:  $(uname -a)"
    echo "free:   $(df -h /mnt/us 2>/dev/null | tail -1)"
    echo "binary: $(ls -l $BASE/crosspoint 2>&1)"
    echo

    # What the reader will look for. Missing assets are the likeliest first
    # failure, and knowing which are absent beats guessing from a crash.
    echo "--- what is on the card ---"
    for d in /mnt/us/fonts /mnt/us/documents /mnt/us/crosspoint; do
        echo "$d: $(ls "$d" 2>/dev/null | wc -l) entries"
    done
    echo

    chmod +x "$BASE/crosspoint" 2>/dev/null

    echo "--- starting, will stop after ${RUN_SECONDS}s ---"
    if [ -x "$BASE/crosspoint" ]; then
        "$BASE/crosspoint" &
    else
        echo "not executable via the vfat mount; going through the loader"
        /lib/ld-linux.so.3 "$BASE/crosspoint" &
    fi
    pid=$!
    echo "pid: $pid"

    # Poll rather than sleep the whole window: if it dies early, say so early
    # instead of waiting out the clock.
    elapsed=0
    while [ "$elapsed" -lt "$RUN_SECONDS" ]; do
        kill -0 "$pid" 2>/dev/null || break
        sleep 2
        elapsed=$((elapsed + 2))
    done

    if kill -0 "$pid" 2>/dev/null; then
        echo "--- still running after ${elapsed}s, asking it to stop ---"
        kill -TERM "$pid" 2>/dev/null
        sleep 3
        kill -KILL "$pid" 2>/dev/null
        echo "--- SURVIVED the window: it did not crash ---"
    else
        echo "--- exited on its own after ~${elapsed}s ---"
    fi

    wait "$pid" 2>/dev/null
    status=$?
    echo "exit status: $status"
    # 128+N is death by signal N: 139 is SIGSEGV, 134 SIGABRT, 143 SIGTERM.
    if [ "$status" -gt 128 ]; then
        echo "  (killed by signal $((status - 128)))"
    fi
    echo "=== finished: $(date) ==="
} > "$LOG" 2>&1
