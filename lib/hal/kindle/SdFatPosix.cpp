// FsFile and SdFs over POSIX. See arduino-shim/SdFat.h for why this exists
// rather than the real SdFat.

#include <fcntl.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "arduino-shim/SdFat.h"

namespace crosspoint_storage {
namespace {

char g_root[256] = "/mnt/us";
// Thread-local so two threads resolving at once cannot overwrite each other's
// answer; the storage lock does not cover this shim.
thread_local char g_resolved[1024];

}  // namespace

const char* root() { return g_root; }

void setRoot(const char* path) {
  if (path != nullptr && *path != '\0') {
    std::snprintf(g_root, sizeof(g_root), "%s", path);
  }
}

const char* resolve(const char* path) {
  if (path == nullptr) {
    return g_root;
  }
  const size_t rootLen = std::strlen(g_root);
  // Already inside the root: openNext builds child paths from an
  // already-resolved parent, so resolving has to be idempotent.
  if (std::strncmp(path, g_root, rootLen) == 0 && (path[rootLen] == '\0' || path[rootLen] == '/')) {
    return path;
  }
  // A relative path is relative to the root too, which is what the app means
  // when it passes a bare filename.
  std::snprintf(g_resolved, sizeof(g_resolved), "%s%s%s", g_root, path[0] == '/' ? "" : "/", path);
  return g_resolved;
}

}  // namespace crosspoint_storage

namespace {

// Recursive mkdir, since SdFat's takes a createParents flag and callers use it.
bool makeDirs(const char* path, const bool parents) {
  if (path == nullptr || *path == '\0') {
    return false;
  }
  if (!parents) {
    return ::mkdir(path, 0755) == 0 || errno == EEXIST;
  }

  char work[512];
  std::snprintf(work, sizeof(work), "%s", path);
  for (char* p = work + 1; *p != '\0'; ++p) {
    if (*p != '/') {
      continue;
    }
    *p = '\0';
    if (::mkdir(work, 0755) != 0 && errno != EEXIST) {
      return false;
    }
    *p = '/';
  }
  return ::mkdir(work, 0755) == 0 || errno == EEXIST;
}

}  // namespace

// ---------------------------------------------------------------- FsFile ---

FsFile::~FsFile() { close(); }

void FsFile::adopt(FsFile&& other) noexcept {
  fd = other.fd;
  dir = other.dir;
  std::memcpy(path, other.path, sizeof(path));
  other.fd = -1;
  other.dir = nullptr;
  other.path[0] = '\0';
}

FsFile::FsFile(FsFile&& other) noexcept { adopt(static_cast<FsFile&&>(other)); }

FsFile& FsFile::operator=(FsFile&& other) noexcept {
  if (this != &other) {
    close();
    adopt(static_cast<FsFile&&>(other));
  }
  return *this;
}

bool FsFile::open(const char* p, const oflag_t flags) {
  close();
  if (p == nullptr) {
    return false;
  }
  const char* resolved = crosspoint_storage::resolve(p);
  std::snprintf(path, sizeof(path), "%s", resolved);

  struct stat st {};
  if (stat(resolved, &st) == 0 && S_ISDIR(st.st_mode)) {
    dir = opendir(resolved);
    return dir != nullptr;
  }

  // O_AT_END is SdFat's own bit, not a POSIX one: strip it before the call and
  // honour it with an explicit seek afterwards.
  const bool atEnd = (flags & O_AT_END) != 0;
  fd = ::open(resolved, flags & ~O_AT_END, 0644);
  if (fd < 0) {
    return false;
  }
  if (atEnd) {
    lseek(fd, 0, SEEK_END);
  }
  return true;
}

bool FsFile::close() {
  bool wasOpen = false;
  if (fd >= 0) {
    ::close(fd);
    fd = -1;
    wasOpen = true;
  }
  if (dir != nullptr) {
    closedir(dir);
    dir = nullptr;
    wasOpen = true;
  }
  path[0] = '\0';
  return wasOpen;
}

int FsFile::read(void* buf, const size_t count) {
  if (fd < 0) {
    return -1;
  }
  return static_cast<int>(::read(fd, buf, count));
}

int FsFile::read() {
  uint8_t c = 0;
  return read(&c, 1) == 1 ? c : -1;
}

int FsFile::peek() {
  const int c = read();
  if (c >= 0) {
    seekCur(-1);
  }
  return c;
}

int FsFile::available() {
  if (fd < 0) {
    return 0;
  }
  const uint64_t here = position();
  const uint64_t end = size();
  return end > here ? static_cast<int>(end - here) : 0;
}

size_t FsFile::write(const uint8_t c) { return write(&c, 1); }

size_t FsFile::write(const uint8_t* buf, const size_t count) {
  if (fd < 0) {
    return 0;
  }
  const ssize_t n = ::write(fd, buf, count);
  return n > 0 ? static_cast<size_t>(n) : 0;
}

size_t FsFile::write(const void* buf, const size_t count) {
  return write(static_cast<const uint8_t*>(buf), count);
}

void FsFile::flush() {
  if (fd >= 0) {
    fsync(fd);
  }
}

bool FsFile::seek(const uint64_t pos) {
  return fd >= 0 && lseek(fd, static_cast<off_t>(pos), SEEK_SET) >= 0;
}

