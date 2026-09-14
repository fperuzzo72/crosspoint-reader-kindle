#!/bin/sh
# Kindle scriptlet: touch and display together. Gestures draw on screen.
BASE=/mnt/us/crosspoint
LAUNCH_LOG=/mnt/us/crosspoint-touchtest-launch.log

{
    echo "--- scriptlet started: $(date)"
    chmod +x "$BASE/touchtest" 2>/dev/null
    if [ -x "$BASE/touchtest" ]; then
        "$BASE/touchtest"
    else
        echo "not executable via the vfat mount; going through the loader"
        /lib/ld-linux.so.3 "$BASE/touchtest"
    fi
    echo "--- touchtest exited: $?"
    echo "--- scriptlet finished: $(date)"
} >> "$LAUNCH_LOG" 2>&1
