# Building for the Kindle

Nothing here uses PlatformIO. `platformio.ini` only knows how to talk to
ESP32s, and this target is a Linux process.

## Toolchain

One image, built once, holds the cross compiler:

```sh
docker build -f docker/toolchain.Dockerfile -t crosspoint-kindle-tc:latest .
```

That takes about an hour (crosstool-NG builds binutils, then gcc twice, with
glibc in between). It produces `arm-kindlepw2-linux-gnueabi-gcc`, soft-float,
matching the ABI measured off the device. See `docs/kindle-port.md` for why
soft-float and why a container.

## On-device tools

```sh
docker run --rm -v "$PWD:/src" -w /src crosspoint-kindle-tc:latest \
    sh tools/kindle/build.sh
```

Produces three ARM binaries in `build/kindle/`:

| Binary | What it answers |
| --- | --- |
| `smoketest` | Does the display backend paint, and what does each waveform cost? |
| `touchtest` | Do gestures classify, end to end, with feedback on the panel? |
| `inputprobe` | What does this panel's evdev stream actually look like? |

The first run also clones FBInk (pinned to v1.25.0) and builds `libfbink.a`
statically.

### Installing them

Each tool has a matching scriptlet. Copy the binary to `crosspoint/` on the
Kindle and the `.sh` to `documents/`, where it appears as a book on the home
screen; opening the book runs it.

```sh
cp build/kindle/touchtest /Volumes/Kindle/crosspoint/
cp tools/kindle/crosspoint-touchtest.sh /Volumes/Kindle/documents/
```

Eject before tapping: the Kindle's UI is locked while it is in USB drive mode.
Each tool writes its findings to a log on the card, which is the only way to
get results back from a device nobody is watching.

## Generated sources

`lib/I18n/I18nKeys.h` is generated and gitignored. PlatformIO runs the
generator as a pre-action; a build outside PlatformIO has to run it:

```sh
python3 scripts/gen_i18n.py
```

## Third-party headers

Not yet vendored, fetched into `build/kindle/thirdparty/` by hand:

| Header | Source | Used by |
| --- | --- | --- |
| `ArduinoJson.h` | ArduinoJson v7.4.2 single-header release | settings, OPDS, sync |
| `stb_truetype.h` | nothings/stb | `FreeInkBook` TTF rendering |
| `pngle.h` | kikuchan98/pngle | `FreeInkBook` image decode |
| `linebreak.h` | vendored inside FBInk's libunibreak | `FreeInkBook` chapter layout |

Still missing: `tjpgd.h`, `PNGdec.h`, `JPEGDEC.h`, `MD5Builder.h`, `base64.h`,
`qrcode.h`. Turning this table into a real dependency step is part of the build
system work that has not been done.

## Measuring progress

`build/kindle/census.sh` runs the cross-compiler over every source in `src/`,
`lib/` and the SDK and reports how many compile, grouped by failure cause. It
is how this port decides what to work on next: attack whatever appears most
often. That is what turned Print.h and SdFat into the first two shims written.

```sh
docker run --rm -v "$PWD:/src" -w /src crosspoint-kindle-tc:latest \
    sh build/kindle/census.sh
```
