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
    if [ ! -x "$BASE/smoketest" ]; then
        echo "FATAL: $BASE/smoketest is not executable."
        echo "       Either chmod +x it, or /mnt/us lost its exec mount."
        echo "       mount says: $(mount | grep ' /mnt/us ')"
        exit 1
    fi

    "$BASE/smoketest"
    status=$?
    echo "--- smoketest exited: $status"
    if [ "$status" -ne 0 ]; then
        echo "    see /mnt/us/crosspoint-smoketest.log for why"
    fi
    echo "--- scriptlet finished: $(date)"
} >> "$LAUNCH_LOG" 2>&1
