#!/bin/sh
# Is OTA actually disabled on this device?
#
# WinterBreak2's patch_system.sh renames /usr/bin/otaupd and /usr/bin/otav3 to
# .bck, which is the permanent block. But that runs inside the jailbreak's
# compressed payload, so the only way to know it took effect on THIS device is
# to look. The answer decides whether the storage filler is still needed.
LOG=/mnt/us/crosspoint-ota-check.log

{
    echo "=== OTA check: $(date) ==="
    echo
    for b in otaupd otav3; do
        if [ -f "/usr/bin/$b" ]; then
            echo "  /usr/bin/$b        PRESENT  <- still able to update"
        else
            echo "  /usr/bin/$b        absent"
        fi
        if [ -f "/usr/bin/$b.bck" ]; then
            echo "  /usr/bin/$b.bck    present  <- renamed by the jailbreak"
        else
            echo "  /usr/bin/$b.bck    absent"
        fi
    done
    echo
    echo "--- update services ---"
    for s in ota-update otaupd otav3; do
        echo "  $s: $(status $s 2>&1 | head -1)"
    done
    echo
    echo "--- pending update files on the card ---"
    ls -la /mnt/us/*.bin /mnt/us/*.partial 2>/dev/null || echo "  none"
    echo
    if [ ! -f /usr/bin/otaupd ] && [ ! -f /usr/bin/otav3 ]; then
        echo "VERDICT: both binaries are renamed. OTA is blocked permanently,"
        echo "         and the storage filler is no longer what protects you."
    else
        echo "VERDICT: at least one update binary is STILL IN PLACE. Keep the"
        echo "         storage filler; free space is what is protecting you."
    fi
    echo "=== done ==="
} > "$LOG" 2>&1
