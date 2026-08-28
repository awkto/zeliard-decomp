#!/usr/bin/env python3
"""Render Zeliard .grp graphics resources to PNG (MCGA look).

Formats (see docs/ARCHITECTURE.md "Graphics"):
  cells48  bank of 48-byte cells: PC-88 16x8x3bpp -> 8x8 with 64 colours after the
           driver packs pixel pairs ([0x2044]).  Background tiles (roka.grp), shop
           portraits (king/omoya/armor/bank/church/drug/inn/kenjya.grp), en72.grp.
  cells32  bank of 32-byte cells: 16x8 two-bitplane -> 8x8, each pixel PAIR indexes a
           16-entry colour table (gfmcga @4F98.., selected by [0x4ff4]).  Converted by
           gfmcga vec_20 @4EDD, which also builds the horizontally dilated outline mask.
           Enemy banks enp1-8.grp, dman.grp (hero death animation cells).
  hero     fman.grp: 91 frame maps x 9 bytes (3x3 cells row-major, 1-based cell index,
           bit7 = horizontal flip, 0 = empty) at 0x000, then cells32 bank at 0x333
           (cell 0 is blank).  Frames are 24x24.  dman.grp = 54 loose cells32 (death animation).
  font     font.grp section 1: 192 glyphs x 8 bytes, 1bpp, from char 0x20.

usage: grp2png.py FILE OUT.png [--format F] [--skip N] [--cols N] [--scale N] [--pal N]
       grp2png.py --all OUTDIR          # every resource with a known format
Format is guessed from the resource name (docs/RESOURCES.md) when --format is omitted.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from palette import MCGA  # noqa: E402
from PIL import Image  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BG = (20, 20, 60)

# gfmcga.bin @4F98..4FE7 — colour tables for 2bpp sprites; index = (left<<2)|right,
# entry = VGA index (= left8*8 + right8 of four PC-88 colours per table).
PAL2BPP = [
    [0x00, 0x01, 0x02, 0x03, 0x08, 0x09, 0x0A, 0x0B, 0x10, 0x11, 0x12, 0x13, 0x18, 0x19, 0x1A, 0x1B],
    [0x00, 0x02, 0x04, 0x06, 0x10, 0x12, 0x14, 0x16, 0x20, 0x22, 0x24, 0x26, 0x30, 0x32, 0x34, 0x36],
    [0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0C, 0x0D, 0x20, 0x21, 0x24, 0x25, 0x28, 0x29, 0x2C, 0x2D],
    [0x00, 0x05, 0x06, 0x07, 0x28, 0x2D, 0x2E, 0x2F, 0x30, 0x35, 0x36, 0x37, 0x38, 0x3D, 0x3E, 0x3F],
    [0x00, 0x06, 0x05, 0x07, 0x30, 0x36, 0x35, 0x37, 0x28, 0x2E, 0x2D, 0x2F, 0x38, 0x3E, 0x3D, 0x3F],
]

FORMAT_BY_NAME = {
    "roka.grp": ("cells48", 0), "en72.grp": ("cells48", 0), "dman.grp": ("cells32", 0),
    "king.grp": ("cells48", 0), "omoya.grp": ("cells48", 0), "armor.grp": ("cells48", 0),
    "bank.grp": ("cells48", 0), "church.grp": ("cells48", 0), "drug.grp": ("cells48", 0),
    "inn.grp": ("cells48", 0), "kenjya.grp": ("cells48", 0),
    "fman.grp": ("hero", 0), "font.grp": ("font", 6),
}
for _i in range(1, 9):
    FORMAT_BY_NAME[f"enp{_i}.grp"] = ("cells32", 0)


def resource_names():
    """(archive, index) -> name from docs/RESOURCES.md (only single-name rows)."""
    out = {}
    for line in open(os.path.join(ROOT, "docs", "RESOURCES.md")):
        m = re.match(r"\| (ZELRES\d) \| (\d+) \| ([^|]+) \|", line)
        if m and "," not in m.group(3):
            out[(m.group(1), int(m.group(2)))] = m.group(3).strip()
    return out


def name_of(path):
    m = re.search(r"(ZELRES\d)/(?:dec/)?(\d{3})_", path)
    return resource_names().get((m.group(1), int(m.group(2)))) if m else None


def decode48(c):
    px = []
    for row in range(8):
        r = c[row * 6:row * 6 + 6]
        a, b, cc = (r[0] << 8) | r[1], (r[2] << 8) | r[3], (r[4] << 8) | r[5]
        p = [(((cc >> (15 - x)) & 1) << 2) | (((b >> (15 - x)) & 1) << 1) | ((a >> (15 - x)) & 1)
             for x in range(16)]
        px.append([MCGA[(p[2 * k] << 3) | p[2 * k + 1]] if (p[2 * k] | p[2 * k + 1]) else None
                   for k in range(8)])
    return px


def decode32(c, pal):
    px = []
    for row in range(8):
        a, b = (c[row * 4] << 8) | c[row * 4 + 1], (c[row * 4 + 2] << 8) | c[row * 4 + 3]
        p = [(((b >> (15 - x)) & 1) << 1) | ((a >> (15 - x)) & 1) for x in range(16)]
        px.append([MCGA[pal[(p[2 * k] << 2) | p[2 * k + 1]]] if (p[2 * k] | p[2 * k + 1]) else None
                   for k in range(8)])
    return px


def blit(im, px, ox, oy, flip=False):
    for y in range(8):
        for x in range(8):
            v = px[y][7 - x] if flip else px[y][x]
            if v:
                im.putpixel((ox + x, oy + y), v)


def sheet(cells, cols, scale):
    rows = (len(cells) + cols - 1) // cols
    im = Image.new("RGB", (cols * 9 - 1, rows * 9 - 1), BG)
    for i, px in enumerate(cells):
        blit(im, px, (i % cols) * 9, (i // cols) * 9)
    return im.resize((im.width * scale, im.height * scale), Image.NEAREST)


def render(data, fmt, skip=0, cols=16, scale=3, pal=0):
    d = data[skip:]
    if fmt == "cells48":
        return sheet([decode48(d[i * 48:(i + 1) * 48]) for i in range(len(d) // 48)], cols, scale)
    if fmt == "cells32":
        return sheet([decode32(d[i * 32:(i + 1) * 32], PAL2BPP[pal]) for i in range(len(d) // 32)],
                     cols, scale)
    if fmt == "hero":
        maps, bank = d[:0x333], d[0x333:]
        cells = [decode32(bank[i * 32:(i + 1) * 32], PAL2BPP[pal]) for i in range(len(bank) // 32)]
        n = len(maps) // 9
        rows = (n + cols - 1) // cols
        im = Image.new("RGB", (cols * 25 - 1, rows * 25 - 1), BG)
        for f in range(n):
            m = maps[f * 9:(f + 1) * 9]
            for j, idx in enumerate(m):
                if idx and (idx & 0x7F) < len(cells):
                    blit(im, cells[idx & 0x7F], (f % cols) * 25 + (j % 3) * 8,
                         (f // cols) * 25 + (j // 3) * 8, bool(idx & 0x80))
        return im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    if fmt == "font":
        n = 192
        rows = (n + cols - 1) // cols
        im = Image.new("RGB", (cols * 9 - 1, rows * 9 - 1), BG)
        for g in range(n):
            for y in range(8):
                for x in range(8):
                    if d[g * 8 + y] & (0x80 >> x):
                        im.putpixel(((g % cols) * 9 + x, (g // cols) * 9 + y), (250, 250, 250))
        return im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    raise SystemExit(f"unknown format {fmt}")


def main():
    args = sys.argv[1:]

    def opt(name, default):
        return int(args[args.index(name) + 1], 0) if name in args else default

    scale, pal = opt("--scale", 3), opt("--pal", 0)
    if args and args[0] == "--all":
        outdir = args[1]
        os.makedirs(outdir, exist_ok=True)
        for (arch, idx), name in sorted(resource_names().items()):
            if name not in FORMAT_BY_NAME:
                continue
            fmt, skip = FORMAT_BY_NAME[name]
            path = os.path.join(ROOT, "extracted", arch, "dec", f"{idx:03d}_data.dec")
            if not os.path.exists(path):
                continue
            out = os.path.join(outdir, name.replace(".grp", ".png"))
            render(open(path, "rb").read(), fmt, skip, 16 if fmt != "hero" else 13, scale, pal).save(out)
            print(f"{name:12s} {fmt:8s} -> {out}")
        return
    src, out = args[0], args[1]
    name = name_of(src)
    fmt = args[args.index("--format") + 1] if "--format" in args else FORMAT_BY_NAME.get(name, (None,))[0]
    if fmt is None:
        raise SystemExit(f"no known format for {src} ({name}); pass --format")
    skip = opt("--skip", FORMAT_BY_NAME.get(name, (None, 0))[1])
    render(open(src, "rb").read(), fmt, skip, opt("--cols", 16 if fmt != "hero" else 13), scale, pal).save(out)
    print(f"{out}: {name or src} as {fmt}")


if __name__ == "__main__":
    main()
