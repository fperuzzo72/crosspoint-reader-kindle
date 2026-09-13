# CrossPoint on a jailbroken Kindle

Port target: **Kindle 8th generation** (KT3, 2016), serial prefix `G000K9`,
firmware 5.15.1.1, i.MX6SL, 6" 800x600 at 167ppi, touch only, no frontlight,
no page-turn buttons.

This is a different kind of port from the M5PaperS3 or Paper Mono ones. Those
move CrossPoint to another ESP32 board. This moves it off the ESP32 entirely,
onto a 32-bit ARM Linux box where the kernel, not the firmware, owns the panel.

## Opening the device

The KT3 on 5.15.1.1 is jailbroken with **WinterBreak2** (`jb.sh` v1.3.7).
`kindlemodding.org`'s wizard picks it from the serial prefix and firmware.

Two things about that process are worth recording because they are not
obvious and the first one is actively misleading:

- **Do not use airplane mode to hold off the OTA.** WinterBreak2 needs Wi-Fi
  (the exploit runs in the Experimental Browser), so airplane mode and the
  jailbreak are mutually exclusive. The actual mechanism is to *fill the
  storage*: with no free space the update cannot download. Fill first, then
  connect.
- The payload chain is worth reading before trusting it. `wb2.zip` is 672
  bytes and contains one file, `winterbreak2/dialoger.html`, which injects a
  `source_command` into `com.lab126.transfer` via `nativeBridge` and pipes
  `https://kindlemodding.org/jb.sh` into `sh`. The third-party host in the
  instructions (`penguins184.xyz`) serves only the page that opens that local
  file; it delivers no code. `jb.sh` itself is 381 readable lines around an xz
  blob, with self-describing sections (`02_stop_ota`, `05_install_mkk_dev_keystore`,
  `06_patch_system`).

**KUAL is obsolete and does not work.** Much of the Kindle hacking material
online still tells you to install it. The current mechanism is:

- **Scriptlets**: a `.sh` file in `documents/` shows up as a book on the home
  screen; opening the book runs the script. This is how CrossPoint will
  eventually be launched, and it means the packaging story is a ten-line shell
  script rather than a KUAL extension with its own XML.
- **KPM**: a package manager driven from the search bar (`;kpm install`).

## Target ABI, as measured

Not inferred. Read from the `fbink` binary the jailbreak installs at
`/mnt/us/libkh/bin/fbink`:

| Property | Value |
| --- | --- |
| Machine | ARM, EABI5 |
| `e_flags` | `0x05000200` -> `EF_ARM_ABI_FLOAT_SOFT` |
| Interpreter | `/lib/ld-linux.so.3` |
| glibc symbol ceiling | `GLIBC_2.4` |
| Kernel | Linux 3.0.35 (Freescale i.MX6 BSP) |

So the toolchain is **soft-float**, `arm-linux-gnueabi`. Every modern ARM
cross-compiler defaults to `gnueabihf`, and a hard-float binary fails at exec
with a "not found" that names the binary rather than the missing interpreter
(`/lib/ld-linux-armhf.so.3`), which sends you hunting for the wrong problem.

One honest caveat: this proves soft-float *runs* here. It does not prove the
native userland is soft-float, because NiLuJe ships `fbink` soft-float on
purpose so one binary covers every Kindle. The system partition is not exposed
over USB, so a native binary cannot be inspected from the host. Soft-float is
the right choice regardless, for the same reason NiLuJe made it: it is the
universal one.

`docker/toolchain.Dockerfile` builds koxtoolchain's `kindlepw2` profile, which
despite the name covers the i.MX6 generation from the Paperwhite 2 onward and
emits `arm-kindlepw2-linux-gnueabi`. It runs in Debian because koxtoolchain is
only tested on Linux hosts.

## Where the port cuts

The important structural finding, and the one that makes this tractable:

**No file outside `lib/hal/` includes `EInkDisplay.h` or `FreeInkDisplay.h`.**

