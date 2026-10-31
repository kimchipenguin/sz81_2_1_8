#!/usr/bin/env python3
"""Make data/zx81.bmp, the picture of the About window, from atari/zx81.png.

The photo is scaled to 288 pixels wide, laid over the dialog's white
background (its edges are soft) and reduced to 64 colours. The Atari code
dithers it further for screens with 16 colours or fewer.
Needs Pillow: python3 atari/mkabout.py
"""
import os
from PIL import Image

here = os.path.dirname(os.path.abspath(__file__))
src = Image.open(os.path.join(here, "zx81.png")).convert("RGBA")
w = 288
h = round(src.height * w / src.width)
img = src.resize((w, h), Image.LANCZOS)
white = Image.new("RGBA", img.size, (255, 255, 255, 255))
white.alpha_composite(img)
pic = white.convert("RGB").quantize(64, method=Image.FASTOCTREE, kmeans=4, dither=Image.FLOYDSTEINBERG)
pic.save(os.path.join(here, "..", "data", "zx81.bmp"))
print("data/zx81.bmp: %dx%d" % (w, h))
