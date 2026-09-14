// evdev probe for the Kindle touchscreen.
//
// The touch backend has one unknown that decides its whole shape: which evdev
// protocol this panel speaks. Single-touch (ABS_X/ABS_Y gated by BTN_TOUCH)
// and multi-touch protocol B (ABS_MT_SLOT + ABS_MT_TRACKING_ID) need different
// state machines, and the axis ranges and origin decide whether coordinates
// need flipping or swapping against a screen that reports rotation 3.
//
// Guessing that and writing the classifier against the guess is how you get a
// plausible-looking backend that is subtly wrong forever. So: measure first.
//
// Writes /mnt/us/crosspoint-input.log. Run it, then touch the screen as the
// on-screen instructions say.

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

namespace {

constexpr const char* LOG_PATH = "/mnt/us/crosspoint-input.log";
constexpr int CAPTURE_SECONDS = 25;

FILE* out = nullptr;

void sayBlank() {
  if (out != nullptr) {
    std::fputc('\n', out);
    std::fflush(out);
  }
}

void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...) {
  va_list a;
  va_start(a, fmt);
  if (out != nullptr) {
    std::vfprintf(out, fmt, a);
    std::fputc('\n', out);
    std::fflush(out);
  }
  va_end(a);
}

bool hasBit(const unsigned long* bits, const int bit) {
  return (bits[bit / (8 * sizeof(long))] >> (bit % (8 * sizeof(long)))) & 1UL;
}

const char* absName(const int code) {
  switch (code) {
    case ABS_X: return "ABS_X";
    case ABS_Y: return "ABS_Y";
    case ABS_PRESSURE: return "ABS_PRESSURE";
    case ABS_MT_SLOT: return "ABS_MT_SLOT";
    case ABS_MT_POSITION_X: return "ABS_MT_POSITION_X";
    case ABS_MT_POSITION_Y: return "ABS_MT_POSITION_Y";
    case ABS_MT_TRACKING_ID: return "ABS_MT_TRACKING_ID";
    case ABS_MT_TOUCH_MAJOR: return "ABS_MT_TOUCH_MAJOR";
    case ABS_MT_WIDTH_MAJOR: return "ABS_MT_WIDTH_MAJOR";
    case ABS_MT_PRESSURE: return "ABS_MT_PRESSURE";
    default: return nullptr;
  }
}

const char* keyName(const int code) {
  switch (code) {
    case BTN_TOUCH: return "BTN_TOUCH";
    case BTN_TOOL_FINGER: return "BTN_TOOL_FINGER";
    case KEY_POWER: return "KEY_POWER";
    case KEY_HOME: return "KEY_HOME";
    case KEY_PAGEUP: return "KEY_PAGEUP";
    case KEY_PAGEDOWN: return "KEY_PAGEDOWN";
    default: return nullptr;
  }
}

struct Device {
  int fd;
  // Sized for /dev/input/ plus a full NAME_MAX entry: the compiler is right
  // that d_name can be 255 bytes even though these are all "eventN".
  char path[288];
  char name[128];
};

}  // namespace