bool FsFile::seekCur(const int64_t delta) {
  return fd >= 0 && lseek(fd, static_cast<off_t>(delta), SEEK_CUR) >= 0;
}

bool FsFile::seekEnd(const int64_t delta) {
  return fd >= 0 && lseek(fd, static_cast<off_t>(delta), SEEK_END) >= 0;
}

uint64_t FsFile::position() {
  if (fd < 0) {
    return 0;
  }
  const off_t p = lseek(fd, 0, SEEK_CUR);
  return p < 0 ? 0 : static_cast<uint64_t>(p);
}

uint64_t FsFile::size() {
  if (fd < 0) {
    return 0;
  }
  struct stat st {};
  return fstat(fd, &st) == 0 ? static_cast<uint64_t>(st.st_size) : 0;
}

bool FsFile::truncate(const uint64_t length) {
  return fd >= 0 && ftruncate(fd, static_cast<off_t>(length)) == 0;
}

bool FsFile::openNext(FsFile* entry, const oflag_t flags) {
  if (dir == nullptr || entry == nullptr) {
    return false;
  }
  const dirent* e = nullptr;
  while ((e = readdir(dir)) != nullptr) {
    // "." and ".." are not entries SdFat's iteration ever yields, and callers
    // would recurse forever on them.
    if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) {
      continue;
    }
    char child[1024];  // path + NAME_MAX, so the compiler can prove no truncation
    std::snprintf(child, sizeof(child), "%s/%s", path, e->d_name);
    if (entry->open(child, flags)) {
      return true;
    }
  }
  return false;
}

size_t FsFile::getName(char* out, const size_t len) const {
  if (out == nullptr || len == 0) {
    return 0;
  }
  // SdFat reports the base name, not the whole path.
  const char* slash = std::strrchr(path, '/');
  const char* base = slash != nullptr ? slash + 1 : path;
  std::snprintf(out, len, "%s", base);
  return std::strlen(out);
}

void FsFile::printName(Print* out) const {
  if (out == nullptr) {
    return;
  }
  char name[256];
  getName(name, sizeof(name));
  out->print(name);
}

// ------------------------------------------------------------------ SdFs ---

bool SdFs::exists(const char* p) const {
  struct stat st {};
  return p != nullptr && stat(crosspoint_storage::resolve(p), &st) == 0;
}

bool SdFs::mkdir(const char* p, const bool createParents) { return makeDirs(crosspoint_storage::resolve(p), createParents); }

bool SdFs::rmdir(const char* p) { return p != nullptr && ::rmdir(crosspoint_storage::resolve(p)) == 0; }

bool SdFs::remove(const char* p) { return p != nullptr && ::unlink(crosspoint_storage::resolve(p)) == 0; }

bool SdFs::rename(const char* from, const char* to) {
  if (from == nullptr || to == nullptr) {
    return false;
  }
  // Resolve the source first and copy it: resolve() hands back one buffer, so
  // the second call would overwrite the first.
  char src[1024];
  std::snprintf(src, sizeof(src), "%s", crosspoint_storage::resolve(from));
  return ::rename(src, crosspoint_storage::resolve(to)) == 0;
}

FsFile SdFs::open(const char* p, const oflag_t flags) {
  FsFile f;
  f.open(p, flags);
  return f;
}

int FsFile::read(uint8_t* buf, const size_t count) { return read(static_cast<void*>(buf), count); }

bool FsFile::rename(const char* newPath) {
  if (newPath == nullptr || path[0] == '\0') {
    return false;
  }
  const char* resolvedNew = crosspoint_storage::resolve(newPath);
  // The data has to be on disk before the name moves: a rename that beats the
  // writeback would leave the new name pointing at a short file.
  flush();
  if (::rename(path, resolvedNew) != 0) {
    return false;
  }
  std::snprintf(path, sizeof(path), "%s", resolvedNew);
  return true;
}

void FsFile::rewindDirectory() {
  if (dir != nullptr) {
    rewinddir(dir);
  }
}

FsFile FsFile::openNextFile(const oflag_t flags) {
  FsFile entry;
  openNext(&entry, flags);
  // An exhausted directory yields a closed file, which is how the caller's
  // `while (file)` loop terminates.
  return entry;
}

// --- capacity ---------------------------------------------------------------

void SdFs::setVolumePath(const char* path) {
  if (path != nullptr) {
    std::snprintf(volumePath, sizeof(volumePath), "%s", path);
  }
}

uint64_t SdFs::clusterCount() const {
  struct statvfs st {};
  if (statvfs(volumePath, &st) != 0) {
    return 0;
  }
  return static_cast<uint64_t>(st.f_blocks);
}

uint64_t SdFs::freeClusterCount() const {
  struct statvfs st {};
  if (statvfs(volumePath, &st) != 0) {
    return 0;
  }
  // f_bavail, not f_bfree: what an unprivileged process can actually use, which
  // is what the user is being shown.
  return static_cast<uint64_t>(st.f_bavail);
}

uint32_t SdFs::bytesPerCluster() const {
  struct statvfs st {};
  if (statvfs(volumePath, &st) != 0) {
    return 0;
  }
  return static_cast<uint32_t>(st.f_frsize != 0 ? st.f_frsize : st.f_bsize);
}
