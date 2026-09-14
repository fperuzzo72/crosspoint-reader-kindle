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

## Measured on device

Kindle 8th gen (codename **Eanab**, platform Heisenberg, device id 617),
kernel 3.10.53-lab126, armv7l. `tools/kindle/smoketest.cpp`, full-frame
600x800 paints, pixels verified by reading panel memory back at three known
points.

| Waveform | Blocking total | Submit (async) | Waveform alone |
| --- | --- | --- | --- |
| `WFM_GC16`, flashing | 486 ms | 20 ms | ~478 ms |
| `WFM_GL16` | 23 ms | 18 ms | ~5 ms |
| `WFM_DU` | 23 ms | 18 ms | ~5 ms |

Four things follow:

- **A page turn costs ~23 ms.** Good for e-ink, and it leaves real headroom.
- **The 1bpp -> 8bpp expansion is not a bottleneck.** Comparing against an
  earlier run whose blit silently did nothing puts the whole full-screen
  expansion at roughly 3-5 ms. It needs no lookup table and no optimising.
- **The submit path dominates, not the waveform.** 18 ms of a 23 ms paint is
  submission, of which only 3-5 ms is the blit; the remaining ~13 ms is
  FBInk's refresh call. Worth digging into eventually.
- **The async split genuinely defers on all three modes.** For a full refresh
  that is the difference between 20 ms and 498 ms of blocked time.

### Open question: DU earns nothing at full screen

`WFM_GL16` and `WFM_DU` measured identical (23 ms). At full screen DU offers
no speed and gives up 14 grey levels, so mapping `FAST_REFRESH` to it is not
supported by this evidence. DU's classic advantage is on a small partial
rectangle (a menu row changing under a finger), which the smoke test does not
exercise. The mapping stays as it is until a partial-rect measurement decides
it, rather than being changed on full-screen data alone.

### Stride

The panel is 600px wide and the framebuffer's scanline stride is **608
bytes**. Destination rows must step by the stride; stepping by width walks
each row 8px further left than the last and shears the image.


## How much of the SDK already crosses

The app does not only lean on the SDK for the display. It also gets its book
engine, UI toolkit, fonts and themes there, so "can CrossPoint run" is really
a question about the SDK, not about `lib/hal/`.

Measured by actually running the ARM cross-compiler over the sources, not by
grepping for `esp_`:

| SDK lib | Lines | Files touching ESP32 APIs |
| --- | --- | --- |
| FreeInkBook (EPUB engine) | 28752 | 0 of 77 |
| FreeInkUI (UI toolkit) | 15363 | 1 of 51 |
| FreeInkDisplay | 11144 | 23 of 44 (bypassed by this port) |
| InputManager | 3963 | 3 of 5 |
| BoardConfig | 2702 | 5 of 5 |

**14 of the 15 library sources in FreeInkBook and FreeInkUI compile for
`arm-kindlepw2-linux-gnueabi` as they stand.** The single failure is a missing
third-party header (`tjpgd.h`), not a portability problem. The two biggest
pieces of the reader, roughly 44k lines, want a compiler and nothing else.

Getting there needed three shims, all under `lib/hal/kindle/arduino-shim/`,
which goes first on the include path so `#include <Arduino.h>` inside the SDK
resolves without editing the SDK:

- `Arduino.h` forwards to `ArduinoCompat.h` and adds the core's vocabulary
  (`byte`, `PROGMEM`, `pgm_read_*`, `constrain`, the no-op GPIO calls).
- `driver/gpio.h` covers the three ESP-IDF names BoardConfig wants
  (`gpio_num_t`, `gpio_hold_en`, `gpio_hold_dis`), all no-ops. They are not a
  promise that pin latching works; if a Kindle profile ever needs a real GPIO,
  this file should stop being a stub rather than keep returning success.
- `esp_rom_sys.h` maps the ROM printf and busy-wait onto the ordinary ones.
  They exist on the ESP32 because the ordinary ones are not always safe to
  call; that constraint has no meaning in a Linux process.

