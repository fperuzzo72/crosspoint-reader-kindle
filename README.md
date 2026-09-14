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

- Reading EPUBs, with the file browser rooted at `/mnt/us`.
- Touch: taps, long presses and swipes, classified from the raw evdev stream.
- Portrait UI on a panel whose native axis is portrait, which is a quarter turn
  away from what the renderer assumes.
- Grayscale text antialiasing and grayscale images, composed from the
  renderer's two 1bpp planes into the 8bpp frame the EPDC wants.
- Leaving: Settings > System > Exit CrossPoint hands the screen back.
- Suspend and resume: the power button suspends the machine out from under the
  process, and it repaints when it comes back rather than leaving a stale panel.
- Extra fonts from the CrossPoint catalogue, in `/.fonts` rather than `/fonts`,
  which belongs to the Kindle.

## What does not

Listed because a port that hides its edges wastes the next person's afternoon.

- **HTTPS is refused, never downgraded to HTTP.** TLS is unimplemented. The
  tree sends preemptive HTTP Basic credentials for OPDS and KOReader sync, so
  a silent retry in the clear would put a password on the wire. In practice
  this rules out most real OPDS catalogues and sync servers.
- **The built-in web server does not start.** Multipart upload is
  unimplemented, one route registers an upload handler, and `begin()` refuses
  rather than accepting a POST it would drop halfway through a file.
- **Wi-Fi join and hotspot do not work.** The system owns the radio on this
  device. CrossPoint can use a connection the Kindle has already made; it
  cannot make one.
- **Firmware update over the air does not apply** to this target and fails
  every call on purpose.
- **CrossPoint's own sleep timer is disabled here.** Sleeping is the system's
  job on this device: the Kindle suspends and wakes on its own schedule, and
  the reader repaints when it comes back. The app-level timer ended in a call
  that cannot be made from a Linux process, so it is not taken.

## Installing

Build first (below), then, with the Kindle mounted as USB storage:

```sh
# The link leaves symbols in: 4.2 MB unstripped, 3.2 MB without. Both run.
docker run --rm -v "$PWD:/src" -w /src crosspoint-kindle-tc:latest \
    arm-kindlepw2-linux-gnueabi-strip build/kindle/link/crosspoint

cp build/kindle/link/crosspoint /Volumes/Kindle/crosspoint/crosspoint
cp tools/kindle/crosspoint.sh   /Volumes/Kindle/documents/crosspoint.sh
```

Books go in `/mnt/us/ebooks`, which is `ebooks/` on the mounted volume.
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
