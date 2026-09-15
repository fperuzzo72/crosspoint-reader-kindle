// The evdev half of Kindle touch: opens the panel, reassembles the driver's
// per-SYN axis deltas, and feeds TouchClassifier.

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <cstdio>
#include <cerrno>
#include <cstring>

#include "KindleTouch.h"

namespace crosspoint::kindle {
namespace {

bool hasBit(const unsigned long* bits, const int bit) {
  return (bits[bit / (8 * sizeof(long))] >> (bit % (8 * sizeof(long)))) & 1UL;
}

// The driver timestamps events off CLOCK_REALTIME, which the Kindle adjusts
// when it talks to Amazon's time servers. A jump there would make a held
// finger look like it had been down for hours, or for a negative time. Use our
// own monotonic clock for every duration instead.
uint32_t monotonicMs() {
  timespec ts{};
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return static_cast<uint32_t>(static_cast<uint64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000);
}

// Identify the touchscreen by what it can report rather than by its name.
// This panel calls itself "zforce2", but other Kindles ship other controllers
// and the capability is the thing that actually matters.
bool looksLikeTouchscreen(const int fd) {
  unsigned long evbits[EV_MAX / (8 * sizeof(long)) + 1]{};
  if (ioctl(fd, EVIOCGBIT(0, sizeof(evbits)), evbits) < 0 || !hasBit(evbits, EV_ABS)) {
    return false;
  }
  unsigned long absbits[ABS_MAX / (8 * sizeof(long)) + 1]{};
  if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits) < 0) {
    return false;
  }
  return hasBit(absbits, ABS_MT_POSITION_X) && hasBit(absbits, ABS_MT_POSITION_Y);
}

}  // namespace

KindleTouchDevice::~KindleTouchDevice() { end(); }

bool KindleTouchDevice::begin(const uint16_t panelWidth, const uint16_t panelHeight, const TouchTuning tuning) {
  end();
  openedWidth = panelWidth;
  openedHeight = panelHeight;
  openedTuning = tuning;
  classifier = TouchClassifier(panelWidth, panelHeight, tuning);

  DIR* dir = opendir("/dev/input");
  if (dir == nullptr) {
    std::fprintf(stderr, "[kindle] cannot open /dev/input\n");
    return false;
  }

  const dirent* e = nullptr;
  while ((e = readdir(dir)) != nullptr) {
    if (std::strncmp(e->d_name, "event", 5) != 0) {
      continue;
    }
    char path[288];
    std::snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
    const int candidate = open(path, O_RDONLY | O_NONBLOCK);
    if (candidate < 0) {
      continue;
    }
    if (looksLikeTouchscreen(candidate)) {
      fd = candidate;
      break;
    }
    close(candidate);
  }
  closedir(dir);

  if (fd < 0) {
    std::fprintf(stderr, "[kindle] no input device reports ABS_MT_POSITION_X/Y\n");
    return false;
  }
  return true;
}

void KindleTouchDevice::end() {
  if (fd >= 0) {
    close(fd);
    fd = -1;
  }
  haveContact = false;
  pendingDown = false;
}

bool KindleTouchDevice::reopen() {
  const unsigned long now = static_cast<unsigned long>(monotonicMs());
  if (now < nextReopenAtMs) {
    return false;
  }
  nextReopenAtMs = now + 1000;
  const uint16_t w = openedWidth;
  const uint16_t h = openedHeight;
  const TouchTuning tuning = openedTuning;
  if (!begin(w, h, tuning)) {
    return false;
  }
  std::fprintf(stderr, "[kindle] touch device reopened\n");
  return true;
}

GestureResult KindleTouchDevice::update(const int timeoutMs) {
  if (fd < 0) {
    // Either it never opened or it went away; either way, keep trying. The
    // rate limit inside reopen() is what makes that affordable.
    if (!reopen()) {
      return {};
    }
  }

  pollfd pfd{fd, POLLIN, 0};
  const int ready = poll(&pfd, 1, timeoutMs);
  if (ready < 0 || (pfd.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
    std::fprintf(stderr, "[kindle] touch descriptor went bad (revents 0x%x); reopening\n",
                 static_cast<unsigned>(pfd.revents));
    end();
    reopen();
    return {};
  }
  if (ready > 0 && (pfd.revents & POLLIN) != 0) {
    input_event ev{};
    ssize_t got = 0;
    while ((got = read(fd, &ev, sizeof(ev))) == static_cast<ssize_t>(sizeof(ev))) {
      switch (ev.type) {
        case EV_ABS:
          switch (ev.code) {
            case ABS_MT_POSITION_X:
              pendingX = ev.value;
              break;
            case ABS_MT_POSITION_Y:
              pendingY = ev.value;
              break;
            case ABS_MT_TRACKING_ID:
              // Protocol B: a non-negative id opens a contact, -1 closes it.
              pendingDown = ev.value >= 0;
              haveContact = true;
              break;
            default:
              // Slot 1 exists on this panel but nothing here uses a second
              // finger, so ABS_MT_SLOT is ignored on purpose rather than
              // half-handled. A pinch would need real per-slot state.
              break;
          }
          break;

        case EV_KEY:
          if (ev.code == BTN_TOUCH) {
            pendingDown = ev.value != 0;
            haveContact = true;
          }
          break;

        case EV_SYN:
          // The driver reports only the axes that changed, so the accumulated
          // state is the truth and a SYN is the only place it is complete.
          if (haveContact) {
            const GestureResult g = classifier.onSample(static_cast<uint16_t>(pendingX),
                                                        static_cast<uint16_t>(pendingY), pendingDown, monotonicMs());
            if (g) {
              return g;
            }
          }
          break;

        default:
          break;
      }
    }
    // The drain ends either on EAGAIN, which is the normal "nothing more to
    // read" on a non-blocking descriptor, or on a real error. A short read is
    // not expected from evdev and is treated as a real error too: whatever it
    // means, the stream is no longer framed and this cannot go on parsing it.
    if (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
      std::fprintf(stderr, "[kindle] touch read failed (%s); reopening\n", std::strerror(errno));
      end();
      reopen();
    } else if (got > 0) {
      std::fprintf(stderr, "[kindle] short read of %d bytes from the touch device; reopening\n",
                   static_cast<int>(got));
      end();
      reopen();
    }
  }

  // Always tick, events or not: a stationary finger on this panel is silent,
  // and this is the only thing that can notice a long press before the lift.
  return classifier.tick(monotonicMs());
}

}  // namespace crosspoint::kindle
