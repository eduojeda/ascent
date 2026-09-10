#!/usr/bin/env python3
"""Builds assets/Ascent.icns from the ORIGINAL 2002 app icon family
(recovered/resources: ICN#/icl8 32x32 and ics#/ics8 16x16, all id 128).

The 8-bit members index the classic Mac OS system palette; the 1-bit
members carry the transparency masks. Larger sizes are nearest-neighbor
upscales, keeping the chunky original look in the Dock.
"""

import os
import shutil
import subprocess
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
RES = os.path.join(ROOT, "recovered", "resources")


def mac_system_palette():
    # 0-214: the 6x6x6 color cube (white first) minus black; then 10-step
    # red, green, blue and gray ramps; 255 is black
    vals = [0xFF, 0xCC, 0x99, 0x66, 0x33, 0x00]
    p = [(r, g, b) for r in vals for g in vals for b in vals][:215]
    ramp = [0xEE, 0xDD, 0xBB, 0xAA, 0x88, 0x77, 0x55, 0x44, 0x22, 0x11]
    p += [(v, 0, 0) for v in ramp]
    p += [(0, v, 0) for v in ramp]
    p += [(0, 0, v) for v in ramp]
    p += [(v, v, v) for v in ramp]
    p.append((0, 0, 0))
    return p


PALETTE = mac_system_palette()


def read_res(rtype, rid=128):
    with open(os.path.join(RES, rtype, f"{rid}.bin"), "rb") as f:
        return f.read()


def decode(size, pixels8, iconmask):
    # iconmask (ICN#/ics#) is a 1-bit icon then a 1-bit mask, size/8 bytes
    # per row each; the mask is the second half
    rowbytes = size // 8
    mask = iconmask[size * rowbytes:]
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    px = img.load()
    for y in range(size):
        for x in range(size):
            if not mask[y * rowbytes + (x >> 3)] >> (7 - (x & 7)) & 1:
                continue
            r, g, b = PALETTE[pixels8[y * size + x]]
            px[x, y] = (r, g, b, 255)
    return img


def main():
    icon32 = decode(32, read_res("icl8"), read_res("ICN_"))
    icon16 = decode(16, read_res("ics8"), read_res("ics_"))

    iconset = os.path.join(ROOT, "build", "Ascent.iconset")
    shutil.rmtree(iconset, ignore_errors=True)
    os.makedirs(iconset)
    icon16.save(os.path.join(iconset, "icon_16x16.png"))
    icon32.save(os.path.join(iconset, "icon_16x16@2x.png"))
    icon32.save(os.path.join(iconset, "icon_32x32.png"))
    for name, s in [("icon_32x32@2x.png", 64), ("icon_128x128.png", 128),
                    ("icon_128x128@2x.png", 256), ("icon_256x256.png", 256),
                    ("icon_256x256@2x.png", 512), ("icon_512x512.png", 512)]:
        icon32.resize((s, s), Image.NEAREST).save(os.path.join(iconset, name))

    out = os.path.join(ROOT, "assets", "Ascent.icns")
    subprocess.run(["iconutil", "-c", "icns", "-o", out, iconset], check=True)
    print("wrote", out)


if __name__ == "__main__":
    sys.exit(main())
