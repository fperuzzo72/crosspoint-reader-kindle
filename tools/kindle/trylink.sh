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
CROSS_AR=arm-kindlepw2-linux-gnueabi-gcc-ar
OUT=build/kindle/link
JOBS=$(nproc 2>/dev/null || echo 4)
mkdir -p "$OUT"

INC="-Ibuild/kindle/FBInk/libunibreak/src -Ibuild/kindle/thirdparty -Ibuild/kindle/FBInk"
# Fetched libraries keep their upstream layout: headers live under src/.
# tools/kindle/fetch-deps.sh puts them here at the versions platformio.ini pins.
for d in build/kindle/thirdparty/*/src; do [ -d "$d" ] && INC="$INC -I$d"; done
# tjpgd is already vendored in the SDK and only needed an include path.
INC="$INC -Ifreeink-sdk/libs/book/FreeInkBook/third_party/tjpgd"
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
# ANY header, not just the shim's.
#
# The rule below rebuilds an object when its own source is newer, which says
# nothing about the headers that source includes. That is not a theoretical
# gap: I18nKeys.h is generated, and adding one string moved StrId::_COUNT from
# 462 to 463 while I18n.cpp's object stayed behind holding "cmp #462". The new
# string was in the binary and the code that looked it up rejected it as out of
# range, so the menu showed "???" and everything about the build looked fine.
#
# A stale object carrying a stale constant is the worst shape a build error
# takes, because nothing fails. So the whole tree's headers are watched and any
# change discards every object. That costs a full rebuild, which is about a
# minute, and buys never having to wonder.
NEWEST_HEADER=$(find lib src freeink-sdk build/kindle/census-defines.h \
    \( -name '*.h' -o -name '*.hpp' \) -newer "$OUT/.stamp" 2>/dev/null | head -1)
if [ ! -f "$OUT/.stamp" ] || [ -n "$NEWEST_HEADER" ]; then
    echo "--- a header changed (${NEWEST_HEADER:-first run}); discarding objects ---"
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
{ find src lib freeink-sdk/libs -name '*.cpp' 2>/dev/null
  find build/kindle/thirdparty/*/src -name '*.cpp' 2>/dev/null
  echo tools/kindle/main_kindle.cpp
} \
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
# CrossPoint and the SDK each vendor expat, both exporting unprefixed XML_*
# symbols, so only one can be linked. The SDK's wins because it carries a real
# expat_config.h rather than relying on build flags, and because its include
# directory already comes first: linking CrossPoint's copy while compiling
# against the SDK's header would mix two configurations of one library.
{ find lib -name '*.c' -not -path 'lib/expat/*' 2>/dev/null
  find freeink-sdk/libs -name '*.c' 2>/dev/null
  # Fetched libraries: only their src/, never examples, tests or the desktop
  # ports upstream ships alongside (those carry their own main()).
  find build/kindle/thirdparty/*/src -name '*.c' 2>/dev/null
} > "$OUT/csources.raw"

# Some vendored C sources are not standalone: a wrapper (*_impl.c) applies a
# symbol-prefix config and then #includes the raw file. There are two miniz
# copies in this tree, each with its own prefix, and compiling the raw sources
# as well gives every one of their symbols two definitions.
#
# Rather than hardcode names, read the wrappers and exclude exactly what they
# include. That stays correct if the vendoring changes.
: > "$OUT/wrapped.txt"
REPO_ROOT_ABS=$(pwd -P)
# Any C file that #includes another C file is a wrapper, whatever it is
# named: the miniz ones are *_impl.c but expat's are expat_xmlparse.c, and
# matching on the name missed them entirely.
for w in $(grep -rl '#include.*\.c"' lib freeink-sdk/libs --include='*.c' 2>/dev/null); do
    dir=$(dirname "$w")
    grep -oE '#include "[^"]*\.c"' "$w" | sed 's/#include "//;s/"//' | while read -r inc; do
        # Normalise against the wrapper's directory, then make it relative
        # again: the source list is relative, and an absolute path here would
        # match nothing.
        (cd "$dir" && readlink -f "$inc" 2>/dev/null | sed "s|^$REPO_ROOT_ABS/||") || true
    done >> "$OUT/wrapped.txt"
done

if [ -s "$OUT/wrapped.txt" ]; then
    grep -vFf "$OUT/wrapped.txt" "$OUT/csources.raw" > "$OUT/csources.txt" || cp "$OUT/csources.raw" "$OUT/csources.txt"
    echo "--- excluding $(wc -l < "$OUT/wrapped.txt" | tr -d ' ') C sources that a wrapper already includes ---"
else
    cp "$OUT/csources.raw" "$OUT/csources.txt"
fi

echo "--- compiling ($(wc -l < "$OUT/sources.txt" | tr -d ' ') C++, $(wc -l < "$OUT/csources.txt" | tr -d ' ') C, -j$JOBS, incremental) ---"
cat "$OUT/sources.txt" "$OUT/csources.txt" | xargs -P "$JOBS" -n1 "$OUT/cc-one.sh"

echo "objects: $(ls "$OUT"/*.o 2>/dev/null | wc -l | tr -d ' ')"

# Group objects into per-library archives rather than linking them loose.
#
# This is how the real build works and it is not cosmetic. Three copies of
# miniz live in this tree (CrossPoint's, FreeInkBook's, ContentProtection's),
# each with a config that prefixes only some of its symbols, so the rest
# collide. PlatformIO never sees that because it archives each library and the
# linker then pulls a member only when it resolves something still undefined:
# duplicates across archives are "first wins", not an error.
#
# Linking loose objects forces every definition in and turns a working layout
# into 14 multiple-definition errors.
rm -f "$OUT"/*.a
for o in "$OUT"/*.o; do
    base=$(basename "$o")
    case "$base" in
        src_*|tools_*)   lib=app ;;
        lib_hal_*)       lib=hal ;;
        lib_*)           lib=$(echo "$base" | cut -d_ -f1-2) ;;
        freeink-sdk_*)   lib=$(echo "$base" | cut -d_ -f1-4) ;;
        *)               lib=thirdparty ;;
    esac
    "$CROSS_AR" rcs "$OUT/lib$lib.a" "$o" 2>/dev/null
done

# The app and the HAL go in as loose objects: they define main() and the
# globals nothing references by name, which an archive would drop.
objs=$(ls "$OUT"/src_*.o "$OUT"/tools_*.o "$OUT"/lib_hal_*.o 2>/dev/null | tr '\n' ' ')
archives=$(ls "$OUT"/*.a 2>/dev/null | grep -vE 'libapp\.a|libhal\.a' | tr '\n' ' ')
echo "archives: $(echo $archives | wc -w | tr -d ' ')"

echo "--- attempting a link ---"
# --gc-sections is what the firmware build uses and it is not just about size:
# uzlib's checksum helpers are declared and called but never defined in the
# vendored subset, and nothing calls the function that calls them. Without
# section GC those show up as undefined references to code that is never
# reached.
# Archives last, and repeated (--start-group): the libraries reference each
# other and a single pass would miss symbols pulled in by a later member.
# -static-libstdc++ -static-libgcc, and this is not optional on this device.
#
# The first binary linked cleanly and then refused to start:
#
#   libstdc++.so.6: version `GLIBCXX_3.4.31' not found
#
# The Kindle's C++ runtime is from its 2013 firmware; the toolchain's gcc is
# 14.4. The library is there, it is just old. Carrying the C++ runtime inside
# the binary settles it, and leaves only glibc shared, which is safe: this
# binary needs GLIBC_2.4, the same as the fbink the jailbreak installs.
$CROSS -o "$OUT/crosspoint" $objs \
    -static-libstdc++ -static-libgcc \
    -Wl,--gc-sections \
    -Wl,--start-group $archives -Wl,--end-group \
    build/kindle/FBInk/Release/libfbink.a -lrt -lpthread 2>"$OUT/link.err"
echo "exit: $?"
echo
echo "--- undefined symbols, by frequency ---"
grep -oE "undefined reference to .[^']*'" "$OUT/link.err" \
  | sed "s/undefined reference to //" | sort | uniq -c | sort -rn | head -20
echo
echo "distinct undefined references: $(grep -oE "undefined reference to .[^']*'" "$OUT/link.err" | sort -u | wc -l | tr -d ' ')"
