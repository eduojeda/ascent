#!/usr/bin/env python3
"""Builds higher-resolution copies of the sprites into assets/sprites/hires/.

The 2002 sprites are 3D renders that were shrunk to 45x34 and friends to fit
Classic Mac. The renders survive in Development Stuff/ at around five times
that, so on a modern display the engine can reduce one of those to the size
it needs instead of enlarging the small one.

Each render is fitted onto the silhouette of the shipped sprite — same
bounding box, same place in the frame — so a sprite does not shift against
the collision rectangle the game computes from its position. Two things are
then checked before a render is accepted:

  * which way round it goes, measured rather than assumed. The mirror flags
    in build_assets.py describe sprites it reconstructed itself; the sprites
    actually shipped come from the recovered resource fork and face the other
    way in about forty cases.
  * whether it is the same object at all. One render stands in for five
    missile frames, and the receiver render does not match its sprite; those
    keep their 2002 art rather than being replaced by something that only
    roughly fits.

The check runs per animation, so a group is either wholly rebuilt from the
renders or wholly left alone — half an animation in each style would flicker.

Inputs:  build/art-png/       (PICTs pre-converted by tools/convert-art.sh)
         assets/sprites/      (the shipped sprites: the framing to match)
Outputs: assets/sprites/hires/<resource id>.png
"""

import os
import sys
from collections import defaultdict
from PIL import Image, ImageOps

import build_assets as B

HIRES = os.path.join(B.SPRITES, "hires")

MAX_SCALE = 4.0    # 400% zoom, the most the engine allows
MIN_SCALE = 1.5    # below this the render adds too little to be worth shipping
MIN_MATCH = 0.70   # silhouette agreement needed to accept a render at all


def silhouette_box(img):
    """Where the drawn part of a sprite sits inside its frame."""
    box = img.split()[3].getbbox() if img.mode == "RGBA" else None
    return box or (0, 0, img.width, img.height)


def mask(img):
    a = img.split()[3].point(lambda v: 255 if v > 96 else 0)
    return a.load(), img.width, img.height


def agreement(cur, cand):
    """Fraction of the two silhouettes that coincide (intersection/union)."""
    a, w, h = mask(cur)
    b, _, _ = mask(cand)
    inter = union = 0
    for y in range(h):
        for x in range(w):
            pa, pb = a[x, y] > 0, b[x, y] > 0
            if pa or pb:
                union += 1
                inter += pa and pb
    return inter / union if union else 0.0


def fit(ren, cur, scale):
    """The render, sized and placed to sit on the sprite's silhouette."""
    box = silhouette_box(cur)
    bw, bh = box[2] - box[0], box[3] - box[1]
    art = B.make_transparent(ren.resize((max(1, round(bw * scale)),
                                         max(1, round(bh * scale))),
                                        Image.LANCZOS))
    out = Image.new("RGBA", (max(1, round(cur.width * scale)),
                             max(1, round(cur.height * scale))), (0, 0, 0, 0))
    out.paste(art, (round(box[0] * scale), round(box[1] * scale)))
    return out


def prepare(res_id, src):
    """Decide orientation and scale for one resource; None if unusable."""
    path = os.path.join(B.SPRITES, f"{res_id}.png")
    if not os.path.exists(path):
        return None
    cur = Image.open(path).convert("RGBA")
    box = silhouette_box(cur)
    bw, bh = box[2] - box[0], box[3] - box[1]
    try:
        ren = B.autocrop(B.load(src))   # autocrop trims to the silhouette
    except FileNotFoundError:
        return None

    scale = min(MAX_SCALE, ren.width / bw, ren.height / bh)
    if scale < MIN_SCALE:
        return None

    best = None
    for flipped in (False, True):
        cand = ImageOps.mirror(ren) if flipped else ren
        # judged at the sprite's own size, which is what the framing must match
        score = agreement(cur, fit(cand, cur, 1.0).resize(cur.size, Image.BOX))
        if best is None or score > best[0]:
            best = (score, flipped)
    return { "cur": cur, "ren": ren, "scale": scale,
             "score": best[0], "mirror": best[1] }


def main():
    os.makedirs(HIRES, exist_ok=True)
    for stale in os.listdir(HIRES):
        os.remove(os.path.join(HIRES, stale))

    plans, groups = {}, defaultdict(list)
    for res_id, src, _size, _fit, _mirror, group in B.sprite_sources():
        p = prepare(res_id, src)
        if p:
            plans[res_id] = p
            groups[group].append(res_id)
        else:
            groups[group].append(None)

    made = 0
    for group, ids in sorted(groups.items()):
        usable = [i for i in ids if i is not None]
        worst = min((plans[i]["score"] for i in usable), default=0.0)
        if len(usable) != len(ids) or worst < MIN_MATCH:
            print(f"  {group:9s} keeps its 2002 art "
                  f"(silhouettes agree only {worst:.0%})")
            continue
        for res_id in usable:
            p = plans[res_id]
            ren = ImageOps.mirror(p["ren"]) if p["mirror"] else p["ren"]
            fit(ren, p["cur"], p["scale"]).save(
                os.path.join(HIRES, f"{res_id}.png"))
            made += 1
        print(f"  {group:9s} {len(usable):3d} sprites, "
              f"agreement {worst:.0%}+, up to {plans[usable[0]]['scale']:.1f}x")
    print(f"hires sprites: {made} built")
    return 0


if __name__ == "__main__":
    sys.exit(main())
