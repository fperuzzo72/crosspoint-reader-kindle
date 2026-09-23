# CrossPoint Reader on a jailbroken Kindle

A port of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
to run as an ordinary Linux program on a Kindle, instead of as firmware on a
microcontroller.

CrossPoint is e-reader firmware written for ESP32-based devices, where it owns
the whole machine. A Kindle is not that. It is an ARM Linux box with its own
kernel, its own userspace and its own UI already running, and the panel is
driven by an in-kernel EPDC rather than by a SPI controller the application
talks to. So this port does not replace the Kindle's software. It runs
alongside it, borrows the screen while it is open, and hands it back on exit.

The reader itself is unchanged. What was written here is the layer underneath:
a display backend over `/dev/fb0`, a touch backend over evdev, and enough of an
Arduino and FreeRTOS surface for a tree written against a microcontroller to
compile and run against glibc. See [docs/kindle-port.md](docs/kindle-port.md)
for where the port cuts and why.

## Requirements

**A Kindle that is already jailbroken.** This project does not jailbreak your
device, and deliberately does not explain how. That is between you and your
device, and there are communities that do it properly. Nothing here will work
on a stock Kindle, because a stock Kindle will not run an unsigned binary.

Beyond that the footprint is small: one binary and one shell script on the
user partition. No firmware is flashed, no system file is modified, and the
Kindle's own software is left exactly as it was.

## Tested hardware

Only one device, and it is worth being precise about which:

| | |
| --- | --- |
| Model | Kindle 8th generation (KT3, codename Eanab) |
| Firmware | 5.15.1.1 |
| SoC | i.MX6SL, armv7l |
| Kernel | 3.10.53-lab126 |
| Panel | 600x800, 167 ppi, no frontlight |
| Input | infrared touch (`zforce2`), no buttons other than power |

Nothing else has been tried. Another Kindle would need at minimum its panel
geometry and its touch device checked, and the ABI re-measured if its firmware
is from a different era. The display backend already reaches the panel through
FBInk, which carries a per-model quirk table, so the distance may be short.
That is a guess, though, and it is the kind of guess this port has tried to
avoid making in writing.

## What works

All of this has been watched working on the device, not merely built.

- Reading EPUBs, with the file browser rooted at `/mnt/us`.
- Touch: taps, long presses and swipes, classified from the raw evdev stream.
- Portrait UI on a panel whose native axis is portrait, which is a quarter turn
  away from what the renderer assumes.
- Grayscale text and grayscale images, composed from the renderer's two 1bpp
  planes into the 8bpp frame the EPDC wants.
- Extra fonts, installed to `/.sleep`'s neighbour `/.fonts` rather than
  `/fonts`, which belongs to the Kindle.
- Battery, read from powerd rather than from a gauge on a bus, since the PMIC
  belongs to the kernel here.
- Sleeping and waking with the power button: the page survives the round trip.
  The reader repaints whenever it finds that something else has painted over
  the framebuffer it shares with the Kindle's UI, which covers both the blank
  on the way in and the one on the way out.
- File transfer over Wi-Fi: the file list loads, an upload lands on the card,
  and a delete removes it. The Kindle has to be on Wi-Fi through its own
  settings first, since this process cannot join a network (see below); the
  network-selection screen recognises that and steps aside rather than opening
  a list it cannot fill. On a device kept in airplane mode to stop the firmware
  downloader, turning the radio back on starts that downloader too. It cannot
  install anything, since the jailbreak renamed the installers, but it will
  leave a partial `update.bin.tmp` that is simply deleted.
- Leaving, from the end of the home menu or from Settings > System.

## What does not

Listed because a port that hides its edges wastes the next person's afternoon.

- **HTTPS is refused, never downgraded to HTTP.** TLS is unimplemented. The
  tree sends preemptive HTTP Basic credentials for OPDS and KOReader sync, so
  a silent retry in the clear would put a password on the wire. In practice
  this rules out most real OPDS catalogues and sync servers.
