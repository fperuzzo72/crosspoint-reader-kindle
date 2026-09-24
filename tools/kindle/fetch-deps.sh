#!/bin/sh
# Fetch the third-party libraries the Kindle build needs, at the versions
# platformio.ini pins.
#
#   sh tools/kindle/fetch-deps.sh
#
# PlatformIO resolves these from its registry; a CMake build outside it has to
# fetch them itself. They land in build/kindle/thirdparty/, which is gitignored:
# this is a fetch step, not vendoring into the repo.
#
# Pins are copied from platformio.ini and must be kept in step with it. A
# version drift here would be invisible until something decoded wrongly.
#
# tjpgd is NOT here: the SDK already vendors it at
# freeink-sdk/libs/book/FreeInkBook/third_party/tjpgd, and it only needed to be
# on the include path.
set -eu

OUT=build/kindle/thirdparty
mkdir -p "$OUT"

fetch_repo() {
    name="$1"
    url="$2"
    ref="$3"
    if [ -d "$OUT/$name" ]; then
        echo "--- $name already present"
        return
    fi
    echo "--- cloning $name at $ref"
    git clone -q "$url" "$OUT/$name"
    git -C "$OUT/$name" checkout -q "$ref"
}

# Idempotency is git's call, not ours:
#   --check --reverse succeeds -> already applied, skip
#   --check           succeeds -> apply
#   neither                    -> stop, because the tree is not what we expect
patch_repo() {
    name="$1"
    dir="$OUT/$name"
    for patch in scripts/jpegdec_patches/*.patch; do
        [ -f "$patch" ] || continue
        base=$(basename "$patch")
        if git -C "$dir" apply --check --reverse "$PWD/$patch" 2>/dev/null; then
            echo "--- $name: $base already applied"
        elif git -C "$dir" apply --check "$PWD/$patch" 2>/dev/null; then
            git -C "$dir" apply "$PWD/$patch"
            echo "--- $name: applied $base"
        else
            echo "ERRO: $base nao aplica nem reverte em $dir" >&2
            echo "      o pin mudou, ou a arvore foi editada a mao." >&2
            exit 1
        fi
    done
}

# ricmoo/QRCode @ 0.0.1  -- the QR encoder behind QrUtils
fetch_repo QRCode https://github.com/ricmoo/QRCode.git v0.0.1

# bitbank2/PNGdec @ 1.1.6
fetch_repo PNGdec https://github.com/bitbank2/PNGdec.git 1.1.6

# bitbank2/JPEGDEC, pinned to a commit rather than a tag by platformio.ini
fetch_repo JPEGDEC https://github.com/bitbank2/JPEGDEC.git 86282979224c8a32fd51e091ed5a35b0c699a52b

# The upstream JPEGDEC pin still has the wild-pointer and DC-write bugs in
# JPEGDecodeMCU_P that fire when EIGHT_BIT_GRAYSCALE decodes a 3-component
# progressive JPEG: each Y MCU drags two MCU_SKIP calls behind it for Cb/Cr,
# and MCU_SKIP passes a negative iMCU that the code indexes with anyway.
#
# Under PlatformIO these patches are applied by scripts/patch_jpegdec.py as a
# pre-build step. The Kindle build does not go through PlatformIO, so it has to
# apply them here or it ships the crash.
patch_repo JPEGDEC

echo
echo "--- present ---"
ls "$OUT"