int main() {
  out = std::fopen(LOG_PATH, "w");
  const time_t now = time(nullptr);
  say("CrossPoint Kindle input probe");
  say("run at: %s", ctime(&now));

  std::vector<Device> devices;

  DIR* dir = opendir("/dev/input");
  if (dir == nullptr) {
    say("FATAL: cannot open /dev/input");
    return 1;
  }

  say("--- devices ---");
  const dirent* e = nullptr;
  while ((e = readdir(dir)) != nullptr) {
    if (std::strncmp(e->d_name, "event", 5) != 0) {
      continue;
    }
    Device d{};
    std::snprintf(d.path, sizeof(d.path), "/dev/input/%s", e->d_name);
    d.fd = open(d.path, O_RDONLY | O_NONBLOCK);
    if (d.fd < 0) {
      say("%s: cannot open (errno %d)", d.path, errno);
      continue;
    }
    if (ioctl(d.fd, EVIOCGNAME(sizeof(d.name)), d.name) < 0) {
      std::snprintf(d.name, sizeof(d.name), "<unnamed>");
    }
    sayBlank();
    say("%s  \"%s\"", d.path, d.name);

    unsigned long evbits[EV_MAX / (8 * sizeof(long)) + 1]{};
    ioctl(d.fd, EVIOCGBIT(0, sizeof(evbits)), evbits);
    say("  types: %s%s%s%s", hasBit(evbits, EV_KEY) ? "EV_KEY " : "", hasBit(evbits, EV_ABS) ? "EV_ABS " : "",
        hasBit(evbits, EV_REL) ? "EV_REL " : "", hasBit(evbits, EV_SYN) ? "EV_SYN" : "");

    if (hasBit(evbits, EV_ABS)) {
      unsigned long absbits[ABS_MAX / (8 * sizeof(long)) + 1]{};
      ioctl(d.fd, EVIOCGBIT(EV_ABS, sizeof(absbits)), absbits);
      for (int code = 0; code <= ABS_MAX; ++code) {
        if (!hasBit(absbits, code)) {
          continue;
        }
        input_absinfo info{};
        ioctl(d.fd, EVIOCGABS(code), &info);
        const char* n = absName(code);
        if (n != nullptr) {
          say("  abs %-22s min=%d max=%d fuzz=%d flat=%d", n, info.minimum, info.maximum, info.fuzz, info.flat);
        } else {
          say("  abs code 0x%02X            min=%d max=%d", code, info.minimum, info.maximum);
        }
      }
    }

    if (hasBit(evbits, EV_KEY)) {
      unsigned long keybits[KEY_MAX / (8 * sizeof(long)) + 1]{};
      ioctl(d.fd, EVIOCGBIT(EV_KEY, sizeof(keybits)), keybits);
      char line[512] = "  keys:";
      for (int code = 0; code <= KEY_MAX; ++code) {
        if (!hasBit(keybits, code)) {
          continue;
        }
        const char* n = keyName(code);
        char frag[48];
        if (n != nullptr) {
          std::snprintf(frag, sizeof(frag), " %s", n);
        } else {
          std::snprintf(frag, sizeof(frag), " 0x%03X", code);
        }
        if (std::strlen(line) + std::strlen(frag) < sizeof(line) - 1) {
          std::strcat(line, frag);
        }
      }
      say("%s", line);
    }

    devices.push_back(d);
  }
  closedir(dir);

  if (devices.empty()) {
    say("FATAL: no input devices readable. Is this running as root?");
    return 1;
  }

  sayBlank();
  say("--- capturing %d seconds of events ---", CAPTURE_SECONDS);
  say("DO THIS, in order, pausing a beat between each:");
  say("  1. one short tap near the TOP-LEFT of the screen");
  say("  2. one short tap near the BOTTOM-RIGHT");
  say("  3. one slow swipe LEFT to RIGHT across the middle");
  say("  4. press and hold one spot for ~2 seconds");
  sayBlank();

  std::vector<pollfd> pfds;
  pfds.reserve(devices.size());
  for (const auto& d : devices) {
    pfds.push_back(pollfd{d.fd, POLLIN, 0});
  }

  const time_t deadline = time(nullptr) + CAPTURE_SECONDS;
  long events = 0;
  while (time(nullptr) < deadline) {
    const int ready = poll(pfds.data(), pfds.size(), 500);
    if (ready <= 0) {
      continue;
    }
    for (size_t i = 0; i < pfds.size(); ++i) {
      if ((pfds[i].revents & POLLIN) == 0) {
        continue;
      }
      input_event ev{};
      while (read(devices[i].fd, &ev, sizeof(ev)) == static_cast<ssize_t>(sizeof(ev))) {
        ++events;
        if (ev.type == EV_SYN) {
          say("[%s] ---- SYN ----", devices[i].path);
        } else if (ev.type == EV_ABS) {
          const char* n = absName(ev.code);
          say("[%s] EV_ABS %-22s %d", devices[i].path, n != nullptr ? n : "?", ev.value);
        } else if (ev.type == EV_KEY) {
          const char* n = keyName(ev.code);
          say("[%s] EV_KEY %-22s %d", devices[i].path, n != nullptr ? n : "?", ev.value);
        }
      }
    }
  }

  sayBlank();
  say("--- captured %ld events ---", events);
  if (events == 0) {
    say("NOTHING CAME THROUGH. Either the screen was not touched during the");
    say("window, or the touch device is not among the ones listed above.");
  }

  for (const auto& d : devices) {
    close(d.fd);
  }
  return 0;
}
