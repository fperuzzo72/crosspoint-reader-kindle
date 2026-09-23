#pragma once

// Streaming parser for multipart/form-data bodies.
//
// Pulled out of the web server on purpose. Everything else in that file needs a
// socket and a peer to do anything at all; this does not, so it is the one
// piece whose correctness a host test can hold to account. The same reasoning
// put the 1bpp expansion in lib/hal/hosted/HostedGray.
//
// The whole point is that nothing here holds the payload. A book is tens of
// megabytes arriving as fast as the Wi-Fi allows; buffering the body to parse
// it would be the one thing this must not do. The delimiter is searched for
// inside a small sliding window and everything safely before it is emitted at
// once.
//
// The subtlety that makes or breaks it: a delimiter can straddle two reads. The
// tail of the window, as long as the delimiter itself, is therefore never
// emitted until the next read proves it is not the start of one. Emitting it
// eagerly writes the first bytes of "\r\n--boundary" into the book, and the
// corruption is at the end of a chunk rather than at the start, so it survives
// a casual look at the file.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

namespace crosspoint::multipart {

// Caps on the parts that are NOT streamed.
//
// A file part costs nothing to be large: it goes out chunk by chunk and never
// accumulates. An ordinary field does accumulate, because a caller wants it
// whole, so it needs a ceiling or a client that posts a gigabyte without a
// filename makes the reader allocate a gigabyte.
//
// The same reasoning caps the part headers: they are read line by line into a
// string before the blank line that ends them, and a peer that never sends that
// blank line would otherwise be an unbounded read.
//
// Values chosen to be far above anything CrossPoint's own pages send (the
// largest is a font family name) and far below anything that hurts.
inline constexpr size_t MAX_FIELD_BYTES = 64u * 1024u;
inline constexpr size_t MAX_HEADER_LINE = 8u * 1024u;
inline constexpr size_t MAX_PARTS = 64;

struct PartInfo {
  std::string name;
  std::string filename;  // empty for an ordinary form field
  std::string type;
};

struct Callbacks {
  // Reads up to `len` bytes. Returns how many, or <= 0 at the end of the body.
  std::function<int(uint8_t*, size_t)> fill;

  // A file part begins. Only called when filename is non-empty.
  std::function<void(const PartInfo&)> onFileStart;
  // A chunk of the current file part. The pointer is valid only for this call.
  std::function<void(const uint8_t*, size_t)> onFileData;
  // The file part ended. `complete` is false when the body was cut short.
  std::function<void(bool complete, size_t total)> onFileEnd;

  // An ordinary field, delivered whole because form fields are small.
  std::function<void(const std::string& name, const std::string& value)> onField;
};

// Returns false when the body is malformed or the peer vanished mid-part.
// A false return does not mean nothing happened: onFileEnd(false, ...) has
// already told the caller what to undo.
bool parse(const std::string& boundary, const Callbacks& cb);

}  // namespace crosspoint::multipart
