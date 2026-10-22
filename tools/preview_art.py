#!/usr/bin/env python3
"""Render a contact sheet of every sprite in art.h.

Usage: python3 tools/preview_art.py [out.png]      (default: tools/art_preview.png)

Parses art.h with regexes (no C++ compile), validates every sprite's row count and
row lengths against its Art{w,h,...} declaration, and exits non-zero on any mismatch.
"""
import os
import re
import sys

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))
ART_H = os.path.join(HERE, "..", "art.h")
SCALE = 6
THEMES = [("DAY", "#f7f7f7", "#535353", "#222222"), ("NIGHT", "#202124", "#acacac", "#dddddd")]

# Display order: rows of related sprites so frames can be compared side by side.
LAYOUT = [
    ["DINO_IDLE", "DINO_BLINK", "DINO_DEAD", "DINO_RUN_A", "DINO_RUN_B"],
    ["DINO_DUCK_A", "DINO_DUCK_B", "PTERA_UP", "PTERA_DOWN"],
    ["CACTUS_SMALL_A", "CACTUS_SMALL_B", "CACTUS_LARGE_A", "CACTUS_LARGE_B", "CLOUD"],
    ["STAR_A", "STAR_B", "RESTART", "BUMP_A", "BUMP_B"],
]


def parse(path):
    src = open(path).read()
    arrays = {}
    for m in re.finditer(r"const\s+char\*\s+(\w+)_ROWS\s*\[\s*\]\s*=\s*\{(.*?)\};", src, re.S):
        arrays[m.group(1)] = re.findall(r'"([^"]*)"', m.group(2))
    sprites = {}
    for m in re.finditer(r"constexpr\s+Art\s+(\w+)\s*=\s*\{\s*(\d+)\s*,\s*(\d+)\s*,\s*data::(\w+)_ROWS\s*\}", src):
        name, w, h, arr = m.group(1), int(m.group(2)), int(m.group(3)), m.group(4)
        sprites[name] = (w, h, arrays.get(arr))
        if arr != name:
            fail(f"{name}: Art refers to array {arr}_ROWS")
    return arrays, sprites


def fail(msg):
    print("ART ERROR:", msg, file=sys.stderr)
    fail.count += 1


fail.count = 0


def validate(arrays, sprites):
    for arr in arrays:
        if arr not in sprites:
            fail(f"array {arr}_ROWS has no Art declaration")
    for name, (w, h, rows) in sprites.items():
        if rows is None:
            fail(f"{name}: rows array missing")
            continue
        if len(rows) != h:
            fail(f"{name}: declared h={h} but {len(rows)} rows")
        for i, r in enumerate(rows):
            if len(r) != w:
                fail(f"{name}: row {i} has {len(r)} chars, expected w={w}")
            if set(r) - set("#."):
                fail(f"{name}: row {i} has characters other than '#' and '.'")
        if rows and "#" not in rows[-1] and not name.startswith("PTERA"):  # flapping wings: frames share a box
            fail(f"{name}: last row is empty (feet/base must be on the last row)")
    for group in LAYOUT:
        for n in group:
            if n not in sprites:
                fail(f"{n} missing from art.h")
    for grp in (["DINO_IDLE", "DINO_RUN_A", "DINO_RUN_B", "DINO_BLINK", "DINO_DEAD"],
                ["DINO_DUCK_A", "DINO_DUCK_B"], ["PTERA_UP", "PTERA_DOWN"]):
        sizes = {sprites[n][:2] for n in grp if n in sprites}
        if len(sizes) > 1:
            fail(f"sizes differ within {grp}: {sizes}")
    if fail.count:
        print(f"{fail.count} problem(s) found", file=sys.stderr)
        sys.exit(1)


def font(sz):
    for p in ("/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
              "/usr/share/fonts/dejavu/DejaVuSansMono.ttf"):
        if os.path.exists(p):
            return ImageFont.truetype(p, sz)
    return ImageFont.load_default()


def render(sprites, out):
    f = font(12)
    pad = 14
    label_h = 30
    sections = []
    total_h = 0
    max_w = 0
    for theme in THEMES:
        sec_h = 24
        sec_w = 0
        row_infos = []
        for group in LAYOUT:
            rw = pad
            rh = 0
            for n in group:
                w, h, _ = sprites[n]
                cw = max(w * SCALE, 8 * len(n) + 4)
                rw += cw + pad
                rh = max(rh, h * SCALE)
            row_infos.append((group, rh))
            sec_h += rh + label_h + pad
            sec_w = max(sec_w, rw)
        sections.append((theme, row_infos, sec_h))
        total_h += sec_h
        max_w = max(max_w, sec_w)
    img = Image.new("RGB", (max_w, total_h), "#808080")
    d = ImageDraw.Draw(img)
    y0 = 0
    for (tname, bg, ink, lab), row_infos, sec_h in sections:
        d.rectangle([0, y0, max_w, y0 + sec_h], fill=bg)
        d.text((pad, y0 + 6), tname, fill=lab, font=f)
        y = y0 + 24
        for group, rh in row_infos:
            x = pad
            for n in group:
                w, h, rows = sprites[n]
                cw = max(w * SCALE, 8 * len(n) + 4)
                top = y + rh - h * SCALE  # bottom-align within the row
                for ry, r in enumerate(rows):
                    for rx, ch in enumerate(r):
                        if ch == "#":
                            d.rectangle([x + rx * SCALE, top + ry * SCALE,
                                         x + (rx + 1) * SCALE - 1, top + (ry + 1) * SCALE - 1], fill=ink)
                d.text((x, y + rh + 4), f"{n}", fill=lab, font=f)
                d.text((x, y + rh + 17), f"{w}x{h}", fill=lab, font=f)
                x += cw + pad
            y += rh + label_h + pad
        y0 += sec_h
    img.save(out)
    print("wrote", out)


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "art_preview.png")
    arrays, sprites = parse(ART_H)
    validate(arrays, sprites)
    print(f"{len(sprites)} sprites OK")
    for n, (w, h, _) in sprites.items():
        print(f"  {n:16s} {w}x{h}")
    render(sprites, out)


if __name__ == "__main__":
    main()
