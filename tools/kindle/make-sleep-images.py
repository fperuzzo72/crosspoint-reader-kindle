#!/usr/bin/env python3
"""Convert sleep wallpapers for the Kindle's panel.

The wallpapers that ship for the Xteink panels are 480x800 24-bit colour. This
target is 600x800 and reads grayscale, so they arrive both too narrow and far
larger than they need to be.

Two decisions worth stating, because neither is the obvious one:

- The image is never stretched to fill the width. 480x800 already matches the
  panel's height exactly, so it is placed at its native size and the 60px
  gutters are filled with white.

  The first version sampled the median colour of the image's own edge, on the
  theory that a dark poster framed in white would read as a mistake. On the
  panel it read worse: e-ink renders a near-black gutter as a solid band of
  ink, and any mismatch between the sampled colour and the image's actual edge
  shows up as a visible seam that white never has. Judged on the glass rather
  than on a monitor, plain white won.

- 4 bits per pixel, a fixed uniform 16-level gray palette, which is one sixth
  the size of 24-bit at no visible cost: 28.8 MB becomes 4.8 MB across twenty
  wallpapers. The palette is fixed rather than adaptive because CrossPoint
  reads the palette and takes luminance from it, so adapting would save nothing
  and make the file depend on its own contents.

Note that 16 levels is what the PANEL does, not what the reader draws: the BMP
path quantises to 4 levels with Atkinson dithering on every target. The extra
levels feed that dither rather than reaching the panel, which is still worth
having, and is why the step from 24-bit to 4-bit is free while a step down to
2-bit would not be.

PIL cannot write 4bpp BMPs, so the container is written here. Round-tripping it
through an independent decoder and diffing against the intended pixels is not
optional: a swapped nibble order produces a file that decodes as perfectly
valid and looks like noise.

    python3 tools/kindle/make-sleep-images.py <source-dir> <output-dir>
"""
import os
import struct
import sys

from PIL import Image

W, H, LEVELS = 600, 800, 16


def gray_palette_image():
    img = Image.new("P", (1, 1))
    pal = []
    for i in range(LEVELS):
        v = round(i * 255 / (LEVELS - 1))
        pal += [v, v, v]
    img.putpalette(pal + [0, 0, 0] * (256 - LEVELS))
    return img


def write_bmp4(path, indices, w, h, levels=LEVELS):
    row_bytes = (w + 1) // 2
    stride = (row_bytes + 3) & ~3  # BMP rows are 4-byte aligned
    offset = 14 + 40 + levels * 4
    size_img = stride * h
    out = bytearray()
    out += b"BM" + struct.pack("<IHHI", offset + size_img, 0, 0, offset)
    # Positive height: rows bottom-up, the classic arrangement.
    out += struct.pack("<IiiHHIIiiII", 40, w, h, 1, 4, 0, size_img, 2835, 2835, levels, levels)
    for i in range(levels):
        v = round(i * 255 / (levels - 1))
        out += bytes((v, v, v, 0))  # BGRA
    for y in range(h - 1, -1, -1):
        row = bytearray(stride)
        base = y * w
        for x in range(0, w, 2):
            hi = indices[base + x] & 0x0F
            lo = indices[base + x + 1] & 0x0F if x + 1 < w else 0
            row[x >> 1] = (hi << 4) | lo  # high nibble is the LEFT pixel
        out += row
    with open(path, "wb") as fh:
        fh.write(out)


def convert(src_dir, dst_dir):
    os.makedirs(dst_dir, exist_ok=True)
    palette = gray_palette_image()
    total = count = 0
    for name in sorted(os.listdir(src_dir)):
        if name.rsplit(".", 1)[-1].lower() not in ("bmp", "png", "jpg", "jpeg"):
            continue
        im = Image.open(os.path.join(src_dir, name)).convert("RGB")
        scale = min(W / im.width, H / im.height)
        nw, nh = round(im.width * scale), round(im.height * scale)
        if (nw, nh) != (im.width, im.height):
            im = im.resize((nw, nh), Image.LANCZOS)
        canvas = Image.new("RGB", (W, H), (255, 255, 255))
        canvas.paste(im, ((W - nw) // 2, (H - nh) // 2))
        quantised = canvas.quantize(palette=palette, dither=Image.FLOYDSTEINBERG)
        out = os.path.join(dst_dir, name.rsplit(".", 1)[0] + ".bmp")
        write_bmp4(out, quantised.tobytes(), W, H)

        # Verify rather than trust: decode what was written and diff it against
        # what was meant. This is the check that catches a swapped nibble.
        back = Image.open(out).convert("L")
        if list(back.getdata()) != list(quantised.convert("L").getdata()):
            raise SystemExit("round-trip mismatch: %s" % out)

        total += os.path.getsize(out)
        count += 1
        print("  %-46s %dx%d" % (name[:46], W, H))
    print("\n%d images, %.1f MB, all verified by round-trip" % (count, total / 1e6))


if __name__ == "__main__":
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    convert(sys.argv[1], sys.argv[2])
