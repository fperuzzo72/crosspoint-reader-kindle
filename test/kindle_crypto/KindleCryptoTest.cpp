// MD5 and base64 against published vectors.
//
// These are the two places in the shim where being WRONG is as damaging as
// being absent. KOReader's sync identifies a document by an MD5; if this
// implementation disagreed with every other one, reading positions would
// silently attach to the wrong books. So: RFC 1321's own test suite, and
// RFC 4648's base64 vectors, rather than round-tripping against itself, which
// would pass for any self-consistent nonsense.

#include <gtest/gtest.h>

#include "MD5Builder.h"
#include <cstring>
#include <string>

#include "base64.h"

namespace {

String md5Of(const char* text) {
  MD5Builder b;
  b.begin();
  b.add(text);
  b.calculate();
  return b.toString();
}

// RFC 1321, section A.5.
TEST(KindleCrypto, Md5MatchesRfc1321Vectors) {
  EXPECT_EQ(md5Of("").str(), "d41d8cd98f00b204e9800998ecf8427e");
  EXPECT_EQ(md5Of("a").str(), "0cc175b9c0f1b6a831c399e269772661");
  EXPECT_EQ(md5Of("abc").str(), "900150983cd24fb0d6963f7d28e17f72");
  EXPECT_EQ(md5Of("message digest").str(), "f96b697d7cb7938d525a2f31aaf161d0");
  EXPECT_EQ(md5Of("abcdefghijklmnopqrstuvwxyz").str(), "c3fcd3d76192e4007dfb496cca67e13b");
  EXPECT_EQ(md5Of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789").str(),
            "d174ab98d277d9f5a5611c2c9f419d9f");
  EXPECT_EQ(md5Of("12345678901234567890123456789012345678901234567890123456789012345678901234567890").str(),
            "57edf4a22be3c955ac49da2e2107b67a");
}

TEST(KindleCrypto, Md5HandlesTheBlockBoundaryExactly) {
  // 55, 56 and 64 bytes are where the padding logic either works or does not:
  // 56 is the first length that forces a second block, and 64 is a whole one.
  const std::string a55(55, 'a');
  const std::string a56(56, 'a');
  const std::string a64(64, 'a');
  EXPECT_EQ(md5Of(a55.c_str()).str(), "ef1772b6dff9a122358552954ad0df65");
  EXPECT_EQ(md5Of(a56.c_str()).str(), "3b0c8ac703f828b04c6c197006d17218");
  EXPECT_EQ(md5Of(a64.c_str()).str(), "014842d480b571495a4a0363793f7367");
}

TEST(KindleCrypto, Md5AccumulatesAcrossCalls) {
  // The sync client feeds a document in chunks, so a digest built piecewise
  // must equal one built in a single call.
  MD5Builder chunked;
  chunked.begin();
  chunked.add("mess");
  chunked.add("age ");
  chunked.add("digest");
  chunked.calculate();
  EXPECT_EQ(chunked.toString().str(), md5Of("message digest").str());
}

TEST(KindleCrypto, Md5GetBytesMatchesTheHexForm) {
  MD5Builder b;
  b.begin();
  b.add("abc");
  b.calculate();
  uint8_t raw[16] = {0};
  b.getBytes(raw);
  // 900150983cd24fb0d6963f7d28e17f72
  EXPECT_EQ(raw[0], 0x90);
  EXPECT_EQ(raw[1], 0x01);
  EXPECT_EQ(raw[15], 0x72);
}

// RFC 4648, section 10.
TEST(KindleCrypto, Base64MatchesRfc4648Vectors) {
  EXPECT_EQ(base64::encode(String("")).str(), "");
  EXPECT_EQ(base64::encode(String("f")).str(), "Zg==");
  EXPECT_EQ(base64::encode(String("fo")).str(), "Zm8=");
  EXPECT_EQ(base64::encode(String("foo")).str(), "Zm9v");
  EXPECT_EQ(base64::encode(String("foob")).str(), "Zm9vYg==");
  EXPECT_EQ(base64::encode(String("fooba")).str(), "Zm9vYmE=");
  EXPECT_EQ(base64::encode(String("foobar")).str(), "Zm9vYmFy");
}

TEST(KindleCrypto, Base64DecodesTheSameVectors) {
  EXPECT_EQ(base64::decode(String("Zg==")).str(), "f");
  EXPECT_EQ(base64::decode(String("Zm8=")).str(), "fo");
  EXPECT_EQ(base64::decode(String("Zm9v")).str(), "foo");
  EXPECT_EQ(base64::decode(String("Zm9vYmFy")).str(), "foobar");
}

TEST(KindleCrypto, Base64HandlesBinaryAndTheFullAlphabet) {
  // Bytes above 0x7F must survive; a char-signedness slip mangles them and
  // would only show on non-ASCII content.
  const uint8_t binary[] = {0x00, 0xFF, 0x80, 0x7F, 0xFE};
  const String encoded = base64::encode(binary, sizeof(binary));
  const String decoded = base64::decode(encoded);
  ASSERT_EQ(decoded.length(), sizeof(binary));
  for (size_t i = 0; i < sizeof(binary); ++i) {
    EXPECT_EQ(static_cast<uint8_t>(decoded.charAt(i)), binary[i]) << "byte " << i;
  }

  // '+' and '/' only appear for particular bit patterns; make sure they do.
  const uint8_t needsBoth[] = {0xFB, 0xEF, 0xFF};
  EXPECT_EQ(base64::encode(needsBoth, sizeof(needsBoth)).str(), "++//");
}

TEST(KindleCrypto, Base64DecodeSkipsWhitespaceLikeEveryOtherDecoder) {
  EXPECT_EQ(base64::decode(String("Zm9v\r\nYmFy")).str(), "foobar");
}

// RFC 3174 / FIPS 180-1 vectors, plus the worked example from RFC 6455 itself.
// A wrong SHA-1 here fails loudly rather than silently, since the browser
// simply refuses the upgrade, but that failure is far from its cause.
TEST(KindleCrypto, Sha1MatchesPublishedVectors) {
  const auto hex = [](const char* text) {
    uint8_t d[20];
    sha1(reinterpret_cast<const uint8_t*>(text), std::strlen(text), d);
    char out[41];
    for (int i = 0; i < 20; ++i) {
      std::snprintf(out + i * 2, 3, "%02x", d[i]);
    }
    return std::string(out);
  };

  EXPECT_EQ(hex(""), "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  EXPECT_EQ(hex("abc"), "a9993e364706816aba3e25717850c26c9cd0d89d");
  EXPECT_EQ(hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
            "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}

TEST(KindleCrypto, Sha1HandlesTheBlockBoundary) {
  const auto hexOf = [](const std::string& in) {
    uint8_t d[20];
    sha1(reinterpret_cast<const uint8_t*>(in.data()), in.size(), d);
    char out[41];
    for (int i = 0; i < 20; ++i) {
      std::snprintf(out + i * 2, 3, "%02x", d[i]);
    }
    return std::string(out);
  };
  // 55, 56 and 64 bytes: where the length field either fits in the block or
  // forces another one.
  EXPECT_EQ(hexOf(std::string(55, 'a')), "c1c8bbdc22796e28c0e15163d20899b65621d65a");
  EXPECT_EQ(hexOf(std::string(56, 'a')), "c2db330f6083854c99d4b5bfb6e8f29f201be699");
  EXPECT_EQ(hexOf(std::string(64, 'a')), "0098ba824b5c16427bd7a1122a5a442a25ec644d");
}

TEST(KindleCrypto, WebSocketHandshakeAcceptMatchesRfc6455) {
  // RFC 6455 section 1.3's worked example: this exact key must produce this
  // exact accept value, or every browser refuses the upgrade.
  const std::string key = "dGhlIHNhbXBsZSBub25jZQ==258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
  uint8_t d[20];
  sha1(reinterpret_cast<const uint8_t*>(key.data()), key.size(), d);
  EXPECT_EQ(base64::encode(d, sizeof(d)).str(), "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

}  // namespace
