set -u
CROSS=arm-kindlepw2-linux-gnueabi-g++
INC="-Ilib/hal/kindle/arduino-shim -Ibuild/kindle/FBInk/libunibreak/src -Ibuild/kindle/thirdparty -Ibuild/kindle/FBInk"
for d in $(find freeink-sdk/libs -type d -name include); do INC="$INC -I$d"; done
for d in lib/*/; do INC="$INC -I${d%/}"; done
INC="$INC -Ilib/hal/kindle -Ilib/hal -Isrc -Ilib -Isrc/components -Isrc/activities -Isrc/util -Isrc/network"
DEF="-DFREEINK_DEVICE_KINDLE=1"
: > build/kindle/census-errors.txt
ok=0; fail=0
for f in $(find src lib freeink-sdk/libs -name '*.cpp' 2>/dev/null | grep -vE 'expat|miniz|uzlib|/test/|/tools/|FBInk'); do
  if $CROSS -std=c++20 -fsyntax-only $INC $DEF "$f" >build/kindle/one.err 2>&1; then
    ok=$((ok+1))
  else
    fail=$((fail+1))
    grep -m1 -E "fatal error|error:" build/kindle/one.err | sed "s|.*error: ||" >> build/kindle/census-errors.txt
  fi
done
echo "CENSO: $ok de $((ok+fail)) arquivos compilam ($(( ok * 100 / (ok+fail) ))%)"
echo
echo "--- causas das falhas, por frequencia ---"
sed -E "s/'[^']*' file not found/HEADER/; s/([a-zA-Z0-9_\/.]+\.h): No such file.*/missing header: \1/" build/kindle/census-errors.txt \
  | cut -c1-72 | sort | uniq -c | sort -rn | head -18
