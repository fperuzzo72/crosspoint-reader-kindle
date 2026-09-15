#!/bin/sh
# The real launcher. Installed at /mnt/us/crosspoint/run.sh and executed from a
# copy in /tmp by documents/crosspoint.sh, so that replacing it while a session
# is running is safe. See that stub for why that matters.
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

# Keep the previous run. The log is truncated on every launch, so retrying
# after a crash destroyed the only record of it: the fastest reaction to a
# failure was also what erased its evidence. One generation back is enough,
# because the interesting run is almost always the one just before this.
[ -f "$LOG" ] && mv -f "$LOG" "$LOG.prev" 2>/dev/null

{
    echo "=== CrossPoint run: $(date) ==="
    echo "uname:  $(uname -a)"
    echo "free:   $(df -h /mnt/us 2>/dev/null | tail -1)"
    echo "binary: $(ls -l $BASE/crosspoint 2>&1)"
    # Size and mtime are not enough to tell two builds apart: stripped ARM
    # binaries land on section alignment, so an added function can come out to
    # exactly the same byte count, and it has. A checksum ends the "did I run
    # the new one?" question that cost a round of testing.
    echo "cksum:  $( (cksum "$BASE/crosspoint" 2>/dev/null || md5sum "$BASE/crosspoint" 2>/dev/null) | head -1)"
    echo

    # How this boot started. Written at launch, so it describes the boot BEFORE
    # this run: if the device restarted on its own, this is where the reason
    # shows up. The Kindle's own logs live on the root filesystem, which is not
    # reachable from a Mac over USB, so asking here is the only way to see them.
    echo "--- how this boot started ---"
    echo "uptime: $(cat /proc/uptime 2>/dev/null | cut -d' ' -f1)s"
    dmesg 2>/dev/null | grep -iE "watchdog|panic|oops|reset source|reboot|wdog" | tail -12
    echo

    # Which input devices exist and what each one reports. Asked because the
    # reader opens only the node with ABS_MT_POSITION_X/Y, the touchscreen, so
    # a power button press never reaches it. Whether there is a node to open
    # for that is a question with an answer, and this is where the answer is.
    echo "--- input devices ---"
    cat /proc/bus/input/devices 2>/dev/null | grep -E "^(N|H|B: KEY|B: ABS)" | head -30
    echo

    echo "--- what is on the card ---"
    for d in /mnt/us/fonts /mnt/us/ebooks /mnt/us/crosspoint; do
        echo "$d: $(ls "$d" 2>/dev/null | wc -l) entries"
    done
    echo

    chmod +x "$BASE/crosspoint" 2>/dev/null

    # Run from RAM, not from the card, and the reason is not speed.
    #
    # Plugging in USB unmounts /mnt/us on the device and hands the raw block
    # device to the host. A running process still has its code pages mapped
    # from that filesystem: the ones already resident keep working, which is
    # why reading carried on fine, but the first branch into code that had not
    # been paged in yet sends the kernel back to the file, where a newly copied
    # binary now sits at those offsets. It reads another program's bytes and
    # dies. That is what "opened Settings and it crashed" was.
    #
    # Copying to tmpfs first breaks the link: the image being executed is in
    # RAM and no longer cares what happens to the card.
    RUN="$BASE/crosspoint"
    # The margin is deliberately fat. /tmp is RAM that the Kindle's own
    # framework uses, and taking 3 MB of a small tmpfs to save this process a
    # hazard would be trading our problem for the system's. If there is not
    # room to spare, run from the card and say so.
    NEED_KB=$(( $(ls -l "$BASE/crosspoint" | awk '{print $5}') / 1024 + 6144 ))
    FREE_KB=$(df -k /tmp 2>/dev/null | tail -1 | awk '{print $4}')
    if [ -n "$FREE_KB" ] && [ "$FREE_KB" -gt "$NEED_KB" ] && cp "$BASE/crosspoint" /tmp/crosspoint 2>/dev/null; then
        chmod +x /tmp/crosspoint
        RUN=/tmp/crosspoint
        echo "running from tmpfs: need ${NEED_KB}KB, free ${FREE_KB}KB"
    else
        echo "running from the card: /tmp has ${FREE_KB:-?}KB free, needed ${NEED_KB}KB"
        echo "  (replacing the binary while this runs will crash it later)"
    fi

    echo "--- starting ---"
    if [ -x "$RUN" ]; then
        "$RUN" &
    else
        echo "not executable via the vfat mount; going through the loader"
        /lib/ld-linux.so.3 "$RUN" &
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

    # Hand the screen back.
    #
    # The first attempt at this asked appmgrd to start the home booklet, which
    # returned success and repainted nothing: home was ALREADY the foreground
    # app, so there was no state change for the framework to redraw. It never
    # knew its screen had been taken. The device came back only on replug,
    # because entering USB mode IS a state change.
    #
    # So: leave a readable screen first, collect the evidence while the CPU is
    # certainly awake, and only then force a real state change. If the force
    # works the framework paints over the message; if it does not, the message
    # is still standing and says what to do. A blank panel with no way back is
    # the one outcome worth ruling out.
    FBINK=/mnt/us/libkh/bin/fbink
    [ "$RUN" = /tmp/crosspoint ] && rm -f /tmp/crosspoint

    echo "--- leaving a message on the panel ---"
    if [ -x "$FBINK" ]; then
        # One call per line, and no empty strings. fbink refuses to print an
        # empty string and ABORTS THE WHOLE CALL with 255 when it meets one, so
        # the blank line between these used to take the two lines after it down
        # with it: the panel said "CrossPoint closed." and nothing about how to
        # get back. A separate call per row also means one bad row cannot
        # silence the others.
        row=18
        for line in \
            "CrossPoint closed." \
            " " \
            "Press the power button to return" \
            "to the Kindle."
        do
            "$FBINK" -q -m -y "$row" "$line" 2>&1
            echo "  fbink row $row: exit $?"
            row=$((row + 1))
        done
    else
        echo "  fbink not found at $FBINK"
    fi

    # Evidence for next time, cheap and read-only. It runs BEFORE the power
    # press because that press suspends the CPU, and a log half-written across
    # a suspend is the kind of thing that costs a run to notice.
    echo
    echo "--- what this firmware exposes ---"
    echo "lipc services:"
    lipc-probe -a 2>&1 | head -40
    echo "powerd properties:"
    lipc-probe com.lab126.powerd 2>&1 | head -30
    echo "upstart jobs:"
    initctl list 2>&1 | grep -iE "gui|framework|pillow|powerd" | head -10
    echo "/etc/upstart:"
    ls /etc/upstart 2>&1 | head -20

    echo
    echo "=== finished: $(date) ==="

    # Last, because it suspends the device. One press, not two: the first one
    # sleeps the CPU and this script with it, so a second press would not fire
    # until the user had already woken the device by hand, and would then put
    # it straight back to sleep. Going down draws the screensaver, which is the
    # full repaint that was missing; coming back up is the user's own press and
    # lands on the Kindle home screen.
    echo "--- simulating a power button press ---"
    if command -v lipc-set-prop >/dev/null 2>&1; then
        lipc-set-prop com.lab126.powerd powerButton 1 2>&1
        echo "  powerButton: exit $?"
    else
        echo "  lipc-set-prop not present; the message on screen is the fallback"
    fi
} > "$LOG" 2>&1
