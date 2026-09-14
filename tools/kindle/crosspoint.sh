#!/bin/sh
# Kindle scriptlet: start CrossPoint.
#
# Deliberately tiny, and deliberately the file that never changes.
#
# A shell reads a script as it executes it, a few hundred bytes at a time, and
# keeps a byte offset into the open file rather than a copy of its contents. So
# replacing a running script leaves the shell reading its old offset inside new
# content and executing whatever fragments it lands on. That filled the screen
# with "not found" errors and killed the session, and it is the same hazard
# that replacing the binary mid-session caused, in the piece that was missed
# when that one was fixed.
#
# The cure is the same: get the thing being executed off the card. The launcher
# proper lives in crosspoint/run.sh and is run from a copy in RAM, so it can be
# updated at any time. This stub is read from the card, so it stays small
# enough to be read in a single gulp and, with luck, never needs editing again.
SRC=/mnt/us/crosspoint/run.sh
DST=/tmp/crosspoint-run.sh

if cp "$SRC" "$DST" 2>/dev/null; then
    exec /bin/sh "$DST"
fi

echo "=== CrossPoint could not start: $SRC is missing ===" > /mnt/us/crosspoint-run.log