- **Wi-Fi join and hotspot do not work.** The system owns the radio on this
  device. CrossPoint can use a connection the Kindle has already made; it
  cannot make one.
- **Firmware update over the air does not apply** to this target and fails
  every call on purpose.
- **The sleep screen is drawn and then covered.** CrossPoint's inactivity
  timer runs and renders its sleep screen, cover and all, and then asks powerd
  to suspend — at which point the Kindle draws its own screensaver on top,
  because the framework owns that part of going to sleep and does not know this
  process exists. The feature is wired up and effectively invisible. See
  docs/kindle-port.md for what was measured and the one avenue left.

## Installing

Build first (below), then, with the Kindle mounted as USB storage:

```sh
# The link leaves symbols in: 4.2 MB unstripped, 3.2 MB without. Both run.
docker run --rm -v "$PWD:/src" -w /src crosspoint-kindle-tc:latest \
    arm-kindlepw2-linux-gnueabi-strip build/kindle/link/crosspoint

cp build/kindle/link/crosspoint /Volumes/Kindle/crosspoint/crosspoint
cp tools/kindle/run.sh          /Volumes/Kindle/crosspoint/run.sh
cp tools/kindle/crosspoint.sh   /Volumes/Kindle/documents/crosspoint.sh
```

Two scripts, and the split is not decoration. `crosspoint.sh` is the scriptlet
the Kindle launches; it is tiny, it hands off to a copy of `run.sh` in RAM, and
it is meant never to change. Everything that does change lives in `run.sh`,
which is executed from `/tmp` so that replacing it during a session cannot
corrupt the session. The binary is copied to RAM for the same reason.

Books go in `/mnt/us/ebooks`, which is `ebooks/` on the mounted volume. Sleep
wallpapers go in `/.sleep`, and only BMP is read there; `tools/kindle/
make-sleep-images.py` converts a folder of them to the panel's 600x800 at 4 bits
per pixel, which is one sixth the size of 24-bit at no visible cost.
Eject, and `crosspoint.sh` appears on the home screen as if it were a book.
Opening it runs the reader. Everything the run prints lands in
`/mnt/us/crosspoint-run.log`, which is the only window into a session once the
device is unplugged.

On exit the launcher writes a line to the panel using the `fbink` a jailbreak
usually leaves at `/mnt/us/libkh/bin/fbink`. That is the only thing outside
this repo it touches, it is optional, and the reader itself does not use it:
FBInk is linked into the binary statically.

## Building

No PlatformIO. `platformio.ini` only knows how to talk to ESP32s, and this
target is a Linux process.

```sh
docker build -f docker/toolchain.Dockerfile -t crosspoint-kindle-tc:latest .
```

That takes about an hour, because crosstool-NG builds binutils, then gcc twice
with glibc in between. It produces a soft-float `arm-kindlepw2-linux-gnueabi`
toolchain matching the ABI measured off the device, down to
`e_flags 0x05000200` and a GLIBC_2.4 ceiling.

Then:

```sh
docker run --rm -v "$PWD:/src" -w /src crosspoint-kindle-tc:latest \
    sh tools/kindle/trylink.sh
```

Parallel and incremental: about a minute cold, seconds after a one-file change.
It reports undefined references grouped by frequency, which during bring-up was
the honest distance to a running binary. It should say zero.

`tools/kindle/build.sh` builds the standalone on-device probes instead
(display, touch, evdev). [docs/kindle-build.md](docs/kindle-build.md) has the
details.

## Relationship to upstream

This repository carries the full history of CrossPoint Reader with the port on
top, on the `kindle-port` branch. The port is confined to `lib/hal/kindle/`,
the Kindle variants of the HAL, and `tools/kindle/`, so upstream changes
generally merge without touching it.

CrossPoint Reader is by Dave Allie and its contributors, MIT licensed, and this
repository keeps that license and that notice. If you want CrossPoint on
hardware it was actually designed for, go to
[crosspointreader.com](https://crosspointreader.com); the devices there will
give you a better experience than a 2016 Kindle running a port.
