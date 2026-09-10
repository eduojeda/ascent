#!/usr/bin/env python3
"""Builds the game's runtime assets from the surviving Development Stuff art.

Inputs:  build/art-png/  (PICTs pre-converted by tools/convert-art.sh)
         Development Stuff/  (PICTs netpbm can't read are decoded here)
Outputs: assets/sprites/<resource id>.png
         assets/pics/<resource id>.png

The resource IDs are the ones the 2002 code loads (see src/ascent.h). Art
whose resource-fork originals were lost (explosions, smoke, sparks, digits,
bullets, banners, countdown text) is regenerated procedurally.
"""

import os
import struct
import subprocess
import sys
from PIL import Image, ImageDraw, ImageFilter, ImageFont, ImageOps

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ART = os.path.join(ROOT, "build", "art-png")
DEV = os.path.join(ROOT, "Development Stuff")
SPRITES = os.path.join(ROOT, "assets", "sprites")
PICS = os.path.join(ROOT, "assets", "pics")
IMPACT = "/System/Library/Fonts/Supplemental/Impact.ttf"

DARK = 14  # pixels darker than this count as black background


# ---------- minimal PICT v2 decoder (DirectBitsRect / PackBitsRect) ----------

def unpackbits(data, expected, word=False):
    """PackBits; word=True repeats/copies 16-bit units (PICT packType 3)."""
    unit = 2 if word else 1
    out = bytearray()
    i = 0
    while len(out) < expected and i < len(data):
        flag = data[i]
        i += 1
        if flag > 128:
            out += data[i:i + unit] * (257 - flag)
            i += unit
        elif flag < 128:
            n = (flag + 1) * unit
            out += data[i:i + n]
            i += n
    return bytes(out), i


