#!/usr/bin/env python3
"""Render a bank of 8x8 sprite/tile cells to a PNG contact sheet (MCGA palette).

Cell format (48 bytes) as stored in .grp resources = PC-88 heritage:
  8 rows x { planeA u16 BE, planeB u16 BE, planeC u16 BE }  -> 16 px/row, 3 bits/px
  pixel value v = C<<2 | B<<1 | A (bit 15 = leftmost)
The MCGA driver service [0x2044] (GMMCGA @2C2A) packs each pair of adjacent 3-bit
pixels into one 6-bit VGA index (left<<3 | right), so the on-screen cell is 8x8 with
64 colours (see tools/palette.py). --packed reads that post-conversion layout instead
(gfmcga blitter @412F: per 3 bytes b0,b1,b2 -> [b1>>2, (b1&3)<<4|b0>>4,
(b0&15)<<2|b2>>6, b2&63]).

usage: cellsheet.py FILE OUT.png [--skip N] [--count N] [--cols N] [--scale N] [--packed]
"""
import sys

sys.path.insert(0, __file__.rsplit("/", 1)[0])
from palette import MCGA  # noqa: E402

from PIL import Image  # noqa: E402

CELL = 48


def decode_pc88(c: bytes) -> list:
    px = []
    for row in range(8):
        r = c[row * 6:row * 6 + 6]
        a, b, cc = (r[0] << 8) | r[1], (r[2] << 8) | r[3], (r[4] << 8) | r[5]
        p16 = [(((cc >> (15 - x)) & 1) << 2) | (((b >> (15 - x)) & 1) << 1)
               | ((a >> (15 - x)) & 1) for x in range(16)]
        px.append([(p16[2 * k] << 3) | p16[2 * k + 1] for k in range(8)])
    return px


def decode_packed(c: bytes) -> list:
    px = []
    for row in range(8):
        r = c[row * 6:row * 6 + 6]
        o = []
        for g in range(2):
            b0, b1, b2 = r[g * 3], r[g * 3 + 1], r[g * 3 + 2]
            o += [b1 >> 2, ((b1 & 3) << 4) | (b0 >> 4), ((b0 & 0xF) << 2) | (b2 >> 6), b2 & 0x3F]
        px.append(o)
    return px


def main() -> None:
    args = sys.argv[1:]
    src, out = args[0], args[1]

    def opt(name, default):
        return int(args[args.index(name) + 1], 0) if name in args else default

    data = open(src, "rb").read()[opt("--skip", 0):]
    n = min(len(data) // CELL, opt("--count", 1 << 30))
    cols, scale = opt("--cols", 16), opt("--scale", 3)
    decode = decode_packed if "--packed" in args else decode_pc88
    rows = (n + cols - 1) // cols
    im = Image.new("RGB", (cols * 9, rows * 9), (40, 40, 40))
    for i in range(n):
        p = decode(data[i * CELL:(i + 1) * CELL])
        ox, oy = (i % cols) * 9, (i // cols) * 9
        for y in range(8):
            for x in range(8):
                v = p[y][x]
                im.putpixel((ox + x, oy + y), MCGA[v] if v else (20, 20, 60))
    im.resize((im.width * scale, im.height * scale), Image.NEAREST).save(out)
    print(f"{out}: {n} cells")


if __name__ == "__main__":
    main()
