#!/usr/bin/env python3
"""Render candidate pixel data from decompressed Zeliard resources to PNG.

No PIL dependency — minimal PNG writer via zlib. Default assumption:
4bpp packed pixels, rendered as a grid of tiles with the EGA palette.

usage: grpview.py FILE OUT.png [--tile WxH] [--width N] [--linear]
  --tile 16x16   tile dimensions (default 16x16)
  --width 16     tiles per row in output (default 16)
  --linear       treat data as one linear 4bpp bitmap, --width = pixels/row
  --planar       4-plane EGA planar rows (BGRI), --width = pixels/row
  --skip N       skip N header bytes
"""
import struct
import sys
import zlib

EGA = [(0, 0, 0), (0, 0, 170), (0, 170, 0), (0, 170, 170),
       (170, 0, 0), (170, 0, 170), (170, 85, 0), (170, 170, 170),
       (85, 85, 85), (85, 85, 255), (85, 255, 85), (85, 255, 255),
       (255, 85, 85), (255, 85, 255), (255, 255, 85), (255, 255, 255)]


def write_png(path: str, w: int, h: int, pix: list) -> None:
    raw = b"".join(b"\x00" + bytes(min(255, max(0, c))
                                   for p in row for c in EGA[p % len(EGA)])
                   for row in pix)

    def chunk(tag: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data)))

    hdr = struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0)
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", hdr)
           + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))
    with open(path, "wb") as f:
        f.write(png)


def nibbles(data: bytes):
    for b in data:
        yield b >> 4
        yield b & 15


def main() -> None:
    args = sys.argv[1:]
    src, out = args[0], args[1]
    opts = args[2:]

    def opt(name, default=None):
        return opts[opts.index(name) + 1] if name in opts else default

    data = open(src, "rb").read()[int(opt("--skip", "0")):]

    if "--linear" in opts:
        w = int(opt("--width", "320"))
        pix_flat = list(nibbles(data))
        h = len(pix_flat) // w
        pix = [pix_flat[y * w:(y + 1) * w] for y in range(h)]
    elif "--planar" in opts:
        w = int(opt("--width", "320"))
        rb = w // 8  # bytes per plane-row
        h = len(data) // (rb * 4)
        pix = []
        for y in range(h):
            row = []
            base = y * rb * 4
            for x in range(w):
                byte, bit = x // 8, 7 - (x % 8)
                v = 0
                for pl in range(4):  # B, G, R, I plane order
                    v |= ((data[base + pl * rb + byte] >> bit) & 1) << pl
                row.append(v)
            pix.append(row)
    else:
        tw, th = (int(v) for v in opt("--tile", "16x16").split("x"))
        per = tw * th // 2
        n = len(data) // per
        cols = int(opt("--width", "16"))
        rows = (n + cols - 1) // cols
        w, h = cols * tw, rows * th
        pix = [[0] * w for _ in range(h)]
        for t in range(n):
            tp = list(nibbles(data[t * per:(t + 1) * per]))
            ox, oy = (t % cols) * tw, (t // cols) * th
            for i, v in enumerate(tp):
                pix[oy + i // tw][ox + i % tw] = v
    write_png(out, w, h, pix)
    print(f"{out}: {w}x{h} from {len(data)} bytes")


if __name__ == "__main__":
    main()
