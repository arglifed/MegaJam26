"""Builds the itch.io cover image (630x500 plus a 2x 1260x1000 copy) from playtest screenshots.

Usage: python Scripts/make_itch_cover.py [screenshot_dir] [out_dir]
Defaults: Docs/devlog_images/playtest_captures -> Docs/devlog_images
Needs Pillow and the Windows Impact and Bahnschrift fonts. Tune each panel's
crop centre and zoom in the `takes` list.
"""
import sys, os
from PIL import Image, ImageDraw, ImageFont, ImageOps, ImageFilter, ImageEnhance

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = sys.argv[1] if len(sys.argv) > 1 else os.path.join(ROOT, "Docs", "devlog_images", "playtest_captures")
OUT = sys.argv[2] if len(sys.argv) > 2 else os.path.join(ROOT, "Docs", "devlog_images")
S = 2               # supersample: render 1260x1000, ship 630x500
W, H = 630 * S, 500 * S

# (file, vehicle centre in 1280x720 source, zoom, label, grayscale)
takes = [
    ("orb_shots/s1_rearclose.png",    (680, 360), 0.85, "ACTION",    False),
    ("orb_shots/s2_threequarter.png", (470, 330), 0.8, "WESTERN",   False),
    ("orb_shots/s3_threequarter.png", (560, 330), 0.95, "NOIR",      True),
    ("orb_shots/s4_threequarter.png", (660, 560), 0.8, "CHASE",     False),
    ("orb_shots/s5_threequarter.png", (600, 300), 0.85, "FINAL CUT", False),
]

F = "C:/Windows/Fonts/"
title_f = ImageFont.truetype(F + "impact.ttf", 118 * S // 2)
sub_f   = ImageFont.truetype(F + "bahnschrift.ttf", 17 * S)
small_f = ImageFont.truetype(F + "bahnschrift.ttf", 13 * S)
tag_f   = ImageFont.truetype(F + "impact.ttf", 15 * S)

canvas = Image.new("RGB", (W, H), (10, 10, 12))
n = len(takes)
pw = W / n
slant = 34 * S          # horizontal offset of the splice cut, top vs bottom
gap = 5 * S             # black splice line between takes

for i, (f, (cx, cy), zoom, label, gray) in enumerate(takes):
    im = Image.open(os.path.join(SRC, f)).convert("RGB")
    if gray:
        im = ImageOps.grayscale(im).convert("RGB")
        im = ImageEnhance.Contrast(im).enhance(1.25)
    else:
        im = ImageEnhance.Color(im).enhance(1.2)
    sc = H / 720 * zoom
    im = im.resize((int(1280 * sc), int(720 * sc)), Image.LANCZOS)
    # panel's horizontal extent incl. slant; centre vehicle in it
    x0 = int(i * pw - slant)
    x1 = int((i + 1) * pw + slant)
    wpan = x1 - x0
    left = int(cx * sc - wpan / 2)
    top = int(cy * sc - H * 0.33)
    left = max(0, min(left, im.width - wpan))
    top = max(0, min(top, im.height - H))
    crop = im.crop((left, top, left + wpan, top + H))
    # slanted mask: top edge shifted right, bottom edge shifted left
    mask = Image.new("L", (W, H), 0)
    md = ImageDraw.Draw(mask)
    a = i * pw + (gap / 2 if i else -slant)
    b = (i + 1) * pw - (gap / 2 if i < n - 1 else -slant)
    md.polygon([(a + slant / 2, 0), (b + slant / 2, 0),
                (b - slant / 2, H), (a - slant / 2, H)], fill=255)
    layer = Image.new("RGB", (W, H))
    layer.paste(crop, (x0, 0))
    canvas.paste(layer, (0, 0), mask)

# darken lower part for title legibility
grad = Image.new("L", (1, H))
for y in range(H):
    t = max(0.0, (y / H - 0.45) / 0.55)
    grad.putpixel((0, y), int(235 * t ** 1.3))
grad = grad.resize((W, H))
canvas.paste(Image.new("RGB", (W, H), (6, 6, 10)), (0, 0), grad)
# light top vignette for the studio credit
topg = Image.new("L", (1, H))
for y in range(H):
    t = max(0.0, 1 - y / (H * 0.16))
    topg.putpixel((0, y), int(170 * t))
canvas.paste(Image.new("RGB", (W, H), (6, 6, 10)), (0, 0), topg.resize((W, H)))

d = ImageDraw.Draw(canvas)

# take labels under each panel, like slate tags
for i, t in enumerate(takes):
    cx = (i + 0.5) * pw - slant * 0.05
    txt = f"{i + 1:02d}  {t[3]}"
    tw = d.textlength(txt, font=tag_f)
    y = H * 0.565
    d.rectangle([cx - tw / 2 - 6 * S, y - 2 * S, cx + tw / 2 + 6 * S, y + 19 * S],
                fill=(8, 8, 10))
    d.text((cx - tw / 2, y), txt, font=tag_f, fill=(120, 230, 235) if i != 2 else (235, 235, 235))

# film strip band with sprockets behind the title
band_top, band_bot = int(H * 0.66), int(H * 0.93)
d.rectangle([0, band_top, W, band_bot], fill=(8, 8, 10))
hole_w, hole_h, step = 12 * S, 8 * S, 24 * S
for x in range(6 * S, W, step):
    for yy in (band_top + 5 * S, band_bot - 5 * S - hole_h):
        d.rounded_rectangle([x, yy, x + hole_w, yy + hole_h], radius=2 * S, fill=(225, 225, 220))

# title
title = "DIRECTOR'S CUT"
tw = d.textlength(title, font=title_f)
bbox = d.textbbox((0, 0), title, font=title_f)
th = bbox[3] - bbox[1]
tx = (W - tw) / 2
ty = (band_top + band_bot) / 2 - th / 2 - bbox[1] - 8 * S
# cyan glow
glow = Image.new("L", (W, H), 0)
ImageDraw.Draw(glow).text((tx, ty), title, font=title_f, fill=255)
glow = glow.filter(ImageFilter.GaussianBlur(9 * S))
canvas.paste(Image.new("RGB", (W, H), (60, 220, 230)), (0, 0), glow.point(lambda v: int(v * 0.8)))
d = ImageDraw.Draw(canvas)
d.text((tx, ty), title, font=title_f, fill=(255, 255, 255))

tag = "DROP THE CLAPPERBOARD.  ROLL CAMERA."
sw = d.textlength(tag, font=small_f)
d.text(((W - sw) / 2, ty + th + bbox[1] + 6 * S), tag, font=small_f, fill=(200, 200, 205))

pres = "FOUR SWORDS PICTURES PRESENTS"
pw2 = d.textlength(pres, font=sub_f)
d.text(((W - pw2) / 2, 14 * S), pres, font=sub_f, fill=(245, 245, 245))

foot = "5 GENRES  •  5 VEHICLES  •  ONE CONTINUOUS TAKE"
fw = d.textlength(foot, font=small_f)
d.text(((W - fw) / 2, H - 25 * S), foot, font=small_f, fill=(150, 225, 230))

os.makedirs(OUT, exist_ok=True)
canvas.save(os.path.join(OUT, "itch_cover_1260x1000.png"))
canvas.resize((630, 500), Image.LANCZOS).save(os.path.join(OUT, "itch_cover_630x500.png"))
print("ok")
