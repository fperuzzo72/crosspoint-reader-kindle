#!/bin/sh
# Kindle scriptlet: copy this to documents/ on the device and it appears as a
# book on the home screen. Opening the book runs it.
#
# KUAL is dead; this is how homebrew is launched now. The jailbreak's cleanup
# step remounts /mnt/us with exec, which is what lets a binary on the user
# partition run at all.
#
# Two separate logs on purpose. The binary owns crosspoint-smoketest.log and
# truncates it on every run, so the launch trace lives elsewhere: if the binary
# never starts, or dies before opening its log, this file is the only evidence
# left behind, and it must not be the one that got clobbered.
BASE=/mnt/us/crosspoint
LAUNCH_LOG=/mnt/us/crosspoint-smoketest-launch.log

{
    echo "--- scriptlet started: $(date)"
    echo "uname: $(uname -a)"

    if [ ! -f "$BASE/smoketest" ]; then
        echo "FATAL: $BASE/smoketest is missing."
        exit 1
    fi
    echo "mount: $(mount | grep ' /mnt/us ')"

    # /mnt/us is vfat, which stores no permission bits: whether a file looks
    # executable is decided by the mount's fmask, not by the file. chmod may
    # therefore be a no-op. If the direct exec is refused, invoking the dynamic
    # loader explicitly runs the binary anyway, since the kernel is then asked
    # to exec ld-linux (which does live on a real filesystem) rather than the
    # file on the card.
    chmod +x "$BASE/smoketest" 2>/dev/null

    if [ -x "$BASE/smoketest" ]; then
        "$BASE/smoketest"
        status=$?
    else
        echo "not executable via the vfat mount; going through the loader"
        /lib/ld-linux.so.3 "$BASE/smoketest"
        status=$?
    fi
    echo "--- smoketest exited: $status"
    if [ "$status" -ne 0 ]; then
        echo "    see /mnt/us/crosspoint-smoketest.log for why"
    fi
    echo "--- scriptlet finished: $(date)"
} >> "$LAUNCH_LOG" 2>&1
