#!/usr/bin/env python3
"""Installs the ORIGINAL assets recovered from the 2002 app's resource fork
(extracted by extract_rsrc.py into recovered/resources) over the
reconstructed ones in assets/.

- 'snd ' WAVs  -> assets/sounds/<slug>.wav  (slug of the resource name,
                  matching src/compat/sat_sound.c)
- 'cicn' icons -> assets/sprites/<id>.png   (indexed pixels + 1-bit mask)
- 'PICT's      -> assets/pics/<id>.png      (512-byte header + decoder from
                  build_assets.py, falling back to netpbm's picttoppm)
"""

import os
import re
import shutil
import struct
import subprocess
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from build_assets import decode_pict  # noqa: E402
from PIL import Image  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "recovered", "resources")
SOUNDS = os.path.join(ROOT, "assets", "sounds")
SPRITES = os.path.join(ROOT, "assets", "sprites")
PICS = os.path.join(ROOT, "assets", "pics")


def slugify(name):
    out = []
    pending = False
    for ch in name:
        if ch.isalnum():
            if pending and out:
                out.append("-")
            pending = False
            out.append(ch.lower())
        else:
            pending = True
    return "".join(out)


def install_sounds():
    n = 0
    for f in os.listdir(os.path.join(RES, "snd")):
        m = re.match(r"^(-?\d+)-(.+)\.wav$", f)
        if not m:
            continue
        slug = slugify(m.group(2))
        shutil.copyfile(os.path.join(RES, "snd", f),
                        os.path.join(SOUNDS, slug + ".wav"))
        n += 1
    # the 2002 resource was named "Shield Reload" but the code asks for
    # "ShieldReload"; give the code what it wants
    src = os.path.join(SOUNDS, "shield-reload.wav")
    if os.path.exists(src):
        shutil.copyfile(src, os.path.join(SOUNDS, "shieldreload.wav"))
    print(f"sounds installed: {n}")


def decode_cicn(data):
    rowBytes, top, left, bottom, right = struct.unpack_from(">HHHHH", data, 4)
    pixelSize, = struct.unpack_from(">H", data, 32)
    pixRowBytes = rowBytes & 0x3FFF
    w, h = right - left, bottom - top
    maskRowBytes, mt, ml, mb, mr = struct.unpack_from(">HHHHH", data, 54)
    bmapRowBytes, = struct.unpack_from(">H", data, 68)
    pos = 82
    mask = data[pos:pos + maskRowBytes * (mb - mt)]
    pos += maskRowBytes * (mb - mt)
    pos += bmapRowBytes * (mb - mt)  # skip 1-bit icon image
    # color table
    pos += 4  # seed
    pos += 2  # flags
    ctSize, = struct.unpack_from(">H", data, pos)
    pos += 2
    palette = {}
    for _ in range(ctSize + 1):
        val, r, g, b = struct.unpack_from(">HHHH", data, pos)
        pos += 8
        palette[val] = (r >> 8, g >> 8, b >> 8)
    pixels = data[pos:pos + pixRowBytes * h]

    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    px = img.load()
    for y in range(h):
        for x in range(w):
            bit = mask[y * maskRowBytes + (x >> 3)] >> (7 - (x & 7)) & 1
            if not bit:
                continue
            if pixelSize == 8:
                v = pixels[y * pixRowBytes + x]
            elif pixelSize == 4:
                v = pixels[y * pixRowBytes + (x >> 1)] >> (0 if x & 1 else 4) & 0xF
            elif pixelSize == 2:
                v = pixels[y * pixRowBytes + (x >> 2)] >> (6 - 2 * (x & 3)) & 3
            elif pixelSize == 1:
                v = pixels[y * pixRowBytes + (x >> 3)] >> (7 - (x & 7)) & 1
            else:
                raise ValueError(f"pixelSize {pixelSize}")
            r, g, b = palette.get(v, (255, 0, 255))
            px[x, y] = (r, g, b, 255)
    return img


def install_cicns():
    n, failed = 0, []
    d = os.path.join(RES, "cicn")
    for f in sorted(os.listdir(d)):
        m = re.match(r"^(-?\d+)", f)
        if not m or not f.endswith(".bin"):
            continue
        rid = int(m.group(1))
        try:
            img = decode_cicn(open(os.path.join(d, f), "rb").read())
            img.save(os.path.join(SPRITES, f"{rid}.png"))
            n += 1
        except Exception as e:
            failed.append((rid, str(e)))
    print(f"cicn sprites installed: {n}")
    for rid, e in failed:
        print(f"  FAILED cicn {rid}: {e}")


def key_border_white(img, threshold=240):
    """The menu PICT has a white filler block around the machine that the
    author's mockup shows as transparent; punch out white connected to the
    picture border, keeping interior white highlights."""
    from collections import deque
    img = img.convert("RGBA")
    w, h = img.size
    px = img.load()
    seen = bytearray(w * h)
    q = deque()

    def white(x, y):
        r, g, b, _ = px[x, y]
        return r >= threshold and g >= threshold and b >= threshold

    for x in range(w):
        for y in (0, h - 1):
            if white(x, y) and not seen[y * w + x]:
                seen[y * w + x] = 1
                q.append((x, y))
    for y in range(h):
        for x in (0, w - 1):
            if white(x, y) and not seen[y * w + x]:
                seen[y * w + x] = 1
                q.append((x, y))
    while q:
        x, y = q.popleft()
        r, g, b, _ = px[x, y]
        px[x, y] = (r, g, b, 0)
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < w and 0 <= ny < h and not seen[ny * w + nx] \
                    and white(nx, ny):
                seen[ny * w + nx] = 1
                q.append((nx, ny))
    return img


def install_picts():
    n, failed = 0, []
    d = os.path.join(RES, "PICT")
    for f in sorted(os.listdir(d)):
        m = re.match(r"^(-?\d+)", f)
        if not m or not f.endswith(".bin"):
            continue
        rid = int(m.group(1))
        raw = open(os.path.join(d, f), "rb").read()
        padded = b"\0" * 512 + raw
        tmp = os.path.join(ROOT, "build", f"pict-{rid}.tmp")
        with open(tmp, "wb") as t:
            t.write(padded)
        img = None
        try:
            img = decode_pict(tmp)
        except Exception:
            try:
                png = subprocess.run(
                    f'picttoppm "{tmp}" | pnmtopng', shell=True,
                    capture_output=True, check=True).stdout
                with open(tmp + ".png", "wb") as t:
                    t.write(png)
                img = Image.open(tmp + ".png").convert("RGB")
            except Exception as e:
                failed.append((rid, str(e)[:80]))
        if img:
            if rid == 131:
                img = key_border_white(img)
            img.save(os.path.join(PICS, f"{rid}.png"))
            n += 1
        os.remove(tmp)
    print(f"PICTs installed: {n}")
    for rid, e in failed:
        print(f"  FAILED PICT {rid}: {e}")


if __name__ == "__main__":
    install_sounds()
    install_cicns()
    install_picts()
