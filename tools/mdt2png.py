#!/usr/bin/env python3
"""Render Zeliard .mdt cavern maps to PNG (see docs/ARCHITECTURE.md "Maps").

A cavern map is loaded raw to BASE:C000 (kernel AL=1) and every pointer in it is an
absolute BASE offset.  Layout (fight.bin is the consumer):

  C000 u16 level      -> level record {flags, ?, tileset, ai, enemies, [boss, ai2, en2, patches..]}
  C002 u16 width      map width in 8x8 cells; the map wraps horizontally (col == width -> 0)
  C004..C010 u16 x7   record lists (C010 = object table, 16-byte records, end 0xFFFF)
  C012 u8  cavern     1..10
  C013 u16 start col  (0xFFFF = none), C015 u8 start row, C016 u8 row bias, C017 u16 ?
  C019 u16 stream end (== C004)
  C01B..  tile stream: column-major RLE, 64 rows per column (fight.bin 6CED/6D57),
          byte b: 00-3F {count=b+1, value=next byte}; 40-7F count=((b>>4)&3)+2, value=(b&15)+1;
          80-BF count=b&0x3F of value 0 (empty); C0-FF one cell of value b&0x3F.
          Values are 1-based cell indices into the cavern tile bank MPP1..MPPB.GRP
          (ZELRES3[74..84], fight.bin table @9C43, picked by level record byte +2);
          cell 0 of the bank is a 24-byte list of solid cell indices (fight.bin 6DF3).
  Cell values 0x40..0x66 are DCHR.GRP (ZELRES3[54], loaded to arena:8C00 = bank slot 0x40):
  C004 list of {u16 col, u8 row} -> cells 40,41,42 at col..col+2 (fight.bin 7FB1)
  C006 list of {u16 col, u8 row} -> cells 43,44,45 (8163)
  C008 list of {u16 col|variant<<14, u8 row|state<<6, u8[4]} -> cells 46,47,48 (81AE)
  Objects (C010 records {u16 col, u8 row, u8 ?, u8 type, ...}) are placed in the 36-column
  ring buffer as 0x80|index when their column scrolls in; the renderer draws them as sprites
  (col 0xFFxx = disabled).  All lists end with 0xFFFF.

usage: mdt2png.py FILE OUT.png [--scale N] [--no-objects] [--info]
       mdt2png.py --all OUTDIR          # every cavern map (ZELRES3[20..50])
       mdt2png.py FILE --text           # ASCII dump of the cell grid
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from grp2png import decode48, blit, resource_names  # noqa: E402
from PIL import Image, ImageDraw  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE = 0xC000
ROWS = 64
BG = (0, 0, 0)
TILE_BANKS = list(range(74, 85))  # ZELRES3 index of MPP1..MPP9, MPPA, MPPB (fight.bin @9C43)
MUSIC = ["mgt1", "ugm1", "mgt2", "ugm2", "mus1", "mus2", "mus3", "mus4", "mus5", "mus6",
         "mus7", "mus8", "mbos", "mmao"]  # fight.bin @9E53


def u16(d, o):
    return struct.unpack_from("<H", d, o)[0]


def decode_stream(d, start, width):
    """Column-major RLE -> list of `width` columns, each 64 cell values.  Returns (cols, end)."""
    si, cols = start, []
    while len(cols) < width:
        col = []
        while len(col) < ROWS:
            b = d[si]
            k = b >> 6
            if k == 0:
                n, v, si = b + 1, d[si + 1], si + 2
            elif k == 1:
                n, v, si = ((b >> 4) & 3) + 2, (b & 15) + 1, si + 1
            elif k == 2:
                n, v, si = b & 0x3F, 0, si + 1
            else:
                n, v, si = 1, b & 0x3F, si + 1
            col += [v] * n
        if len(col) != ROWS:
            raise ValueError(f"column {len(cols)} has {len(col)} rows")
        cols.append(col)
    return cols, si


def parse(d):
    m = {
        "level": u16(d, 0), "width": u16(d, 2),
        "ptrs": [u16(d, 4 + 2 * i) for i in range(7)],
        "cavern": d[0x12], "start_col": u16(d, 0x13), "start_row": d[0x15],
        "row_bias": d[0x16], "w17": u16(d, 0x17), "stream_end": u16(d, 0x19),
    }
    lv = m["level"] - BASE
    m["flags"] = d[lv]
    m["music"] = (d[lv] >> 1) & 0xF
    m["tileset"], m["ai"], m["enemies"] = d[lv + 2], d[lv + 3], d[lv + 4]
    m["cols"], end = decode_stream(d, 0x1B, m["width"])
    m["stream_ok"] = end + BASE == m["stream_end"]
    objs, o = [], m["ptrs"][6] - BASE
    while u16(d, o) != 0xFFFF:
        objs.append(d[o:o + 16])
        o += 16
    m["objects"] = objs
    m["fixtures"] = []  # (col, row, first cell) for the DCHR three-cell fixtures
    for ptr, size, cell in ((m["ptrs"][0], 3, 0x40), (m["ptrs"][1], 3, 0x43), (m["ptrs"][2], 7, 0x46)):
        o = ptr - BASE
        while u16(d, o) != 0xFFFF:
            m["fixtures"].append((u16(d, o) & 0x3FFF, d[o + 2] & 0x3F, cell))
            o += size
    return m


def load_bank(idx):
    """Cavern tile bank MPPx (cells 1..) with DCHR.GRP overlaid at slot 0x40, as at arena:8000."""
    def cells(path):
        d = open(path, "rb").read()
        return [decode48(d[i * 48:(i + 1) * 48]) for i in range(len(d) // 48)]
    mpp = cells(os.path.join(ROOT, "extracted", "ZELRES3", "dec", f"{TILE_BANKS[idx]:03d}_data.dec"))
    dchr = cells(os.path.join(ROOT, "extracted", "ZELRES3", "dec", "054_data.dec"))
    bank = mpp + [None] * (0x40 - len(mpp)) if len(mpp) < 0x40 else mpp[:0x40]
    return bank + dchr, list(open(os.path.join(ROOT, "extracted", "ZELRES3", "dec",
                                               f"{TILE_BANKS[idx]:03d}_data.dec"), "rb").read()[:24])


def render(m, scale=2, objects=True):
    cells, solid = load_bank(m["tileset"])
    w, h = m["width"] * 8, ROWS * 8
    im = Image.new("RGB", (w, h), BG)
    grid = [list(c) for c in m["cols"]]
    for col, row, cell in m["fixtures"]:
        for i in range(3):
            grid[(col + i) % m["width"]][row] = cell + i
    for x, col in enumerate(grid):
        for y, v in enumerate(col):
            if v == 0:
                continue
            if v < len(cells) and cells[v]:
                blit(im, cells[v], x * 8, y * 8)
            else:
                ImageDraw.Draw(im).rectangle([x * 8, y * 8, x * 8 + 7, y * 8 + 7], fill=(255, 0, 255))
    im = im.resize((w * scale, h * scale), Image.NEAREST)
    if objects:
        dr = ImageDraw.Draw(im)
        for i, r in enumerate(m["objects"]):
            col, row, typ = u16(r, 0), r[2], r[4]
            if col >= m["width"]:  # 0xFFxx = disabled
                continue
            x, y = col * 8 * scale, row * 8 * scale
            colour = (255, 255, 0) if typ < 0x40 else (0, 255, 255) if typ < 0x80 else (255, 80, 80)
            dr.rectangle([x, y, x + 8 * scale - 1, y + 8 * scale - 1], outline=colour)
    return im


def text_grid(m):
    sym = lambda v: "." if v == 0 else chr(48 + v) if v < 10 else chr(55 + v)  # noqa: E731
    return "\n".join("".join(sym(m["cols"][x][y]) for x in range(m["width"])) for y in range(ROWS))


def info(m, name):
    p = m["ptrs"]
    print(f"{name}: cavern {m['cavern']}  width {m['width']}  stream {'ok' if m['stream_ok'] else 'BAD'}"
          f"  tileset MPP{'123456789AB'[m['tileset']]}  ai {m['ai']}  enemies {m['enemies']}"
          f"  music {MUSIC[m['music']] if m['music'] < len(MUSIC) else m['music']} (flags {m['flags']:02x})"
          f"  start col {m['start_col']:#x} row {m['start_row']} bias {m['row_bias']}")
    print("  lists: " + " ".join(f"C{4 + 2 * i:03X}={v:04X}" for i, v in enumerate(p)))
    print(f"  {len(m['fixtures'])} fixtures (col,row,cell):", " ".join(
        f"({c},{r},{k:02x})" for c, r, k in m["fixtures"]))
    print(f"  {len(m['objects'])} objects (col,row,type):", " ".join(
        f"({u16(r, 0)},{r[2]},{r[4]:02x})" for r in m["objects"]))


def name_of(path):
    mm = re.search(r"(ZELRES\d)/(?:dec/)?(\d{3})_", path)
    return resource_names().get((mm.group(1), int(mm.group(2)))) if mm else os.path.basename(path)


def main():
    args = sys.argv[1:]

    def opt(name, default):
        return int(args[args.index(name) + 1], 0) if name in args else default

    scale, objects = opt("--scale", 2), "--no-objects" not in args
    if args and args[0] == "--all":
        outdir = args[1]
        os.makedirs(outdir, exist_ok=True)
        for idx in range(20, 51):
            path = os.path.join(ROOT, "extracted", "ZELRES3", "dec", f"{idx:03d}_data.dec")
            if not os.path.exists(path):
                continue
            name = name_of(path) or f"zelres3_{idx}"
            m = parse(open(path, "rb").read())
            out = os.path.join(outdir, name.replace(".mdt", ".png"))
            render(m, scale, objects).save(out)
            info(m, name)
        return
    src = args[0]
    m = parse(open(src, "rb").read())
    if "--text" in args:
        print(text_grid(m))
        return
    if "--info" in args:
        info(m, name_of(src))
    if len(args) > 1 and not args[1].startswith("--"):
        render(m, scale, objects).save(args[1])
        print(f"{args[1]}: {name_of(src)} {m['width']}x{ROWS} cells, tileset MPP{'123456789AB'[m['tileset']]}")


if __name__ == "__main__":
    main()
