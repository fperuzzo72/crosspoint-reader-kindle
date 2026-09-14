#!/bin/sh
# Kindle scriptlet: dumps the evdev input devices and captures 25 seconds of
# real touches, so the touch backend can be written against what this panel
# actually sends instead of against an assumption.
BASE=/mnt/us/crosspoint
LAUNCH_LOG=/mnt/us/crosspoint-inputprobe-launch.log

{
    echo "--- scriptlet started: $(date)"
    echo "whoami: $(id 2>/dev/null)"
    echo "/dev/input: $(ls /dev/input 2>&1 | tr '\n' ' ')"

    chmod +x "$BASE/inputprobe" 2>/dev/null
    if [ -x "$BASE/inputprobe" ]; then
        "$BASE/inputprobe"
    else
        echo "not executable via the vfat mount; going through the loader"
        /lib/ld-linux.so.3 "$BASE/inputprobe"
    fi
    echo "--- inputprobe exited: $?"
    echo "--- scriptlet finished: $(date)"
} >> "$LAUNCH_LOG" 2>&1