The HAL is a real seam, not a nominal one, and the whole of it is 1438 lines
across nine files:

| File | Lines |
| --- | --- |
| `HalGPIO.cpp` | 301 |
| `HalStorage.cpp` | 251 |
| `HalSystem.cpp` | 208 |
| `HalPowerManager.cpp` | 185 |
| `HalTiltSensor.cpp` | 169 |
| `HalDisplay.cpp` | 163 |
| `HalClock.cpp` | 112 |
| `HalFrontlight.cpp` | 31 |
| `HalMemory.cpp` | 18 |

That reframes the job. It is not "260k lines coupled to the ESP32". It is
re-implementing 1438 lines against Linux, plus an Arduino compatibility shim
for the `String`/`millis()`/`WiFi` surface the rest of the tree uses.

Two worries that turned out not to be worries:

- **Runtime resolution is a non-issue.** There are only 11 uses of
  `DISPLAY_WIDTH`/`DISPLAY_HEIGHT` in the entire app, and the SDK already
  carries `displayWidth` as a runtime member because the X3 is 792px and the
  X4 is 800px. For the Kindle the resolution is fixed per model anyway, so it
  stays compile-time, checked against the kernel at `begin()`.
- **Touch-only is already supported.** `BoardConfig.h` derives
  `FREEINK_CAP_TOUCH` per device and the X4 Pro, Paper Mono and M5PaperS3 are
  already in that set. `MappedInputManager` exposes `hasTouch()`,
  `wasScreenTapped()`, `wasScreenLongPress()` and swipe directions. A
  `FREEINK_DEVICE_KINDLE` with `CAP_TOUCH=1` and `CAP_FRONTLIGHT=0` fits the
  existing pattern.

## Display backend

`lib/hal/kindle/` implements the display half. It bypasses `FreeInkDisplay`
rather than adding a twelfth `PanelDriver`: that stack exists to drive a raw
panel over SPI/i80, and on a Kindle there is no raw panel to drive. The kernel
EPDC owns it and userspace gets an 8bpp grayscale `/dev/fb0` plus `MXCFB_*`
ioctls.

Those ioctls are reached through **FBInk** rather than hand-rolled. FBInk
carries the per-model quirk table for every Kindle ever shipped (rotation,
viewport origin, bpp, which waveforms a given panel honors); rediscovering it
would take weeks with the device in hand, and there is no safe way to guess
wrong. It is also already on the device, installed by the jailbreak.

Waveform mapping, from `HalDisplay::RefreshMode`:

| CrossPoint | FBInk | Notes |
| --- | --- | --- |
| `FULL_REFRESH` | `WFM_GC16`, flashing | 16-level repaint with the black flash that scrubs ghosting |
| `HALF_REFRESH` | `WFM_GL16`, no flash | the "text on white" waveform; what a page turn should use |
| `FAST_REFRESH` | `WFM_DU`, no flash | two-level and quick, for UI changing under a finger |

`FAST_REFRESH` maps to DU rather than A2 on purpose. A2 is faster still, but
leaves enough residue that a reader ends up flashing more often to scrub it,
which is a net loss on a page-turn workload.

FBInk's `fbink_refresh` + `fbink_get_last_marker` + `fbink_wait_for_complete`
map cleanly onto the `displayStart()`/`displayFinish()` split the SDK's
`PanelDriver` already defines, so the async refresh path comes almost free.
Unlike the SPI panels, the EPDC copies the frame out at submission time, so
the caller may reuse its framebuffer as soon as `displayStart()` returns.

## Arduino compatibility

The app tree is written against the Arduino core, and the port keeps it that
way. Rewriting 176 `String` call sites and 153 `millis()` ones to be
platform-neutral would be a large, noisy diff across code this port otherwise
never touches, and would fork the tree from upstream for nothing.

`lib/hal/kindle/ArduinoCompat.h` is scoped by measuring what the tree actually
uses, not by reimplementing the core:

