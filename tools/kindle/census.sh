#!/bin/sh
# How far along is the Kindle port?
#
# Runs the cross-compiler over every source in src/, lib/ and the SDK, counts
# what compiles, and groups the failures by cause. This is the port's compass:
# attack whatever cause appears most often, re-measure, repeat. It is what
# identified Print.h and SdFat as the first two shims worth writing, and those
# two alone took the tree from 25% to 68%.
#
#   docker run --rm -v "$PWD:/src" -w /src crosspoint-kindle-tc:latest \
#       sh tools/kindle/census.sh
#
# Syntax-only: this answers "would this file compile", not "does it link" or
# "does it work". A file counted here can still be wrong at runtime.
set -u

CROSS=arm-kindlepw2-linux-gnueabi-g++
OUT=build/kindle

INC="-I$OUT/FBInk/libunibreak/src -I$OUT/thirdparty -I$OUT/FBInk"
for d in "$OUT"/thirdparty/*/src; do [ -d "$d" ] && INC="$INC -I$d"; done
INC="$INC -Ifreeink-sdk/libs/book/FreeInkBook/third_party/tjpgd"
INC="$INC -Ilib/hal/kindle/arduino-shim"
for d in $(find freeink-sdk/libs -type d -name include); do INC="$INC -I$d"; done
for d in lib/*/; do INC="$INC -I${d%/}"; done
INC="$INC -Ilib/miniz/src -Ilib/uzlib/src -Ilib/hal/kindle -Ilib/hal -Isrc -Ilib"
INC="$INC -Isrc/components -Isrc/activities -Isrc/util -Isrc/network"

mkdir -p "$OUT"

# CROSSPOINT_VERSION is a string macro, and passing quotes through a shell
# variable that is later word-split does not survive: the backslashes arrive
# at the compiler literally. A forced include sidesteps the quoting entirely.
cat > "$OUT/census-defines.h" <<'DEFS'
#pragma once
// ArduinoJson probes for the real Arduino core to decide whether to support
// ::String. The shim is not that core, so the support is requested here.
#ifndef ARDUINOJSON_ENABLE_ARDUINO_STRING
#define ARDUINOJSON_ENABLE_ARDUINO_STRING 1
#endif
// This panel is 600x800 at 167ppi, smaller than the boards CrossPoint was
// built for, and the 16 and 18 point reader fonts are larger than anything
// that fits usefully on it. Omitting them also takes their glyph data out of
// the binary, which is most of its size.
#ifndef CROSSPOINT_OMIT_LARGE_READER_FONTS
#define CROSSPOINT_OMIT_LARGE_READER_FONTS 1
#endif
#ifndef CROSSPOINT_VERSION
#define CROSSPOINT_VERSION "kindle-dev"
#endif
DEFS
INC="$INC -include $OUT/census-defines.h"
: > "$OUT/census-errors.txt"
ok=0
fail=0

for f in $(find src lib freeink-sdk/libs -name '*.cpp' 2>/dev/null \
           | grep -vE 'expat|miniz|uzlib|/test/|/tools/|FBInk'); do
    if $CROSS -std=c++20 -fsyntax-only $INC -DFREEINK_DEVICE_KINDLE=1 "$f" >"$OUT/one.err" 2>&1; then
        ok=$((ok + 1))
    else
        fail=$((fail + 1))
        grep -m1 -E "fatal error|error:" "$OUT/one.err" | sed "s|.*error: ||" >> "$OUT/census-errors.txt"
    fi
done

total=$((ok + fail))
echo "CENSUS: $ok of $total compile ($((ok * 100 / total))%)"
echo
echo "--- failures by cause ---"
sed -E "s/([a-zA-Z0-9_\/.]+\.h): No such file.*/missing header: \1/" "$OUT/census-errors.txt" \
    | cut -c1-72 | sort | uniq -c | sort -rn | head -20
