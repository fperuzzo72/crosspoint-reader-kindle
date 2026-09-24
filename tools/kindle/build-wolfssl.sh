#!/bin/sh
# Cross-compile wolfSSL for the Kindle, so the tree's TLS path can be used.
#
#   docker run --rm -v "$PWD:/src" -w /src crosspoint-kindle-tc:latest \
#       sh tools/kindle/build-wolfssl.sh
#
# Why this exists: HttpDownloader already has a wolfSSL branch behind
# FREEINK_NET_WOLFSSL, and the SDK's SecureHttpClient/SecureClient sit on top of
# Arduino's Client interface, which this port's shim already provides. So TLS
# here is a cross-build problem, not a "write TLS" problem — the layer above is
# already written and exercised on the ESP32 targets.
#
# The version matches platformio.ini's `wolfssl/Arduino-wolfSSL @ 5.7.2` pin,
# but the SOURCE is upstream rather than the Arduino repackaging. The Arduino
# one carries a user_settings.h shaped for an ESP32's heap, and scripts/
# patch_wolfssl.py exists to edit that file at build time. Autotools expresses
# the same choices as configure flags, and none of the memory pressure those
# settings answer to exists on a device with 256 MB.
set -eu

VER=5.7.2
OUT=build/kindle/wolfssl
SRC="$OUT/wolfssl-${VER}-stable"
PREFIX="$(pwd)/$OUT/install"

if [ -f "$PREFIX/lib/libwolfssl.a" ]; then
    echo "--- libwolfssl.a already built"
    exit 0
fi

mkdir -p "$OUT"
if [ ! -d "$SRC" ]; then
    echo "--- fetching wolfSSL $VER"
    curl -fsSL "https://github.com/wolfSSL/wolfssl/archive/refs/tags/v${VER}-stable.tar.gz" \
        | tar xz -C "$OUT"
fi

cd "$SRC"

# Accept certificates whose serial number is zero, and ONLY that.
#
# wolfSSL rejects them on purpose: RFC 5280 requires a positive serial, so a CA
# issuing 0 is non-conforming. Six of the 121 CAs in a current Mozilla bundle do
# exactly that, among them Go Daddy Root G2 and both Starfield roots, and every
# other TLS stack accepts them, so a catalogue chaining there fails only here.
# The user asked for the check to be relaxed.
#
# WOLFSSL_NO_ASN_STRICT is the documented switch and is NOT what this does. That
# macro guards seventeen conformance checks in asn.c, and they apply to every
# certificate parsed, including the ones a server presents. This build verifies
# certificates precisely so it does not have to trust whatever answers, so it
# gives up one rule rather than seventeen.
#
# Patching the source rather than defining WOLFSSL_PYTHON, which guards this
# same line and would also work: that macro says something untrue about what
# this is, and a later version is free to hang more behaviour on it.
SERIAL_GUARD='    #if !defined(WOLFSSL_NO_ASN_STRICT) \&\& !defined(WOLFSSL_PYTHON)'
SERIAL_FILE=wolfcrypt/src/asn.c
if grep -q 'CrossPoint: serial-zero' "$SERIAL_FILE"; then
    echo "--- serial-zero check already relaxed"
else
    hits=$(grep -c "$SERIAL_GUARD" "$SERIAL_FILE" || true)
    if [ "$hits" != "1" ]; then
        # Loudly, because the alternative is a build that silently stops
        # accepting six trust anchors again.
        echo "ERROR: expected exactly one serial-zero guard in $SERIAL_FILE, found $hits."
        echo "       wolfSSL $VER may have moved it; re-read asn.c before trusting this build."
        exit 1
    fi
    sed -i "s|$SERIAL_GUARD|    #if 0 /* CrossPoint: serial-zero check relaxed, see build-wolfssl.sh */|" \
        "$SERIAL_FILE"
    echo "--- relaxed the serial-zero check (only that one)"
fi

if [ ! -f configure ]; then
    echo "--- generating configure"
    ./autogen.sh
fi

echo "--- configuring for arm-kindlepw2-linux-gnueabi"
# The feature set is the one the tree asks for in platformio.ini: TLS 1.3, SNI,
# RFC 6066 max fragment length, Curve25519 and FFDHE-2048. Left out on purpose
# are WOLFSSL_SP_SMALL and the FP_MAX_BITS cap, which are there to survive an
# ESP32's ~50KB of free heap; here they would only make it slower.
#
#
# --enable-altcertchains is the other one without which ordinary sites fail.
# By default wolfSSL requires the chain the server PRESENTS to end at a root it
# trusts. Cross-signed chains do not: www.gutenberg.org sends leaf, Network
# Solutions CA 3, USERTrust (cross-signed), and finally AAA Certificate
# Services, which is self-signed and no longer in Mozilla's bundle. The trusted
# anchor, USERTrust, sits in the MIDDLE of what was sent. Alternative chains let
# any presented certificate that is directly trusted end the walk, which is what
# browsers and OpenSSL have always done. Without it the failure is
# ASN_NO_SIGNER_E, which reads as "the root is missing" while the root is loaded.
# --with-max-rsa-bits=4096 is NOT optional, and omitting it fails silently
# rather than loudly. With WOLFSSL_SP_MATH_ALL and no HAVE_FFDHE_4096,
# sp_int.h applies its own default -- "Default to max 3072 for general RSA and
# DH" -- and RSA_MAX_SIZE follows it. A signature made by a 4096-bit key then
# fails to verify with ASN_SIG_CONFIRM_E, which reads like a bad certificate
# and is not. 59 of the 121 CAs in a current Mozilla bundle are RSA-4096,
# including the USERTrust root that www.gutenberg.org chains to, so this is
# most of the public trust store rather than an edge case.
./configure \
    --host=arm-kindlepw2-linux-gnueabi \
    --prefix="$PREFIX" \
    --enable-static --disable-shared \
    --enable-tls13 \
    --enable-sni \
    --enable-maxfragment \
    --enable-curve25519 \
    --enable-dh \
    --enable-supportedcurves \
    --with-max-rsa-bits=4096 \
    --enable-altcertchains \
    --disable-examples \
    --disable-crypttests \
    --disable-oldtls

echo "--- building"
make -j"$(nproc)" >/dev/null
make install >/dev/null

echo
echo "--- built ---"
ls -l "$PREFIX/lib/libwolfssl.a" | awk '{print "  libwolfssl.a:", $5, "bytes"}'
"${CROSS_TC:-arm-kindlepw2-linux-gnueabi}"-readelf -h "$PREFIX/lib/libwolfssl.a" 2>/dev/null | head -0 || true
echo "  headers in: $PREFIX/include/wolfssl"
echo
echo "--- the other half: trust anchors ---"
echo "TLS without verification would be worse than the refusal it replaces, because"
echo "these requests carry preemptive HTTP Basic credentials. Put a PEM bundle at"
echo "/mnt/us/crosspoint/cacert.pem on the device; the reader refuses https without"
echo "one rather than skipping the check. A current set:"
echo "    curl -fsSL -o cacert.pem https://curl.se/ca/cacert.pem"
