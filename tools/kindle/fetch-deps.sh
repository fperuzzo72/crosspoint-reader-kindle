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

# ricmoo/QRCode @ 0.0.1  -- the QR encoder behind QrUtils
fetch_repo QRCode https://github.com/ricmoo/QRCode.git v0.0.1

# bitbank2/PNGdec @ 1.1.6
fetch_repo PNGdec https://github.com/bitbank2/PNGdec.git 1.1.6

# bitbank2/JPEGDEC, pinned to a commit rather than a tag by platformio.ini
fetch_repo JPEGDEC https://github.com/bitbank2/JPEGDEC.git 86282979224c8a32fd51e091ed5a35b0c699a52b

echo
echo "--- present ---"
ls "$OUT"
