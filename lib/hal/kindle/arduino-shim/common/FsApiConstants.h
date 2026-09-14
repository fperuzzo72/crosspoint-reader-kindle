#pragma once

// SdFat's open-mode constants. The tree includes this path directly, so it
// exists under the same name. They are the POSIX values because that is what
// SdFat chose too, which makes the shim's open() a straight pass-through.

#include <fcntl.h>

// SdFat names the open-flag type. 42 files use the name rather than int, so it
// has to exist even though the values are the POSIX ones.
using oflag_t = int;

#ifndef O_AT_END
// SdFat spells "seek to the end after opening" this way; POSIX has no single
// flag for it, so the shim seeks explicitly when it sees this bit. It must not
// collide with the real O_* values.
#define O_AT_END 0x4000000
#endif

#ifndef O_WRITE
// SdFat's own spellings for the two common modes.
#define O_WRITE O_WRONLY
#define O_READ O_RDONLY
#endif

#ifndef FILE_READ
#define FILE_READ O_RDONLY
#define FILE_WRITE (O_RDWR | O_CREAT | O_AT_END)
#endif
