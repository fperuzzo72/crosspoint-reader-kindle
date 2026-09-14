#!/bin/sh
# Does the Kindle build LINK?
#
# tools/kindle/census.sh answers "would this file compile", which is a weaker
# question than it looks: a tree can be 100% syntactically valid and still have
# nothing to link, because a file that fails to compile takes its symbols with
# it and every caller shows up as an undefined reference.
#
# So this compiles everything that can be compiled, links it, and groups the
# undefined references by frequency. The count is the honest distance to a
# running binary, and it falls as whole files start compiling rather than one
# symbol at a time: 255 -> 197 -> 152 -> 122 -> 92 so far, each drop a file or
# a subtree rather than a symbol.
#
#   docker run --rm -v "$PWD:/src" -w /src crosspoint-kindle-tc:latest \
#       sh tools/kindle/trylink.sh
#
set -u
CROSS=arm-kindlepw2-linux-gnueabi-g++
OUT=build/kindle/link
mkdir -p "$OUT"

INC="-Ibuild/kindle/FBInk/libunibreak/src -Ibuild/kindle/thirdparty -Ibuild/kindle/FBInk"
INC="$INC -Ilib/hal/kindle/arduino-shim -include build/kindle/census-defines.h"
for d in $(find freeink-sdk/libs -type d -name include); do INC="$INC -I$d"; done
for d in lib/*/; do INC="$INC -I${d%/}"; done
INC="$INC -Ilib/miniz/src -Ilib/uzlib/src -Ilib/hal/kindle -Ilib/hal -Isrc -Ilib"
INC="$INC -Isrc/components -Isrc/activities -Isrc/util -Isrc/network"

echo "--- compiling every source that passes ---"
objs=""
n=0
# FreeInkDisplay/src is the panel driver stack: PanelDriver implementations and
# the EpdBus they talk through. On this target HalDisplay bypasses all of it, so
# compiling it only produces objects that reference an EpdBus which cannot
# exist here. That is where every remaining EpdBus undefined came from.
#
# expat, miniz and uzlib ARE needed at link time; excluding them from the
# object list is why XML_SetElementHandler came back undefined. They are C
# sources compiled separately below.
for f in $(find src lib freeink-sdk/libs -name '*.cpp' 2>/dev/null \
           | grep -vE '/test/|/tools/|FBInk' \
           | grep -vE 'freeink-sdk/libs/display/FreeInkDisplay/src/'); do
    o="$OUT/$(echo "$f" | tr '/' '_' | sed 's/\.cpp$/.o/')"
    if $CROSS -std=c++20 -Os -c $INC -DFREEINK_DEVICE_KINDLE=1 "$f" -o "$o" 2>/dev/null; then
        objs="$objs $o"
        n=$((n + 1))
    fi
done
echo "objetos produzidos: $n"

echo "--- compiling the C third-party trees ---"
# expat is configured by defines, not by a config header: platformio.ini passes
# these and without them xmlparse.c refuses to build at all.
CDEFS="-DXML_GE=0 -DXML_CONTEXT_BYTES=1024"
for f in $(find lib/expat lib/miniz lib/uzlib -name '*.c' 2>/dev/null); do
    o="$OUT/$(echo "$f" | tr '/' '_' | sed 's/\.c$/.o/')"
    if arm-kindlepw2-linux-gnueabi-gcc -Os -c $INC $CDEFS "$f" -o "$o" 2>/dev/null; then
        objs="$objs $o"
    fi
done

echo "--- attempting a link ---"
$CROSS -o "$OUT/crosspoint" $objs \
    build/kindle/FBInk/Release/libfbink.a -lrt -lpthread 2>"$OUT/link.err"
status=$?
echo "exit: $status"
echo
echo "--- undefined symbols, by frequency ---"
grep -oE "undefined reference to \`[^']*'" "$OUT/link.err" \
  | sed "s/undefined reference to //" | sort | uniq -c | sort -rn | head -25
echo
echo "total de referencias indefinidas distintas: $(grep -oE "undefined reference to \`[^']*'" "$OUT/link.err" | sort -u | wc -l | tr -d ' ')"
