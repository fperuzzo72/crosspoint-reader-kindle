#!/bin/sh
# Cross-compile the Kindle on-device tools. Runs INSIDE the toolchain
# container (docker/toolchain.Dockerfile), which already has
# arm-kindlepw2-linux-gnueabi on PATH.
#
#   docker run --rm -v "$PWD:/src" crosspoint-kindle-tc:latest \
#       sh tools/kindle/build.sh
#
# Output: soft-float ARM binaries for the KT3 in build/kindle/:
#   smoketest   display backend first light, verifies pixels land
#   touchtest   touch and display together: gestures draw on screen
#   inputprobe  dumps the evdev devices and a capture of real touches
set -eu

CROSS_TC=arm-kindlepw2-linux-gnueabi
OUT=build/kindle
FBINK_REV=v1.25.0

mkdir -p "$OUT"

# FBInk is built from source rather than vendored: it is a moving target that
# tracks Amazon's firmware quirks, and pinning a rev here keeps the binary
# reproducible without carrying its tree in this repo.
if [ ! -d "$OUT/FBInk" ]; then
    echo "--- cloning FBInk $FBINK_REV"
    git clone --depth 1 --branch "$FBINK_REV" --recursive \
        https://github.com/NiLuJe/FBInk.git "$OUT/FBInk"
fi

echo "--- building libfbink (static, kindle)"
# MINIMAL drops the font/image/OpenType machinery: this port composes its own
# frames and only needs fbink for the ioctls and the per-model quirk table.
CROSS_TC="$CROSS_TC" make -C "$OUT/FBInk" \
    KINDLE=1 MINIMAL=1 DRAW=1 staticlib -j"$(nproc)"

echo "--- building smoketest"
# -lrt is not optional here: this toolchain targets a glibc old enough
# (2.4-era, matching the device) that clock_gettime still lives in librt
# rather than having been folded into libc, which happened in 2.17.
"$CROSS_TC-g++" \
    -std=c++20 -Os -Wall -Wextra \
    -I "$OUT/FBInk" \
    -o "$OUT/smoketest" \
    tools/kindle/smoketest.cpp \
    lib/hal/kindle/KindleGrayExpand.cpp \
    lib/hal/kindle/KindleFrameBuffer.cpp \
    "$OUT/FBInk/Release/libfbink.a" \
    -lrt

echo "--- building touchtest"
"$CROSS_TC-g++" \
    -std=c++20 -Os -Wall -Wextra \
    -I "$OUT/FBInk" \
    -o "$OUT/touchtest" \
    tools/kindle/touchtest.cpp \
    lib/hal/kindle/KindleGrayExpand.cpp \
    lib/hal/kindle/KindleFrameBuffer.cpp \
    lib/hal/kindle/KindleTouchClassifier.cpp \
    lib/hal/kindle/KindleTouchDevice.cpp \
    "$OUT/FBInk/Release/libfbink.a" \
    -lrt

echo "--- building inputprobe"
# No FBInk here: this one only talks to evdev.
"$CROSS_TC-g++" \
    -std=c++20 -Os -Wall -Wextra \
    -o "$OUT/inputprobe" \
    tools/kindle/inputprobe.cpp

for b in smoketest touchtest inputprobe; do
    "$CROSS_TC-strip" "$OUT/$b"
done

echo "--- done"
for b in smoketest touchtest inputprobe; do
    file "$OUT/$b"
done
