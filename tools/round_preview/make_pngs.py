# Turns out/*.ppm into round PNGs (outside the circle = black, like the
# AMOLED) and a contact sheet, out/all_screens.png.
import glob, os
from PIL import Image, ImageDraw

files = sorted(glob.glob("out/*.ppm"))
mask = Image.new("L", (466, 466), 0)
ImageDraw.Draw(mask).ellipse((0, 0, 465, 465), fill=255)
shots = []
for f in files:
    im = Image.open(f).convert("RGB")
    out = Image.new("RGB", (466, 466), (0, 0, 0))
    out.paste(im, (0, 0), mask)
    png = f[:-4] + ".png"
    out.save(png)
    os.remove(f)
    shots.append(out)
cols = 4
rows = (len(shots) + cols - 1) // cols
sheet = Image.new("RGB", (cols * 486 + 20, rows * 486 + 20), (24, 24, 28))
for i, s in enumerate(shots):
    sheet.paste(s, (20 + (i % cols) * 486, 20 + (i // cols) * 486))
sheet.save("out/all_screens.png")
print(f"{len(shots)} screens -> out/")
