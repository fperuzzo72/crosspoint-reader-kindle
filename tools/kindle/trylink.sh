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
# Parallel and incremental. The first version compiled all 224 sources serially
# on every run, which cost minutes while the link itself is instant: the whole
# wait was one core doing what four could, repeating work that had not changed.
# Now an object is rebuilt only when its source is newer, so a re-run after
# touching one file takes seconds.
#
set -u

CROSS=arm-kindlepw2-linux-gnueabi-g++
CROSS_CC=arm-kindlepw2-linux-gnueabi-gcc
OUT=build/kindle/link
JOBS=$(nproc 2>/dev/null || echo 4)
mkdir -p "$OUT"

INC="-Ibuild/kindle/FBInk/libunibreak/src -Ibuild/kindle/thirdparty -Ibuild/kindle/FBInk"
INC="$INC -Ilib/hal/kindle/arduino-shim -include build/kindle/census-defines.h"
for d in $(find freeink-sdk/libs -type d -name include); do INC="$INC -I$d"; done
for d in lib/*/; do INC="$INC -I${d%/}"; done
INC="$INC -Ilib/miniz/src -Ilib/uzlib/src -Ilib/hal/kindle -Ilib/hal -Isrc -Ilib"
INC="$INC -Isrc/components -Isrc/activities -Isrc/util -Isrc/network"
DEF="-DFREEINK_DEVICE_KINDLE=1"
# expat is configured by defines rather than a config header, and xmlparse.c
# refuses to build without them.
CDEFS="-DXML_GE=0 -DXML_CONTEXT_BYTES=1024"

# Header changes are invisible to a source-vs-object timestamp check, and this
# port edits shim headers constantly: without this, a run after touching
# Arduino.h would reuse every stale object and report a number that was true
# ten edits ago. Rather than track real dependencies, the newest header under
# the shim wins: if it is newer than an object, that object is rebuilt.
NEWEST_HEADER=$(find lib/hal/kindle -name '*.h' -newer "$OUT/.stamp" 2>/dev/null | head -1)
if [ ! -f "$OUT/.stamp" ] || [ -n "$NEWEST_HEADER" ]; then
    echo "--- shim headers changed, discarding objects ---"
    rm -f "$OUT"/*.o
fi
touch "$OUT/.stamp"

# A real helper script rather than an exported shell function: the container's
# /bin/sh is dash, `export -f` is a bashism, and using one made every parallel
# worker fail silently.
cat > "$OUT/cc-one.sh" <<HELPER
#!/bin/sh
f="\$1"
o="$OUT/\$(echo "\$f" | tr '/' '_' | sed 's/\.[cp]*\$/.o/')"
# Skip when the object is already newer than its source.
if [ -f "\$o" ] && [ "\$o" -nt "\$f" ]; then exit 0; fi
case "\$f" in
  *.c) $CROSS_CC -Os -ffunction-sections -fdata-sections -c $INC $CDEFS "\$f" -o "\$o" 2>/dev/null ;;
  *)   $CROSS -std=c++20 -Os -ffunction-sections -fdata-sections -c $INC $DEF "\$f" -o "\$o" 2>/dev/null ;;
esac
# A file that does not compile leaves no object, and the link then reports its
# symbols as undefined. That is the measurement, not a failure to handle.
exit 0
HELPER
chmod +x "$OUT/cc-one.sh"

# FreeInkDisplay/src is the panel driver stack: PanelDriver implementations and
# the EpdBus they talk through. HalDisplay bypasses all of it on this target,
# so compiling it only yields objects referencing a bus that cannot exist here.
# tools/ carries main_kindle.cpp, which supplies the main() the Arduino core
# used to. Leaving the directory out is why `main` itself came back undefined.
{ find src lib freeink-sdk/libs -name '*.cpp' 2>/dev/null; echo tools/kindle/main_kindle.cpp; } \
  | grep -vE '/test/|/tools/|FBInk' \
  | grep -vE 'FreeInkDisplay/src/' > "$OUT/sources.txt"
# Every C source under lib/, not just the three obvious third-party trees:
# lib/MiniBidi/minibidi.c is one, and leaving it out made bidi_class look like
# a missing symbol rather than a file nobody had compiled.
# libunibreak ships with FBInk and provides set_linebreaks_utf8, which
# FreeInkBook's chapter layout calls. Its headers were already on the include
# path; its sources were not being built.
# C sources live in three places, and missing any of them makes a whole
# library look like undefined symbols rather than an uncompiled file:
#   lib/                 expat, miniz, uzlib, minibidi
#   freeink-sdk/libs/    the SDK vendors ITS OWN miniz and libunibreak, with
#                        different symbol prefixes from CrossPoint's copies
#   FBInk/libunibreak    only as a fallback; the SDK's copy wins if present
{ find lib -name '*.c' 2>/dev/null
  find freeink-sdk/libs -name '*.c' 2>/dev/null
} > "$OUT/csources.txt"

echo "--- compiling ($(wc -l < "$OUT/sources.txt" | tr -d ' ') C++, $(wc -l < "$OUT/csources.txt" | tr -d ' ') C, -j$JOBS, incremental) ---"
cat "$OUT/sources.txt" "$OUT/csources.txt" | xargs -P "$JOBS" -n1 "$OUT/cc-one.sh"

objs=$(ls "$OUT"/*.o 2>/dev/null | tr '\n' ' ')
echo "objects: $(echo $objs | wc -w | tr -d ' ')"

echo "--- attempting a link ---"
# --gc-sections is what the firmware build uses and it is not just about size:
# uzlib's checksum helpers are declared and called but never defined in the
# vendored subset, and nothing calls the function that calls them. Without
# section GC those show up as undefined references to code that is never
# reached.
$CROSS -o "$OUT/crosspoint" $objs \
    -Wl,--gc-sections \
    build/kindle/FBInk/Release/libfbink.a -lrt -lpthread 2>"$OUT/link.err"
echo "exit: $?"
echo
echo "--- undefined symbols, by frequency ---"
grep -oE "undefined reference to .[^']*'" "$OUT/link.err" \
  | sed "s/undefined reference to //" | sort | uniq -c | sort -rn | head -20
echo
echo "distinct undefined references: $(grep -oE "undefined reference to .[^']*'" "$OUT/link.err" | sort -u | wc -l | tr -d ' ')"