### The one structural item left

With the shims in place the remaining error is not a portability failure, it
is a question the build has not been asked to answer:

    #error "FreeInk: no device selected. Pass at least one -DFREEINK_DEVICE_<NAME>"

BoardConfig derives every capability from a device macro, and there is no
Kindle among them. Adding `FREEINK_DEVICE_KINDLE` (with `CAP_TOUCH=1`,
`CAP_FRONTLIGHT=0`) is the real remaining SDK work. It lives in the freeink-sdk
submodule rather than this repo, so it needs a fork, the way the M5PaperS3 port
carries its own.

Building with an existing profile (`-DFREEINK_DEVICE_PAPERMONO`) confirms
everything downstream of that decision compiles.


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


## Networking

The division of labour in `arduino-shim/WiFi.h` is the whole design, and it
is different from every other target.

Everywhere else FreeInk *is* the firmware, so `WiFi.begin()` genuinely owns the
radio. A Kindle runs Amazon's stack, which associates, roams, sleeps the radio
and reconnects on its own schedule. A reader process calling `begin()` or
`softAP()` would be fighting the system, and the user would lose their
connection.

So:

| Reading the truth | Taking control |
| --- | --- |
| `localIP()` via `getifaddrs` | `begin()` returns `WL_CONNECT_FAILED` |
| `macAddress()` via `SIOCGIFHWADDR` | `softAP()` returns false |
| `RSSI()` from `/proc/net/wireless` | `disconnect()` returns false |
| `SSID()` via wireless-extensions `SIOCGIWESSID` | `scanNetworks()` returns 0 |
| `status()` from whether an interface is up with an address | |

Failing rather than pretending matters here: a caller told "connecting" would
wait forever for an association nobody requested. The practical consequence for
CrossPoint is that the **join-a-network and hotspot flows do not apply on this
target**, and should be hidden by capability rather than left to fail at the
button.

Everything downstream that just wants a socket works, because TCP and UDP are
entirely real, and so is the HTTP server: routing, query and form arguments,
headers, streamed bodies, CORS.

**Multipart upload is the one deliberate gap.** `WebServer::begin()` refuses to
start if any route registered an upload handler, and says why. Accepting a
browser's POST and silently dropping the book is the worst outcome available;
a failure at start-up is one a person can act on.

## Correctness where wrong is as bad as absent

Two pieces of the shim are implemented and tested against published vectors
rather than stubbed, because their output is used as an identity:

- **MD5.** KOReader's sync names a document by its digest. A stub returning a
  constant would make every book the same document and quietly cross-contaminate
  reading positions between them: data loss, not a missing feature. Checked
  against RFC 1321's full test suite, including the 55/56/64-byte lengths where
  padding either works or does not.
- **base64.** HTTP Basic auth and stored-credential obfuscation. Checked against
  RFC 4648's vectors, plus bytes above 0x7F where a char-signedness slip would
  only show on non-ASCII content.

Round-tripping either against itself would have passed for any self-consistent
nonsense, which is why neither test does that.


## Where the build stands

`build/kindle/census.sh` runs the cross-compiler over every source in `src/`,
`lib/` and the SDK. It is the port's compass: attack whatever cause appears
most often, re-measure, repeat.

| After | Compiling | Dominant remaining cause |
| --- | --- | --- |
| the device profile landed | 25% (58/228) | `Print.h` (59 files) |
| Print, Serial, SPI, Wire, FreeRTOS | 35% (81/229) | SdFat's `FsApiConstants.h` (111 files) |
| SdFat over POSIX, generated I18nKeys | 68% (158/231) | scattered |
| `oflag_t`, ESP stubs, String fix | 73% (169/231) | networking (15 files) |
| sockets, HTTP server, MD5, base64 | 78% (184/234) | third-party headers |
| the last ESP stubs and C++ fixes | **82% (192/234)** | third-party headers |

