#!/usr/bin/env python3
"""Render extracted bitmap resources into a labelled contact sheet.

    scripts/sprite_sheet.py <bitmap-dir> <out.png> [scale]

Each tile is the bitmap scaled up (nearest-neighbour) with its resource ID
underneath. Files are expected to be named like wrestool's *_2_<id>.bmp.
"""
import re
import sys
from pathlib import Path

from PIL import Image, ImageDraw

src, out = Path(sys.argv[1]), Path(sys.argv[2])
scale = int(sys.argv[3]) if len(sys.argv) > 3 else 3


def res_id(p):
    return int(re.search(r"_(\d+)\.bmp$", p.name).group(1))


files = sorted(src.glob("*.bmp"), key=res_id)
imgs = [(res_id(p), Image.open(p).convert("RGB")) for p in files]

cell_w = max(i.width for _, i in imgs) * scale + 8
cell_h = max(i.height for _, i in imgs) * scale + 20
cols = 12
rows = (len(imgs) + cols - 1) // cols
sheet = Image.new("RGB", (cols * cell_w, rows * cell_h), (200, 200, 200))
draw = ImageDraw.Draw(sheet)
for n, (rid, img) in enumerate(imgs):
    x, y = (n % cols) * cell_w, (n // cols) * cell_h
    big = img.resize((img.width * scale, img.height * scale), Image.NEAREST)
    sheet.paste(big, (x + (cell_w - big.width) // 2, y + 2))
    # Label directly under the sprite so it can't be mistaken for the next row's.
    draw.text((x + 4, y + big.height + 4), f"{rid} {img.width}x{img.height}", fill=(0, 0, 0))
sheet.save(out)
print(f"{len(imgs)} bitmaps -> {out}")