def decode_pict(path):
    with open(path, "rb") as f:
        raw = f.read()[512:]
    size, top, left, bottom, right = struct.unpack(">HHHHH", raw[:10])
    pos = 10
    img = None

    def rd(fmt):
        nonlocal pos
        vals = struct.unpack_from(">" + fmt, raw, pos)
        pos += struct.calcsize(">" + fmt)
        return vals if len(vals) > 1 else vals[0]

    while pos < len(raw) - 1:
        if pos & 1:
            pos += 1
        op = rd("H")
        if op == 0xFF:
            break
        elif op in (0x0000, 0x001E, 0x001C, 0x0048):
            pass
        elif op == 0x0011:  # version
            rd("H")
        elif op == 0x0C00:  # header
            pos += 24
        elif op == 0x0001:  # clip region
            n = rd("H")
            pos += n - 2
        elif op == 0x00A1:  # long comment
            rd("H")
            n = rd("H")
            pos += n
        elif op in (0x0003, 0x0004, 0x0005, 0x0008, 0x000D, 0x0016, 0x0023,
                    0x00A0):
            pos += 2
        elif op in (0x0006, 0x0007, 0x000B, 0x000C, 0x000E, 0x000F):
            pos += 4
        elif op in (0x001A, 0x001B, 0x001D, 0x001F):
            pos += 6
        elif op in (0x0098, 0x009A):  # PackBitsRect / DirectBitsRect
            if op == 0x009A:
                rd("I")  # baseAddr
            rowBytes = rd("H")
            packed_rb = rowBytes & 0x7FFF
            b_top, b_left, b_bot, b_right = rd("HHHH")
            rd("HH")  # pmVersion, packType
            rd("I")   # packSize
            rd("II")  # hRes, vRes
            pixelType, pixelSize, cmpCount, cmpSize = rd("HHHH")
            rd("III")  # planeBytes, pmTable, pmReserved
            palette = None
            if op == 0x0098:  # color table follows
                rd("I")
                rd("H")
                ctSize = rd("H")
                palette = []
                for _ in range(ctSize + 1):
                    rd("H")
                    r, g, b = rd("HHH")
                    palette.append((r >> 8, g >> 8, b >> 8))
            rd("HHHH")  # srcRect
            rd("HHHH")  # dstRect
            rd("H")     # mode
            w = b_right - b_left
            h = b_bot - b_top
            img = Image.new("RGB", (w, h))
            px = img.load()
            for y in range(h):
                if packed_rb < 8:
                    row = raw[pos:pos + packed_rb]
                    pos += packed_rb
                else:
                    if packed_rb > 250:
                        cnt = rd("H")
                    else:
                        cnt = rd("B")
                    row, _ = unpackbits(raw[pos:pos + cnt], 1 << 30,
                                        word=(pixelSize == 16))
                    pos += cnt
                if pixelSize == 32:
                    n = len(row) // cmpCount
                    off = n if cmpCount == 4 else 0  # skip alpha plane
                    for x in range(min(w, n)):
                        px[x, y] = (row[off + x], row[off + n + x],
                                    row[off + 2 * n + x])
                elif pixelSize == 16:
                    for x in range(w):
                        v = struct.unpack_from(">H", row, x * 2)[0]
                        px[x, y] = (((v >> 10) & 31) * 255 // 31,
                                    ((v >> 5) & 31) * 255 // 31,
                                    (v & 31) * 255 // 31)
                elif pixelSize == 8 and palette:
                    for x in range(w):
                        px[x, y] = palette[row[x]]
            # rows for 16-bit are PackBits on words, redo if needed
            if pixelSize == 16:
                pass
        else:
            raise ValueError(f"{os.path.basename(path)}: unhandled op {op:#06x} at {pos}")
    if img is None:
        raise ValueError(f"{os.path.basename(path)}: no bitmap op found")
    return img


# ---------- sprite processing ----------

def load(name):
    return Image.open(os.path.join(ART, name)).convert("RGB")


def autocrop(img, threshold=DARK):
    g = img.convert("L").point(lambda v: 255 if v > threshold else 0)
    bbox = g.getbbox()
    return img.crop(bbox) if bbox else img


def make_transparent(img, threshold=DARK):
    """Flood fill from the borders: dark pixels connected to the edge become
    transparent. Dark pixels inside the silhouette stay."""
    img = img.convert("RGBA")
    w, h = img.size
    px = img.load()
    from collections import deque
    seen = bytearray(w * h)
    q = deque()

    def dark(x, y):
        r, g, b, _ = px[x, y]
        return r <= threshold and g <= threshold and b <= threshold

    for x in range(w):
        for y in (0, h - 1):
            if dark(x, y) and not seen[y * w + x]:
                seen[y * w + x] = 1
                q.append((x, y))
    for y in range(h):
        for x in (0, w - 1):
            if dark(x, y) and not seen[y * w + x]:
                seen[y * w + x] = 1
                q.append((x, y))
    while q:
        x, y = q.popleft()
        r, g, b, _ = px[x, y]
        px[x, y] = (r, g, b, 0)
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if 0 <= nx < w and 0 <= ny < h and not seen[ny * w + nx] and dark(nx, ny):
                seen[ny * w + nx] = 1
                q.append((nx, ny))
    return img


def sprite(res_id, src, size=None, fit_width=None, mirror=False):
    """Crop black margins, scale, key out the background, save."""
    img = autocrop(load(src))
    if mirror:
        img = ImageOps.mirror(img)
    if size:
        img = img.resize(size, Image.LANCZOS)
    elif fit_width:
        h = max(1, round(img.height * fit_width / img.width))
        img = img.resize((fit_width, h), Image.LANCZOS)
    img = make_transparent(img)
    img.save(os.path.join(SPRITES, f"{res_id}.png"))


def save_sprite_img(res_id, img):
    img.save(os.path.join(SPRITES, f"{res_id}.png"))


def save_pic(res_id, img):
    img.convert("RGB" if img.mode == "RGB" else "RGBA").save(
        os.path.join(PICS, f"{res_id}.png"))


# ---------- generated art ----------

def text_image(text, target_w, target_h, fill, outline=None):
    font = ImageFont.truetype(IMPACT, 200)
    tmp = Image.new("RGBA", (1600, 500), (0, 0, 0, 0))
    d = ImageDraw.Draw(tmp)
    if outline:
        for dx in (-4, 0, 4):
            for dy in (-4, 0, 4):
                d.text((60 + dx, 60 + dy), text, font=font, fill=outline)
    d.text((60, 60), text, font=font, fill=fill)
    tmp = tmp.crop(tmp.getbbox())
    scale = min(target_w / tmp.width, target_h / tmp.height)
    scaled = tmp.resize((max(1, int(tmp.width * scale)),
                         max(1, int(tmp.height * scale))), Image.LANCZOS)
    out = Image.new("RGBA", (target_w, target_h), (0, 0, 0, 0))
    out.paste(scaled, ((target_w - scaled.width) // 2,
                       (target_h - scaled.height) // 2))
    return out


def fireball(size, phase, big):
    """One explosion frame. phase 0..7: grow then fade."""
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = size / 2
    grow = min(phase / 3.0, 1.0)
    fade = 1.0 if phase < 4 else max(0.0, 1.0 - (phase - 3) / 4.0)
    r_outer = c * (0.35 + 0.65 * grow)
    layers = [
        (r_outer, (200, 40, 10, int(160 * fade))),
        (r_outer * 0.75, (255, 120, 20, int(220 * fade))),
        (r_outer * 0.45, (255, 220, 80, int(255 * fade))),
    ]
    if big:
        layers.append((r_outer * 0.22, (255, 255, 220, int(255 * fade))))
    for r, col in layers:
        d.ellipse((c - r, c - r, c + r, c + r), fill=col)
    return img.filter(ImageFilter.GaussianBlur(size / 14))


def smoke_frame(size, phase):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = size / 2
    r = c * (0.5 + 0.5 * phase / 5.0)
    a = int(130 * (1.0 - phase / 6.0))
    grey = 120 + phase * 12
    d.ellipse((c - r, c - r, c + r, c + r), fill=(grey, grey, grey, a))
    return img.filter(ImageFilter.GaussianBlur(size / 8))


def spark_frame(size, phase):
    img = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = size / 2
    a = int(255 * (1.0 - phase / 6.0))
    r = max(1.0, c * (1.0 - phase / 8.0))
    d.ellipse((c - r, c - r, c + r, c + r), fill=(255, 255, 160, a))
    d.ellipse((c - r / 2, c - r / 2, c + r / 2, c + r / 2),
              fill=(255, 255, 255, a))
    return img


def bullet(color):
    img = Image.new("RGBA", (15, 3), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    r, g, b = color
    d.line((0, 1, 14, 1), fill=(r // 2, g // 2, b // 2, 200))
    d.line((3, 1, 14, 1), fill=(r, g, b, 255))
    img.putpixel((14, 1), (255, 255, 255, 255))
    return img


def digit(n):
    font = ImageFont.truetype(IMPACT, 64)
    tmp = Image.new("RGBA", (100, 100), (0, 0, 0, 0))
    d = ImageDraw.Draw(tmp)
    d.text((10, 10), str(n), font=font, fill=(230, 240, 255, 255))
    tmp = tmp.crop(tmp.getbbox())
    out = Image.new("RGBA", (8, 12), (0, 0, 0, 0))
    scaled = tmp.resize((8, 12), Image.LANCZOS)
    out.paste(scaled, (0, 0))
    return out


def sprite_sources():
    """(resource id, art file, size, fit_width, mirror, group) for every
    sprite that has surviving art. Also read by build_hires_sprites.py, which
    needs to know which render each resource came from; `group` names the
    animation a resource belongs to, so that one can only be rebuilt from the
    renders as a whole."""
    out = [
        (128, "Ball_ball(cyan).png", (10, 10), None, False, "ball"),
        (129, "Targets_Blue_Target.png", (10, 30), None, False, "targetB"),
        (130, "Targets_Red_Target.png", (10, 30), None, False, "targetR"),
        (131, "Generators_Reciever_Render.png", (23, 10), None, False, "recv"),
        (132, "Generators_Reciever_Render(off).png", (23, 10), None, False,
         "recv"),
        (133, "Generators_Generator_Render.png", (20, 29), None, False, "gen"),
        (134, "Generators_GeneratorRender(Dead).png", (20, 29), None, False,
         "gen"),
        (138, "Goodies_Magnet_Render.png", (18, 16), None, False, "magnet"),
        (139, "Goodies_BallShot.png", (22, 8), None, False, "ballshot"),
        (141, "Rocket_RocketLeft.png", (30, 8), None, False, "rocket"),
        (140, "Rocket_RocketLeft.png", (30, 8), None, True, "rocket"),
    ]
    for i in range(5):  # goodie animation
        out.append((160 + i, f"Goodies_{i}.png", (20, 18), None, False,
                    "goodie"))

    # missiles: one surviving render, five animation slots each direction
    for i in range(5):
        out.append((510 + i, "Missile_MissileL0.png", (25, 14), None, False,
                    "missile"))
        out.append((500 + i, "Missile_MissileL0.png", (25, 14), None, True,
                    "missile"))

    # ships: files -3..4 are the eight engine-tilt frames, facing right
    tilt = ["-3", "-2", "-1", "0", "1", "2", "3", "4"]
    for i, t in enumerate(tilt):
        out.append((1000 + i, f"Ships_B_{t}B.png", (45, 28), None, False,
                    "ship"))
        out.append((1100 + i, f"Ships_B_{t}B.png", (45, 28), None, True,
                    "ship"))
        out.append((2000 + i, f"Ships_R_{t}R.png", (45, 28), None, False,
                    "ship"))
        out.append((2100 + i, f"Ships_R_{t}R.png", (45, 28), None, True,
                    "ship"))
    # rotation: angled, head-on, mirrored angled
    out += [
        (1200, "Ships_B_R1B.png", (45, 28), None, False, "ship"),
        (1201, "Ships_B_R2B.png", (45, 28), None, False, "ship"),
        (1202, "Ships_B_R1B.png", (45, 28), None, True, "ship"),
        (2200, "Ships_R_R1R.png", (45, 28), None, False, "ship"),
        (2201, "Ships_R_R2R.png", (45, 28), None, False, "ship"),
        (2202, "Ships_R_R1R.png", (45, 28), None, True, "ship"),
    ]

    # bases: 0..6 then the fully-closed still
    for i in range(7):
        out.append((700 + i, f"Bases_BlueBase{i}.png", (64, 42), None, False,
                    "base"))
        out.append((800 + i, f"Bases_RedBase{i}.png", (64, 42), None, False,
                    "base"))
    out.append((707, "Bases_BlueBase.png", (64, 42), None, False, "base"))
    out.append((807, "Bases_RedBase.png", (64, 42), None, False, "base"))

    for i in range(6):  # ball spawner
        out.append((900 + i, f"Ball_Spawner_BS{i + 1}.png", None, 32, False,
                    "spawner"))

    for i in range(7):  # body debris
        out.append((3000 + i, f"Ships_Debris_body{i}.png", None, 30, False,
                    "debris"))
    for i, src in enumerate([0, 2, 2, 3, 4]):  # engine1 file is corrupt
        out.append((3100 + i, f"Ships_Debris_engine{src}.png", None, 20, False,
                    "debris"))
    return out


def main():
    os.makedirs(SPRITES, exist_ok=True)
    os.makedirs(PICS, exist_ok=True)

    # ---- sprites from surviving art ----
    for res_id, src, size, fit_width, mirror, _group in sprite_sources():
        sprite(res_id, src, size=size, fit_width=fit_width, mirror=mirror)

    # ---- regenerated sprites (originals lost with the resource fork) ----
    for i in range(8):
        save_sprite_img(200 + i, fireball(20, i, big=False))
        save_sprite_img(300 + i, fireball(40, i, big=True))
    for i in range(6):
        save_sprite_img(400 + i, smoke_frame(22, i))
        save_sprite_img(600 + i, spark_frame(6, i))
    save_sprite_img(150, bullet((80, 255, 80)))
    save_sprite_img(151, bullet((255, 70, 50)))
    for n in range(10):
        save_sprite_img(4000 + n, digit(n))
    save_sprite_img(135, text_image("Loser!", 60, 16, (255, 60, 40, 255)))
    save_sprite_img(136, text_image("Winner!", 66, 16, (80, 255, 80, 255)))

    # ---- pictures ----
    save_pic(128, load("Background_Starry_Backround.png"))

    right = decode_pict(os.path.join(DEV, "Scores Panel", "RightPanel"))
    left = decode_pict(os.path.join(DEV, "Scores Panel", "LeftPanel"))
    save_pic(129, right.resize((113, 92), Image.LANCZOS))
    save_pic(130, left.resize((113, 92), Image.LANCZOS))

    menu = load("Interface_Menu.png")
    lit = load("Interface_Menu_Lit_Down.png")
    save_pic(131, menu.crop((73, 0, 566, 391)))
    save_pic(132, lit.crop((115, 164, 420, 211)))
    save_pic(133, lit.crop((309, 237, 566, 297)))
    save_pic(134, lit.crop((98, 288, 270, 334)))
    save_pic(135, lit.crop((426, 321, 565, 388)))

    # title from the Photoshop original, via sips
    title_png = os.path.join(ROOT, "build", "title.png")
    subprocess.run(["sips", "-s", "format", "png",
                    os.path.join(DEV, "Ascent Title"), "--out", title_png],
                   check=True, capture_output=True)
    save_pic(137, Image.open(title_png).convert("RGB"))

    red = (230, 30, 30, 255)
    save_pic(138, text_image("3", 94, 145, red))
    save_pic(139, text_image("2", 92, 146, red))
    save_pic(140, text_image("1", 72, 139, red))
    save_pic(141, text_image("GO!", 203, 140, (80, 255, 80, 255)))
    save_pic(142, text_image("Wins!", 276, 125, (240, 240, 240, 255)))
    save_pic(143, text_image("Left Player", 249, 131, (90, 120, 255, 255)))
    save_pic(144, text_image("Right Player", 214, 131, red))

    print("sprites:", len(os.listdir(SPRITES)), "pics:", len(os.listdir(PICS)))


if __name__ == "__main__":
    sys.exit(main())
