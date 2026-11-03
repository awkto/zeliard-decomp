#!/usr/bin/env python3
"""Render zeliard.png (256x256) -- the same sword as zeliard.svg -- using only
the standard library, so the build needs no image tooling.  Original artwork;
nothing here comes from the game's files."""
import struct, sys, zlib

N = 256
BG, EDGE, STEEL, SHADE, GOLD, WOOD = (27, 16, 48), (107, 79, 176), (223, 230, 240), (154, 168, 189), (224, 168, 46), (122, 59, 30)

def inside_round_rect(x, y, x0, y0, x1, y1, r):
    if not (x0 <= x < x1 and y0 <= y < y1):
        return False
    cx = min(max(x, x0 + r), x1 - 1 - r)
    cy = min(max(y, y0 + r), y1 - 1 - r)
    return (x - cx) ** 2 + (y - cy) ** 2 <= r * r

def blade(x, y):                      # triangle (128,24) (144,148) (112,148)
    if not 24 <= y <= 148:
        return None
    half = 16 * (y - 24) / 124.0
    if abs(x - 128) > half:
        return None
    return SHADE if x >= 128 else STEEL

def pixel(x, y):
    if not inside_round_rect(x, y, 0, 0, N, N, 32):
        return (0, 0, 0, 0)
    c = BG
    if inside_round_rect(x, y, 6, 6, 250, 250, 28) and not inside_round_rect(x, y, 10, 10, 246, 246, 24):
        c = EDGE
    b = blade(x, y)
    if b: c = b
    if inside_round_rect(x, y, 80, 148, 176, 162, 5): c = GOLD
    if 121 <= x < 135 and 162 <= y < 206: c = WOOD
    if (x - 128) ** 2 + (y - 216) ** 2 <= 144: c = GOLD
    return c + (255,)

raw = b"".join(b"\x00" + b"".join(bytes(pixel(x, y)) for x in range(N)) for y in range(N))
def chunk(t, d):
    c = struct.pack(">I", len(d)) + t + d
    return c + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)
png = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", N, N, 8, 6, 0, 0, 0)) \
      + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")
open(sys.argv[1] if len(sys.argv) > 1 else "zeliard.png", "wb").write(png)