Two of those steps are worth remembering as method rather than as results. The
jump from 35% to 68% came from a single header: 111 files could not compile
without SdFat, and not one of them cared about SD cards. And the `String`
ambiguity that cost six files was self-inflicted, an implicit
`String(std::string)` this shim had no business offering, since Arduino's
String has no such constructor.

### What is left, by cause

The remaining 18% is almost entirely **unvendored third-party libraries**, not
portability work:

| Missing | Library | Files |
| --- | --- | --- |
| `WebSocketsServer.h` | links2004/WebSockets | 4 |
| `PNGdec.h`, `JPEGDEC.h`, `tjpgd.h` | image decoders | 5 |
| `mbedtls/*`, `esp_crt_bundle.h` | TLS | 3 |
| `qrcode.h` | ricmoo/QRCode | 1 |

All of them are portable C or C++; none of them has been fetched. Turning
`docs/kindle-build.md`'s hand-fetch table into a real dependency step is the
next piece of build work, and it is worth more than any further shimming.

None of what remains is a portability problem. It is plumbing.

### Honest limits of the shims

`SPI.h` and `Wire.h` are deliberately inert. Code that genuinely needs to move
bytes over a bus will appear to succeed and transfer zeros. That is the right
trade here, because nothing on this target has a bus to talk to, but it is a
trap for any future profile that does.

The FreeRTOS shims are the opposite: real pthreads, real mutexes, real
condition-variable queues, because the tree uses them for actual concurrency
and a stub would produce races rather than merely missing hardware. What does
not carry across is priority and core affinity, which Linux does not offer on
the same terms. Work that is merely backgrounded is fine; anything relying on
priority for correctness is not.


## Two C++ traps worth naming

Both were self-inflicted and both compile silently wrong elsewhere, so they are
recorded rather than quietly fixed.

`WebServer` has a `close()` method, which **hid the global `::close()`** inside
its own members: shutting the listening socket called the member with an int.
The fix is qualification, and the lesson is that a method named after a libc
function shadows it for the whole class.

`WiFiClient`, `NetworkUdp` and `FsFile` override `write(uint8_t)` and
`write(const uint8_t*, size_t)`, which **hides the inherited
`Print::write(const char*)`** entirely: `write("literal")` then tries to
resolve against the `uint8_t` overload. `using Print::write;` brings the base
overloads back. `FsFile` had the same latent trap with no call site yet and got
the same fix rather than waiting for it to bite.


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

The display backend **runs on the device** and its output is verified by
reading panel memory back, not assumed. So does touch: `touchtest` classified
9 taps, 2 long presses and 14 swipes in one session, with both long presses
firing at 572 ms (the 550 ms threshold plus one 30 ms poll), which is the
timer doing exactly the job the silent panel will not do for it.

See "Measured on device" above.

One lesson from getting there is worth keeping. The first on-device run
reported OK while every blit silently failed: `fbink_print_raw_data` sits
behind `FBINK_WITH_IMAGE`, a `MINIMAL` build compiles it out, and nothing
checked its return, so the panel refreshed stale contents and the timings
looked plausible. A test that can pass without doing its job is worse than no
test. Hence the read-back probes.

Next, roughly in order:

1. Run `tools/kindle/smoketest.cpp` on the device. It answers the open
   questions in one tap: real panel geometry and rotation, whether 600x800 is
   what the kernel reports, whether the waveform mapping looks right, and what
   each refresh actually costs in milliseconds.
2. Close the remaining 27%, worst cause first. Networking is the big one.
3. Wire `KindleTouchDevice` into `MappedInputManager` so the app sees gestures
   through the interface it already has, rather than through a second path.
4. `HalStorage`, `HalClock`, `HalSystem`, `HalPowerManager` on POSIX. These are
   mechanical now that SdFat presents a filesystem.
5. A build system for the target: `platformio.ini` is ESP32-only, so this needs
   its own entry point, most likely CMake reusing the host-test conventions.
   See `docs/kindle-build.md` for how the pieces are built today.
