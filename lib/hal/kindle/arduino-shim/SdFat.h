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

#include <cstdint>
#include <cstdio>

#include "Stream.h"
#include "common/FsApiConstants.h"

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
  void close();
  bool isOpen() const { return fd >= 0 || dir != nullptr; }
  explicit operator bool() const { return isOpen(); }

  int read(void* buf, size_t count);
  int read() override;
  int peek() override;
  int available() override;
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
  bool truncate(uint64_t length);
  void rewind() { seek(0); }

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

  // Capacity, in the units SdFat reports them.
  uint64_t clusterCount() const { return 0; }
  uint32_t bytesPerCluster() const { return 0; }
};

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
