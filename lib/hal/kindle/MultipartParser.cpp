#include "MultipartParser.h"

#include <algorithm>

namespace crosspoint::multipart {
namespace {

std::string lower(std::string v) {
  std::transform(v.begin(), v.end(), v.begin(), [](const unsigned char c) { return static_cast<char>(tolower(c)); });
  return v;
}

// Extracts key="value" from a Content-Disposition line.
std::string quoted(const std::string& line, const char* key) {
  const std::string needle = std::string(key) + "=\"";
  const size_t at = line.find(needle);
  if (at == std::string::npos) {
    return {};
  }
  const size_t start = at + needle.size();
  const size_t end = line.find('"', start);
  return end == std::string::npos ? std::string() : line.substr(start, end - start);
}

}  // namespace

bool parse(const std::string& boundary, const Callbacks& cb) {
  if (boundary.empty() || !cb.fill) {
    return false;
  }
  // What separates parts on the wire, as opposed to the token in the header.
  const std::string delim = "\r\n--" + boundary;
  constexpr size_t CHUNK = 2048;

  std::string win;
  bool eof = false;

  const auto fill = [&]() -> bool {
    if (eof) {
      return false;
    }
    uint8_t tmp[CHUNK];
    const int n = cb.fill(tmp, sizeof(tmp));
    if (n <= 0) {
      eof = true;
      return false;
    }
    win.append(reinterpret_cast<const char*>(tmp), static_cast<size_t>(n));
    return true;
  };

  const auto takeLine = [&](std::string* out) -> bool {
    for (;;) {
      const size_t nl = win.find("\r\n");
      if (nl != std::string::npos) {
        *out = win.substr(0, nl);
        win.erase(0, nl + 2);
        return true;
      }
      // A peer that never sends the CRLF would otherwise be an unbounded read.
      if (win.size() > MAX_HEADER_LINE) {
        return false;
      }
      if (!fill()) {
        return false;
      }
    }
  };

  // The preamble: the first delimiter arrives without a leading CRLF.
  std::string first;
  if (!takeLine(&first) || first.rfind("--" + boundary, 0) != 0) {
    return false;
  }

  size_t parts = 0;
  for (;;) {
    if (++parts > MAX_PARTS) {
      return false;
    }
    PartInfo info;
    for (;;) {
      std::string line;
      if (!takeLine(&line)) {
        return false;
      }
      if (line.empty()) {
        break;
      }
      const std::string low = lower(line);
      if (low.rfind("content-disposition:", 0) == 0) {
        info.name = quoted(line, "name");
        info.filename = quoted(line, "filename");
      } else if (low.rfind("content-type:", 0) == 0) {
        size_t v = line.find(':') + 1;
        while (v < line.size() && line[v] == ' ') {
          ++v;
        }
        info.type = line.substr(v);
      }
    }

    const bool isFile = !info.filename.empty();
    std::string fieldValue;
    size_t total = 0;

    if (isFile && cb.onFileStart) {
      cb.onFileStart(info);
    }

    bool closed = false;
    for (;;) {
      const size_t at = win.find(delim);
      if (at != std::string::npos) {
        if (at > 0) {
          if (isFile) {
            total += at;
            if (cb.onFileData) {
              cb.onFileData(reinterpret_cast<const uint8_t*>(win.data()), at);
            }
          } else if (fieldValue.size() + at > MAX_FIELD_BYTES) {
            return false;
          } else {
            fieldValue.append(win, 0, at);
          }
        }
        win.erase(0, at + delim.size());
        closed = true;
        break;
      }

      // No delimiter in the window. Everything except a possible partial one at
      // the tail is safe to emit; see the header for why the tail waits.
      if (win.size() > delim.size()) {
        const size_t emit = win.size() - delim.size();
        if (isFile) {
          total += emit;
          if (cb.onFileData) {
            cb.onFileData(reinterpret_cast<const uint8_t*>(win.data()), emit);
          }
        } else if (fieldValue.size() + emit > MAX_FIELD_BYTES) {
          // Only a field can grow here: a file part streams out and never
          // accumulates, which is why the cap applies to one and not the other.
          return false;
        } else {
          fieldValue.append(win, 0, emit);
        }
        win.erase(0, emit);
      }
      if (!fill()) {
        break;
      }
    }

    if (isFile) {
      if (cb.onFileEnd) {
        cb.onFileEnd(closed, total);
      }
    } else if (closed && !info.name.empty() && cb.onField) {
      cb.onField(info.name, fieldValue);
    }

    if (!closed) {
      return false;
    }

    // After a delimiter: "--" ends the body, CRLF starts another part.
    while (win.size() < 2 && fill()) {
    }
    if (win.size() >= 2 && win[0] == '-' && win[1] == '-') {
      return true;
    }
    if (win.size() >= 2 && win[0] == '\r' && win[1] == '\n') {
      win.erase(0, 2);
      continue;
    }
    return true;
  }
}

}  // namespace crosspoint::multipart
