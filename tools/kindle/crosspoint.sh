#!/bin/sh
# Kindle scriptlet: run CrossPoint.
#
# It runs until it is done: either the user picks "Exit CrossPoint" at the end
# of Settings > System, or something kills it. There used to be a time limit
# here, from when the binary had never executed and holding the panel forever
# was the likelier outcome than working. It works, so the limit is gone.
#
# Everything is still captured. The log costs nothing and it is the only
# window into a run once the device is unplugged.
BASE=/mnt/us/crosspoint
LOG=/mnt/us/crosspoint-run.log

{
    echo "=== CrossPoint run: $(date) ==="
    echo "uname:  $(uname -a)"
    echo "free:   $(df -h /mnt/us 2>/dev/null | tail -1)"
    echo "binary: $(ls -l $BASE/crosspoint 2>&1)"
    echo

    echo "--- what is on the card ---"
    for d in /mnt/us/fonts /mnt/us/ebooks /mnt/us/crosspoint; do
        echo "$d: $(ls "$d" 2>/dev/null | wc -l) entries"
    done
    echo

    chmod +x "$BASE/crosspoint" 2>/dev/null

    echo "--- starting ---"
    if [ -x "$BASE/crosspoint" ]; then
        "$BASE/crosspoint" &
    else
        echo "not executable via the vfat mount; going through the loader"
        /lib/ld-linux.so.3 "$BASE/crosspoint" &
    fi
    pid=$!
    echo "pid: $pid"

    wait "$pid"
    status=$?
    echo "exit status: $status"
    # 128+N is death by signal N: 139 is SIGSEGV, 134 SIGABRT, 143 SIGTERM.
    if [ "$status" -gt 128 ]; then
        echo "  (killed by signal $((status - 128)))"
    fi

    # Hand the screen back. CrossPoint leaves the panel white on the way out,
    # but the Kindle's own UI has been running underneath the whole time and
    # does not know its screen was taken, so it will not repaint until
    # something wakes it. Which mechanism exists depends on the firmware, so
    # this asks rather than assumes, and logs what it found either way.
    echo "--- returning to the Kindle UI ---"
    if command -v lipc-set-prop >/dev/null 2>&1; then
        lipc-set-prop com.lab126.appmgrd start app://com.lab126.booklet.home 2>&1
        echo "  appmgrd home: exit $?"
    else
        echo "  lipc-set-prop not present; press the power button to repaint"
    fi
    echo "=== finished: $(date) ==="
} > "$LOG" 2>&1
