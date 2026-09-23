// Host test for the multipart parser.
//
// The parser exists to stream a book from a browser to the filesystem without
// ever holding it, and the one invariant that makes that work is subtle: a
// delimiter can straddle two reads, so the tail of the window is never emitted
// until the next read proves it is not the start of one.
//
// A test that feeds the body in one large chunk would never exercise that. So
// the same bodies run through several chunk sizes, down to one byte at a time,
// which puts the delimiter across a read boundary at every possible offset.

#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "MultipartParser.h"

namespace {

using crosspoint::multipart::Callbacks;
using crosspoint::multipart::PartInfo;

struct Result {
  std::vector<PartInfo> starts;
  std::vector<std::string> files;  // bytes per file part, in order
  std::vector<bool> completes;
  std::vector<size_t> totals;
  std::vector<std::pair<std::string, std::string>> fields;
  bool ok = false;
};

// Runs `body` through the parser, handing out at most `chunk` bytes per read.
Result run(const std::string& boundary, const std::string& body, const size_t chunk) {
  Result r;
  size_t pos = 0;
  Callbacks cb;
  cb.fill = [&](uint8_t* buf, const size_t len) -> int {
    const size_t n = std::min({len, chunk, body.size() - pos});
    if (n == 0) {
      return 0;
    }
    std::memcpy(buf, body.data() + pos, n);
    pos += n;
    return static_cast<int>(n);
  };
  cb.onFileStart = [&](const PartInfo& i) {
    r.starts.push_back(i);
    r.files.emplace_back();
  };
  cb.onFileData = [&](const uint8_t* d, const size_t n) { r.files.back().append(reinterpret_cast<const char*>(d), n); };
  cb.onFileEnd = [&](const bool complete, const size_t total) {
    r.completes.push_back(complete);
    r.totals.push_back(total);
  };
  cb.onField = [&](const std::string& n, const std::string& v) { r.fields.emplace_back(n, v); };
  r.ok = crosspoint::multipart::parse(boundary, cb);
  return r;
}

int failures = 0;

void check(const bool cond, const char* what, const size_t chunk) {
  if (!cond) {
    std::printf("  FAIL (chunk=%zu): %s\n", chunk, what);
    ++failures;
  }
}

std::string buildBody(const std::string& boundary, const std::string& payload, const bool withField) {
  std::string b = "--" + boundary + "\r\n";
  if (withField) {
    b += "Content-Disposition: form-data; name=\"family\"\r\n\r\nLiterata\r\n";
    b += "--" + boundary + "\r\n";
  }
  b += "Content-Disposition: form-data; name=\"file\"; filename=\"book.epub\"\r\n";
  b += "Content-Type: application/epub+zip\r\n\r\n";
  b += payload;
  b += "\r\n--" + boundary + "--\r\n";
  return b;
}

void testPayload(const char* label, const std::string& payload, const bool withField) {
  const std::string boundary = "----WebKitFormBoundaryAbCdEf123456";
  const std::string body = buildBody(boundary, payload, withField);
  std::printf("%s (%zu bytes)\n", label, payload.size());
  // 1 byte at a time is the case that catches an eager tail emit; the odd
  // sizes land the delimiter at different offsets inside a read.
  for (const size_t chunk : {size_t{1}, size_t{2}, size_t{3}, size_t{7}, size_t{31}, size_t{2048}, size_t{1u << 20}}) {
    const Result r = run(boundary, body, chunk);
    check(r.ok, "parse returned false", chunk);
    check(r.files.size() == 1, "expected exactly one file part", chunk);
    if (r.files.size() == 1) {
      check(r.files[0] == payload, "file bytes differ from what was sent", chunk);
      check(r.totals[0] == payload.size(), "reported total differs from payload size", chunk);
    }
    check(!r.completes.empty() && r.completes[0], "file part not reported complete", chunk);
    check(!r.starts.empty() && r.starts[0].filename == "book.epub", "filename not parsed", chunk);
    check(!r.starts.empty() && r.starts[0].type == "application/epub+zip", "content-type not parsed", chunk);
    if (withField) {
      check(r.fields.size() == 1 && r.fields[0].first == "family" && r.fields[0].second == "Literata",
            "ordinary field not captured", chunk);
    }
  }
}

}  // namespace

int main() {
  // Ordinary content.
  testPayload("plain text", "The Strange Case of Dr Jekyll and Mr Hyde", false);

  // A field before the file, which is how the font upload posts.
  testPayload("field then file", "PK\x03\x04 pretend this is a zip", true);

  // Binary with embedded CRLF and NUL: an EPUB is a zip and contains both.
  {
    std::string bin;
    for (int i = 0; i < 5000; ++i) {
      bin.push_back(static_cast<char>(i % 256));
    }
    testPayload("binary with NUL and CRLF", bin, false);
  }

  // The trap: content that starts like the delimiter but is not one. If the
  // parser emits its window tail eagerly, or matches on a prefix, this is where
  // the file comes out truncated or corrupt.
  testPayload("content containing a partial delimiter",
              std::string("before\r\n------WebKitFormBoundaryAbCdEf12345 not the delimiter\r\nafter"), false);

  // Empty file: a zero-byte upload must still produce START and END.
  testPayload("empty file", "", false);

  // A truncated body must report the part as incomplete rather than claiming
  // success, so the caller deletes the partial file instead of shelving it.
  {
    const std::string boundary = "----b";
    std::string body = buildBody(boundary, "some bytes that never finish", false);
    body.resize(body.size() - 12);  // cut the closing delimiter off
    const Result r = run(boundary, body, 4);
    std::printf("truncated body\n");
    check(!r.ok, "a truncated body must not report success", 4);
    check(!r.completes.empty() && !r.completes[0], "a truncated part must be reported incomplete", 4);
  }

  // A file part of any size is fine: it streams out and never accumulates.
  // A field is not, because the caller wants it whole, so it has a ceiling.
  {
    const std::string boundary = "----b";
    std::string body = "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"huge\"\r\n\r\n";
    body += std::string(crosspoint::multipart::MAX_FIELD_BYTES + 1024, 'x');
    body += "\r\n--" + boundary + "--\r\n";
    const Result r = run(boundary, body, 512);
    std::printf("oversized field\n");
    check(!r.ok, "an oversized field must be refused rather than buffered", 512);
    check(r.fields.empty(), "an oversized field must not be delivered", 512);
  }

  // The same body as a FILE part must go through untouched, which is the whole
  // point of the distinction.
  testPayload("file larger than the field cap", std::string(crosspoint::multipart::MAX_FIELD_BYTES + 1024, 'y'), false);

  if (failures == 0) {
    std::printf("\nall multipart parser checks passed\n");
    return 0;
  }
  std::printf("\n%d check(s) failed\n", failures);
  return 1;
}
