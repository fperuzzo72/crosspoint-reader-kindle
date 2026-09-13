// Arduino String semantics, kept in a portable translation unit.
//
// The timing and ESP halves need <sys/sysinfo.h> and /proc, so they only build
// on Linux. String does not, and it is the half where the behaviour is subtle
// enough to be worth testing: the divergences from std::string (indexOf's -1,
// substring's clamping, remove's tolerance of out-of-range indices) are
// exactly what the app tree leans on, and exactly what a "sensible" rewrite
// would quietly get wrong.

#include "ArduinoCompat.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

namespace {

int toIndex(const size_t pos) {
  return pos == std::string::npos ? -1 : static_cast<int>(pos);
}

}  // namespace

int String::indexOf(const String& needle, const size_t from) const {
  if (from > buf.size()) {
    return -1;
  }
  return toIndex(buf.find(needle.buf, from));
}

int String::indexOf(const char needle, const size_t from) const {
  if (from > buf.size()) {
    return -1;
  }
  return toIndex(buf.find(needle, from));
}

int String::lastIndexOf(const String& needle) const { return toIndex(buf.rfind(needle.buf)); }

int String::lastIndexOf(const char needle) const { return toIndex(buf.rfind(needle)); }

bool String::startsWith(const String& prefix) const {
  return buf.size() >= prefix.buf.size() && buf.compare(0, prefix.buf.size(), prefix.buf) == 0;
}

bool String::endsWith(const String& suffix) const {
  return buf.size() >= suffix.buf.size() &&
         buf.compare(buf.size() - suffix.buf.size(), suffix.buf.size(), suffix.buf) == 0;
}

bool String::equalsIgnoreCase(const String& other) const {
  if (buf.size() != other.buf.size()) {
    return false;
  }
  for (size_t i = 0; i < buf.size(); ++i) {
    if (std::tolower(static_cast<unsigned char>(buf[i])) != std::tolower(static_cast<unsigned char>(other.buf[i]))) {
      return false;
    }
  }
  return true;
}

String String::substring(const size_t from) const {
  if (from >= buf.size()) {
    return String();
  }
  return String(buf.substr(from));
}

String String::substring(const size_t from, const size_t to) const {
  // Arduino clamps rather than throwing, and callers lean on that.
  const size_t begin = std::min(from, buf.size());
  const size_t end = std::min(to, buf.size());
  if (begin >= end) {
    return String();
  }
  return String(buf.substr(begin, end - begin));
}

void String::remove(const size_t index) {
  if (index < buf.size()) {
    buf.erase(index);
  }
}

void String::remove(const size_t index, const size_t count) {
  if (index < buf.size()) {
    buf.erase(index, count);
  }
}

void String::replace(const String& from, const String& to) {
  if (from.buf.empty()) {
    return;  // would not terminate
  }
  size_t pos = 0;
  while ((pos = buf.find(from.buf, pos)) != std::string::npos) {
    buf.replace(pos, from.buf.size(), to.buf);
    pos += to.buf.size();
  }
}

void String::trim() {
  const auto notSpace = [](const unsigned char c) { return std::isspace(c) == 0; };
  buf.erase(buf.begin(), std::find_if(buf.begin(), buf.end(), notSpace));
  buf.erase(std::find_if(buf.rbegin(), buf.rend(), notSpace).base(), buf.end());
}

void String::toUpperCase() {
  std::transform(buf.begin(), buf.end(), buf.begin(),
                 [](const unsigned char c) { return static_cast<char>(std::toupper(c)); });
}

void String::toLowerCase() {
  std::transform(buf.begin(), buf.end(), buf.begin(),
                 [](const unsigned char c) { return static_cast<char>(std::tolower(c)); });
}

long String::toInt() const { return std::strtol(buf.c_str(), nullptr, 10); }

double String::toFloat() const { return std::strtod(buf.c_str(), nullptr); }

