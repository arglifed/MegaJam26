"""
MegaJam26 Issue #5: makes 1K copies of the Meshy texture maps before import (needs Pillow: pip install pillow).

Raw Meshy maps are 2048x2048. Unreal keeps the full-resolution source inside every texture .uasset, and those go to
Git LFS, so 2K maps would add roughly 340 MB to the repo. 1K is plenty for meshes the track generator stretches.

Reads   RawAssets/Art/<Category>/<Name>/<Name>_<map>.png
Writes  RawAssets/Art/.import_1k/<Name>_<map>.png   (git-ignored like all of RawAssets; safe to delete)

Run from the project root:   python Scripts/downscale_art_textures.py [size]
"""
import os
import sys

from PIL import Image

SIZE = int(sys.argv[1]) if len(sys.argv) > 1 else 1024
MAPS = ("base_color", "normal", "metallic", "roughness", "emission")
root = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "RawAssets", "Art")
out = os.path.join(root, ".import_1k")
os.makedirs(out, exist_ok=True)

count = 0
for cat in sorted(os.listdir(root)):
    cat_dir = os.path.join(root, cat)
    if cat.startswith(".") or not os.path.isdir(cat_dir):
        continue
    for name in sorted(os.listdir(cat_dir)):
        for m in MAPS:
            src = os.path.join(cat_dir, name, f"{name}_{m}.png")
            if not os.path.isfile(src):
                continue
            with Image.open(src) as img:
                if img.mode not in ("RGB", "RGBA", "L"):
                    img = img.convert("RGBA" if "A" in img.mode else "RGB")
                img.resize((SIZE, SIZE), Image.LANCZOS).save(os.path.join(out, f"{name}_{m}.png"), optimize=True)
            count += 1
print(f"[Downscale] wrote {count} maps at {SIZE}px to {os.path.normpath(out)}")
