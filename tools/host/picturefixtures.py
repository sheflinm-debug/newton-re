#!/usr/bin/env python3
"""Make the test pictures for the host's picture decoder and fake camera.

Purpose: hal/host/HostImage.cpp is a PNG and JPEG decoder of our own
(docs/modern/camera-gallery.md); its ctest (hal.HostImage,
src/hal/tests/test_HostImage.cpp) decodes a set of small pictures in every
form the decoder claims to read, and compares each with the pixels Pillow
decodes from the same file - an independent decoder as the oracle.  The
fake camera (hal/host/HostCamera.cpp) answers its pictures from a folder of
test photographs.

Inputs: none (the pictures are drawn here, so nothing is downloaded).
Outputs:
  src/hal/tests/pictures/NAME.png|jpg   the pictures
  src/hal/tests/pictures/NAME.ppm       Pillow's decoding of each, RGB,
                                        transparency laid over white
  src/hal/tests/pictures/progressive.jpg  (no .ppm: the decoder must refuse it)
  src/host/demo/camera/*.jpg            the fake camera's photographs

Invocation (Pillow needed; the outputs are committed, so this is run only
to change them):
  python tools/host/picturefixtures.py [--root <checkout>]
"""

import argparse
import math
import os
import sys

from PIL import Image, ImageDraw


def scene(width, height, seed=0):
    """A picture with gradients, edges and colours to decode."""
    im = Image.new("RGB", (width, height))
    px = im.load()
    for y in range(height):
        for x in range(width):
            r = int(255 * x / max(1, width - 1))
            g = int(255 * y / max(1, height - 1))
            b = int(127 + 127 * math.sin((x + y + seed * 7) / 5.0))
            px[x, y] = (r, g, b)
    d = ImageDraw.Draw(im)
    d.rectangle([width // 6, height // 5, width // 2, height // 2], fill=(250, 250, 250), outline=(0, 0, 0))
    d.ellipse([width // 2, height // 3, width - 4, height - 4], fill=(200, 30, 40))
    d.line([0, height - 1, width - 1, 0], fill=(0, 0, 0), width=2)
    return im


def write_adam7(path, im):
    """An RGB picture written as an interlaced (Adam7) PNG."""
    import struct
    import zlib
    w, h = im.size
    px = im.load()
    passes = [(0, 0, 8, 8), (4, 0, 8, 8), (0, 4, 4, 8), (2, 0, 4, 4), (0, 2, 2, 4), (1, 0, 2, 2), (0, 1, 1, 2)]
    raw = bytearray()
    row_number = 0
    for x0, y0, dx, dy in passes:
        prev = None
        for y in range(y0, h, dy):
            row = bytearray()
            for x in range(x0, w, dx):
                row += bytes(px[x, y])
            if not row:
                continue
            ftype = row_number % 5
            row_number += 1
            out = bytearray()
            for i, v in enumerate(row):
                a = row[i - 3] if i >= 3 else 0
                b = prev[i] if prev else 0
                c = prev[i - 3] if prev and i >= 3 else 0
                if ftype == 0:
                    f = v
                elif ftype == 1:
                    f = v - a
                elif ftype == 2:
                    f = v - b
                elif ftype == 3:
                    f = v - ((a + b) >> 1)
                else:
                    p = a + b - c
                    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                    f = v - (a if pa <= pb and pa <= pc else b if pb <= pc else c)
                out.append(f & 0xFF)
            raw += bytes([ftype]) + out
            prev = row

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 1)))
        f.write(chunk(b"IDAT", zlib.compress(bytes(raw), 9)))
        f.write(chunk(b"IEND", b""))


def over_white(im):
    rgba = im.convert("RGBA")
    white = Image.new("RGBA", rgba.size, (255, 255, 255, 255))
    return Image.alpha_composite(white, rgba).convert("RGB")


def save_reference(path, im):
    over_white(im).save(path.rsplit(".", 1)[0] + ".ppm")


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--root", default=os.path.join(os.path.dirname(__file__), "..", ".."))
    args = ap.parse_args()
    out = os.path.join(args.root, "src", "hal", "tests", "pictures")
    camera = os.path.join(args.root, "src", "host", "demo", "camera")
    os.makedirs(out, exist_ok=True)
    os.makedirs(camera, exist_ok=True)

    base = scene(61, 47)
    made = []

    def png(name, im, **kw):
        path = os.path.join(out, name + ".png")
        im.save(path, **kw)
        save_reference(path, Image.open(path))
        made.append(path)

    def jpg(name, im, reference=True, **kw):
        path = os.path.join(out, name + ".jpg")
        im.save(path, **kw)
        if reference:
            save_reference(path, Image.open(path))
        made.append(path)

    png("rgb8", base)
    alpha = base.convert("RGBA")
    a = Image.new("L", base.size)
    a.putdata([(x * 255) // 60 for y in range(47) for x in range(61)])
    alpha.putalpha(a)
    png("rgba8", alpha)
    png("gray8", base.convert("L"))
    png("gray1", base.convert("1"))
    # sixteen-bit grays, each the eight-bit one times 257 (Pillow's own
    # conversion of an I;16 picture to RGB clips it, so the reference is
    # the eight-bit picture)
    gray = base.convert("L")
    g16 = Image.new("I;16", base.size)
    g16.putdata([v * 257 for v in gray.getdata()])
    path = os.path.join(out, "gray16.png")
    g16.save(path)
    over_white(gray).save(path[:-4] + ".ppm")
    made.append(path)
    png("palette4", base.quantize(colors=16), bits=4)
    pal = base.quantize(colors=200)
    pal.info["transparency"] = 3
    png("palette8trns", pal, transparency=3)
    # (Pillow writes no interlaced PNG: written here, Adam7 by hand, each
    # row with a different filter; Pillow reads it back for the reference)
    path = os.path.join(out, "interlaced.png")
    write_adam7(path, base)
    save_reference(path, Image.open(path))
    made.append(path)

    jpg("q90-420", base, quality=90, subsampling=2)
    jpg("q75-422", base, quality=75, subsampling=1)
    jpg("q95-444", base, quality=95, subsampling=0)
    jpg("gray", base.convert("L"), quality=85)
    try:
        jpg("restart", base, quality=85, subsampling=2, restart_marker_blocks=2)
    except TypeError:
        print("picturefixtures: this Pillow writes no restart markers; restart.jpg not made", file=sys.stderr)
    jpg("progressive", base, reference=False, quality=85, progressive=True)

    for i, (w, h) in enumerate([(640, 480), (480, 640), (640, 480)]):
        path = os.path.join(camera, "test-%d.jpg" % (i + 1))
        scene(w, h, seed=i * 5).save(path, quality=85, subsampling=2)
        made.append(path)

    for p in made:
        print("picturefixtures:", os.path.relpath(p, args.root), os.path.getsize(p), "bytes")


if __name__ == "__main__":
    main()
