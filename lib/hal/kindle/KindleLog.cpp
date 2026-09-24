#include "KindleLog.h"

#include <cstdio>
#include <cstring>

namespace KindleLog {
namespace {

constexpr char LOG_PATH[] = "/mnt/us/crosspoint.log";
constexpr char PREV_PATH[] = "/mnt/us/crosspoint.log.prev";

// 256 KB, and one generation back, so the pair never exceeds half a megabyte.
//
// Sized against what a reading session actually produces at LOG_LEVEL 1, which
// is a few lines per page turn and three per image: a long evening is tens of
// kilobytes. The cap is not there for normal use, it is there for the run that
// goes wrong and logs the same error forever, which is exactly the run whose
// evidence matters and the one that would otherwise fill the card.
constexpr long MAX_BYTES = 256L * 1024L;

// ftell is a seek on this stream, since main() sets it unbuffered, and asking
// on every line would put a syscall between the reader and each log write.
// Checking every 64th is still at most 64 lines of overshoot on a 256 KB cap.
constexpr unsigned CHECK_EVERY = 64;

bool owned = false;
unsigned sinceCheck = 0;

// Reopening onto the same descriptor is what keeps every existing call site
// working, including the crash handler's raw write to fd 2.
bool pointStderrAt(const char* const path, const char* const mode) {
  return std::freopen(path, mode, stderr) != nullptr;
}

void rotate() {
  std::fflush(stderr);
  // Rename rather than truncate: the run that just filled the log is the one
  // worth reading, and it is about to stop being the current file.
  std::remove(PREV_PATH);
  std::rename(LOG_PATH, PREV_PATH);
  if (!pointStderrAt(LOG_PATH, "w")) {
    // The stream is gone and there is nowhere to say so. Stop checking rather
    // than retry a seek against a closed file on every line.
    owned = false;
    return;
  }
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  std::fprintf(stderr, "[kindle] log rotated at %ld bytes; previous run continues in %s\n", MAX_BYTES, PREV_PATH);
}

}  // namespace

void begin() {
  // Start each run on a fresh file and keep the one before it. The launcher
  // does the same for its own log, for the same reason: retrying after a crash
  // used to be what destroyed the record of the crash.
  std::remove(PREV_PATH);
  std::rename(LOG_PATH, PREV_PATH);

  if (!pointStderrAt(LOG_PATH, "w")) {
    // /mnt/us is not writable when the device is plugged into a computer, and
    // a reader that refuses to start because of that would be worse than one
    // that logs to the launcher's capture instead. Nothing else changes.
    owned = false;
    return;
  }
  std::setvbuf(stderr, nullptr, _IONBF, 0);
  owned = true;
}

void rotateIfNeeded() {
  if (!owned) {
    return;
  }
  if (++sinceCheck < CHECK_EVERY) {
    return;
  }
  sinceCheck = 0;
  const long pos = std::ftell(stderr);
  if (pos >= MAX_BYTES) {
    rotate();
  }
}

}  // namespace KindleLog
