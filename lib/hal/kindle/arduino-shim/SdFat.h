#pragma once

// SdFat over POSIX.
//
// This one header unblocked more of the tree than anything else in the port:
// 111 files could not compile without it, all of them reaching it transitively
// through HalStorage.h rather than because they care about SD cards.
//
// And they do not care. A Kindle has a real filesystem with a real VFS; the SD
// abstraction is an artefact of the ESP32 targets. So rather than vendor SdFat
// and give it a block device, the API is presented over open/read/write and
// the tree keeps its existing shape.
//
// The surface here is what the tree measurably calls, nothing more, same rule
// as the rest of the shim: a missing method is a compile error at the real
// call site, which beats a stub that quietly returns the wrong thing.

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>

#include <cstdint>
#include <cstdio>

#include "Stream.h"
#include "common/FsApiConstants.h"

// Where "/" means, from the app's point of view.
//
// CrossPoint is written for a device whose storage root IS the filesystem
// root: HalStorage::listFiles defaults to "/" and the browser starts there. On
// a Kindle that is the real Linux root, which is why the file browser showed
// the whole system.
//
// Every path entering this shim is resolved against this prefix, so the app
// keeps thinking it holds a card while never seeing outside /mnt/us. Doing it
// here rather than in the app keeps the diff off code this port does not own,
// and covers every call site at once.
namespace crosspoint_storage {
const char* root();
void setRoot(const char* path);
// Prefixes `path` with the root unless it is already inside it. The returned
// pointer is valid until the next call on this thread.
const char* resolve(const char* path);
}  // namespace crosspoint_storage

class FsFile : public Stream {
 public:
  FsFile() = default;
  ~FsFile() override;

  // Movable, not copyable: two handles owning one descriptor would double
  // close it.
  FsFile(const FsFile&) = delete;
  FsFile& operator=(const FsFile&) = delete;
  FsFile(FsFile&& other) noexcept;
  FsFile& operator=(FsFile&& other) noexcept;

  bool open(const char* path, oflag_t flags = O_RDONLY);
  // Returns bool: HalFile::close() forwards the result straight through.
  bool close();
  bool isOpen() const { return fd >= 0 || dir != nullptr; }
  explicit operator bool() const { return isOpen(); }

  int read(void* buf, size_t count);
  int read(uint8_t* buf, size_t count) override;
  int read() override;
  int peek() override;
  int available() override;
  // Same name-hiding trap as WiFiClient: without this, write("literal")
  // resolves against the uint8_t overload instead of Print's char* one.
  using Print::write;
  size_t write(uint8_t c) override;
  size_t write(const uint8_t* buf, size_t count) override;
  size_t write(const void* buf, size_t count);
  void flush() override;

  bool seek(uint64_t pos);
  bool seekSet(uint64_t pos) { return seek(pos); }
  bool seekCur(int64_t delta);
  bool seekEnd(int64_t delta = 0);
  uint64_t position();
  uint64_t size();
  // SdFat's own name for the same thing; HalStorage uses this spelling.
  uint64_t fileSize() { return size(); }
  uint32_t available32() { return static_cast<uint32_t>(available()); }
  bool truncate(uint64_t length);
  // SdFat lets an open file rename itself; POSIX renames by path, so the open
  // handle's own path is what moves.
  bool rename(const char* newPath);
  // SdFat's newer API returns a bool; HalStorage checks it.
  bool rewind() { return seek(0); }
  // Arduino's File spells directory iteration differently, and HalStorage uses
  // that spelling.
  // void, matching HalFile::rewindDirectory(), whose forwarding macro returns
  // whatever this returns.
  void rewindDirectory();
  FsFile openNextFile(oflag_t flags = O_RDONLY);

  bool isDirectory() const { return dir != nullptr; }
  bool isDir() const { return isDirectory(); }
  // Directory iteration: opens the next entry of this directory into `entry`.
  bool openNext(FsFile* entry, oflag_t flags = O_RDONLY);

  size_t getName(char* out, size_t len) const;
  void printName(Print* out) const;

 private:
  void adopt(FsFile&& other) noexcept;

  int fd = -1;
  DIR* dir = nullptr;
  char path[512] = {0};
};

// The volume. SdFat has several spellings of this depending on filesystem;
// they are all the same thing here, because the kernel already mounted it.
class SdFs {
 private:
  char volumePath[256] = "/mnt/us";  // kept in step with crosspoint_storage::root()

 public:
  // The ESP32 targets pass a bus config; there is no bus here. Accepting and
  // ignoring it keeps the call sites unchanged.
  template <typename... Args>
  bool begin(Args&&...) {
    return true;
  }
  void end() {}

  bool exists(const char* path) const;
  bool mkdir(const char* path, bool createParents = true);
  bool rmdir(const char* path);
  bool remove(const char* path);
  bool rename(const char* from, const char* to);
  bool open(FsFile* file, const char* path, oflag_t flags = O_RDONLY) { return file->open(path, flags); }
  FsFile open(const char* path, oflag_t flags = O_RDONLY);

  // Card-level error diagnostics. SdFat reports why an SPI transaction to the
  // card failed; there is no card and no SPI here, so there is never an error
  // to report and these answer zero. The log line that prints them stays
  // truthful: it says no error, because there was none.
  uint8_t sdErrorCode() const { return 0; }
  uint8_t sdErrorData() const { return 0; }

  // Capacity, in the units SdFat reports them. Implemented over statvfs rather
  // than returned as zero: CrossPoint shows free space to the user, and zero
  // would read as a full card. There are no clusters here, so a filesystem
  // block is reported as the cluster and the arithmetic the callers do
  // (count * bytesPerCluster) comes out right.
  uint64_t clusterCount() const;
  uint64_t freeClusterCount() const;
  uint32_t bytesPerCluster() const;

  // Which filesystem the capacity figures describe. /mnt/us is where a Kindle
  // keeps everything a reader cares about.
  void setVolumePath(const char* path);
};

// SDCardManager names the volume type directly.
using FsVolume = SdFs;
using SdFat32 = SdFs;
using SdFat = SdFs;
using File32 = FsFile;
using ExFile = FsFile;

// Bus configuration types the ESP32 targets construct. They carry no meaning
// here and exist so those constructors still compile.
class SdSpiConfig {
 public:
  template <typename... Args>
  explicit SdSpiConfig(Args&&...) {}
};
class SdioConfig {
 public:
  template <typename... Args>
  explicit SdioConfig(Args&&...) {}
};