| Symbol | Sites |
| --- | --- |
| `String` | 176 (15 distinct methods) |
| `millis` / `micros` | 162 |
| `yield` | 55 |
| `delay` | 46 |
| `ESP.getFreeHeap` | 62 |
| `ESP.getMaxAllocHeap` | 21 |
| `ESP.restart` | 12 |

FreeRTOS use is confined to six files (`HalMemory`, `HalPowerManager`,
`HalStorage`, `ActivityManager`, `UsbSerialJtagHandoff`) and is handled with
those, not here.

Anything outside the measured set is deliberately absent: a missing symbol is
a compile error that points at the real call site, which is what you want. A
stub that silently does the wrong thing at runtime is not.

`String` lives in its own translation unit (`ArduinoString.cpp`) so its tests
build on a Mac; timing and `ESP` need `<sys/sysinfo.h>` and `/proc`. The tests
target the places Arduino diverges from `std::string` and where the obvious
implementation would compile, read correctly and still be wrong:

- `indexOf` returns `-1`, not `npos`.
- `substring` clamps where `substr` throws, including inverted ranges.
- `remove` tolerates out-of-range indices where `erase` throws.
- `replace` advances past its own output instead of spinning.
- `charAt` past the end is NUL, and `String(nullptr)` is empty.

`ESP.restart()` re-execs the process. On the ESP32 restarting the firmware and
rebooting the device are the same act; here they are not, and rebooting a
Kindle to restart an app would be both wrong and slow.


## State

Done:

- Device jailbroken and verified.
- Target ABI measured.
- Cross-toolchain image defined (`docker/toolchain.Dockerfile`).
- Display backend written (`lib/hal/kindle/`).
- Arduino compatibility shim written (`lib/hal/kindle/ArduinoCompat.h`).
- Host tests green: 5 for the 1bpp -> 8bpp expansion, 11 for String semantics.
- On-device smoke test, its cross-build and its launch scriptlet
  (`tools/kindle/`).
- Toolchain built and the smoke test cross-compiled. The output is
  byte-identical in ABI to the device's own binaries: `e_flags 0x05000200`,
  `/lib/ld-linux.so.3`, `for GNU/Linux 3.0.35`, the same triple of values read
  off the stock `fbink`.
- Binary and scriptlet installed on the device, awaiting one tap.

Two things the first cross-compile turned up, both the target's age showing:

- **`-lrt` is mandatory.** `clock_gettime` was only folded into libc in glibc
  2.17, and this target predates that. Without it the link fails on a symbol
  the header declares perfectly happily. `ArduinoCompat` needs it too, so this
  is a standing requirement of the port.
- **`chmod +x` on `/mnt/us` may do nothing.** It is vfat, which stores no
  permission bits; the mount's `fmask` decides. The scriptlet falls back to
  invoking `/lib/ld-linux.so.3` directly, which works because the kernel is
  then asked to exec the loader rather than the file on the card.

**Nothing in `lib/hal/kindle/` has run on the device yet.** The expansion is
host-tested; everything that touches FBInk or `/dev/fb0` is unexercised. The
first real milestone is a scriptlet that paints one frame and proves the
waveform mapping, which needs someone to tap a book on the home screen.

Next, roughly in order:

1. Run `tools/kindle/smoketest.cpp` on the device. It answers the open
   questions in one tap: real panel geometry and rotation, whether 600x800 is
   what the kernel reports, whether the waveform mapping looks right, and what
   each refresh actually costs in milliseconds.
2. `HalStorage` on POSIX, `HalClock` on `clock_gettime`, `HalSystem` on
   `sysinfo`.
3. `HalGPIO` on evdev, mapping touch into `MappedInputManager`.
4. Replace the `WiFi` surface (177 references) with sockets; the Kindle's own
   Wi-Fi is already up and managed by the system.
5. A build system for the target. `platformio.ini` is ESP32-only, so the
   Kindle build needs its own entry point, most likely CMake reusing the
   existing host-test conventions.
