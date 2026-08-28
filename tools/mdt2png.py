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
          cell 0 of the bank is a 24-byte list of PASSABLE cell indices (fight.bin 6DF3; see docs/FIGHT.md §3).
  Cell values 0x40..0x66 are DCHR.GRP (ZELRES3[54], loaded to arena:8C00 = bank slot 0x40):
  C004 list of {u16 col, u8 row} -> cells 40,41,42 at col..col+2 (fight.bin 7FB1)
  C006 list of {u16 col, u8 row} -> cells 43,44,45 (8163)
  C008 list of {u16 col|variant<<14, u8 row|state<<6, u8[4]} -> cells 46,47,48 (81AE)
  Objects (C010 records {u16 col, u8 row, u8 ?, u8 type, ...}) are placed in the 36-column
  ring buffer as 0x80|index when their column scrolls in; the renderer draws them as sprites
  (col 0xFFxx = disabled).  All lists end with 0xFFFF.

usage: mdt2png.py FILE OUT.png [--scale N] [--no-objects] [--info]
       mdt2png.py --all OUTDIR          # every cavern map (ZELRES3[20..50]) and every town map
       mdt2png.py --town OUTDIR         # the 10 town maps only (ZELRES2[36..45])
       mdt2png.py FILE --text           # ASCII dump of the cell grid

Town maps (cmap/mrmp/stmp/bsmp/hlmp/tmmp/drmp/llmp/prmp/esmp.mdt, ZELRES2[36..45]; consumer
town.bin, see docs/TOWN.md) are detected automatically.  They are RAW, not RLE: 8 rows per
column, column-major, `width*8` bytes from C017 (`[C011]` = end of the grid).  Header:
  C000 ->level {music_flags, gfx (0 mman/1 cman), 0xFF, town_flags, tileset}
  C002 width   C004 ->place label   C006 map id   C007 ->edge exits   C009 ->doors {u16 col, u8 dest}
  C00B ->cavern entries {u16 col, u8 row, u8 side, u8 map}   C00D ->dialogue ptr table
  C00F ->NPC records {u16 col, u8 sprite|facing<<7, u8 saved, u8 anim, u8 type, u8 flags, u8 script}
  C011 ->NPC walk range {u16 min, u16 max}   C013 u16 (unused)   C015 ->patches
Tiles come from the town TILE BANK cpat/mpat/dpat.grp (ZELRES2[33..35], picked by level byte 4):
0x100-byte header {u16 6, u16 off_block, u16 off_anim, u8 type[cells]...} then 48-byte PC-88
cells.  gtmcga @3AF9 uses the type to pick the plane that carries the SKY MASK (0 opaque;
1 C=mask; 2 B=mask; 3 A=mask; 4 fully sky).  Rows 0-2 show the ympd/ckpd backdrop through the
mask (drawn here as a flat sky colour); the hero walks on row 7, NPCs live on row 5.
NPC sprites: mman/cman.grp (ZELRES2[29..30]) = 5 sprites x 8 frames x 6 cells (2 cols x 3 rows,
1-based) in the first 0x100 bytes, cells48 from 0x100; colour 0 transparent, white drawn black.
"""
import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from grp2png import decode48, blit, resource_names  # noqa: E402
from palette import MCGA  # noqa: E402
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


# ---------------------------------------------------------------------------------------------
# Town maps (town.bin, gtmcga)
# ---------------------------------------------------------------------------------------------
TOWN_ROWS = 8
TOWN_NAMES = ["cmap", "mrmp", "stmp", "bsmp", "hlmp", "tmmp", "drmp", "llmp", "prmp", "esmp"]
TOWN_BANKS = [33, 34, 35]          # ZELRES2 cpat/mpat/dpat.grp (town.bin table @6DCE)
TOWN_NPC_BANKS = [29, 30]          # ZELRES2 mman/cman.grp (town.bin table @6D88)
TOWN_MUSIC = ["mgt1", "ugm1", "mgt2", "ugm2"]  # GAME.BIN table @A363, index = level byte0 >> 1
SHOPS = ["king", "omoya", "sage", "armour", "drug", "church", "bank", "inn"]  # town.bin table @6F07
SKY = MCGA[0x0D]                   # white+blue blend stands in for the ympd/ckpd backdrop
NPC_TYPES = ["face", "walk2", "walk4", "face2", "idle", "wander2", "wander4", "static"]  # town.bin @6B41


def is_town(d):
    """Town header test: map id 1..9 at C006 and [C011] == C017 + width*8 (raw grid)."""
    return 1 <= d[6] <= 9 and u16(d, 0x11) == BASE + 0x17 + u16(d, 2) * TOWN_ROWS


def parse_town(d):
    w = u16(d, 2)
    m = {"town": True, "width": w, "map_id": d[6], "level_ptr": u16(d, 0),  # C006 = cavern/region number (not read by town.bin)
         "ptrs": {k: u16(d, o) for k, o in (("label", 4), ("exits", 7), ("doors", 9), ("caves", 0xB),
                                            ("dialogue", 0xD), ("npcs", 0xF), ("range", 0x11),
                                            ("patches", 0x15))},
         "w13": u16(d, 0x13)}
    lv = m["level_ptr"] - BASE
    m["music"], m["gfx"] = (d[lv] >> 1) & 0xF, d[lv + 1]
    m["town_flags"], m["tileset"] = d[lv + 3], d[lv + 4]
    lab = m["ptrs"]["label"] - BASE
    m["label"] = d[lab + 4:lab + 4 + d[lab + 3]].decode("latin1")
    m["cols"] = [list(d[0x17 + c * TOWN_ROWS:0x17 + (c + 1) * TOWN_ROWS]) for c in range(w)]
    o, doors = m["ptrs"]["doors"] - BASE, []
    while u16(d, o) != 0xFFFF:
        doors.append((u16(d, o), d[o + 2]))
        o += 3
    m["doors"] = doors
    # edge exits: no terminator; town.bin scans for the record with bit0 set (left) / clear (right)
    o = m["ptrs"]["exits"] - BASE
    m["exits"] = [tuple(d[o + 4 * i:o + 4 * i + 4]) for i in range((m["ptrs"]["doors"] - m["ptrs"]["exits"]) // 4)]
    m["caves"] = []
    if m["ptrs"]["caves"]:
        o, n = m["ptrs"]["caves"] - BASE, 1 + max([dd - 8 for _, dd in doors if 8 <= dd < 0xFF] +
                                                   [e[1] for e in m["exits"] if e[0] & 0x80] + [-1])
        m["caves"] = [(u16(d, o + 5 * i), d[o + 5 * i + 2], d[o + 5 * i + 3], d[o + 5 * i + 4]) for i in range(n)]
    o, npcs = m["ptrs"]["npcs"] - BASE, []
    while u16(d, o) != 0xFFFF:
        r = d[o:o + 8]
        npcs.append({"col": u16(r, 0), "sprite": r[2] & 0x7F, "left": bool(r[2] & 0x80), "anim": r[4],
                     "type": r[5], "flags": r[6], "script": r[7]})
        o += 8
    m["npcs"] = npcs
    o = m["ptrs"]["range"] - BASE
    m["range"] = (u16(d, o), u16(d, o + 2))
    o, pl = m["ptrs"]["patches"] - BASE, []
    while u16(d, o) != 0xFFFF:
        fp, mask, o, pokes = u16(d, o), d[o + 2], o + 3, []
        while u16(d, o) != 0xFFFF:
            pokes.append((u16(d, o), d[o + 2]))
            o += 3
        o += 2
        pl.append((fp, mask, pokes))
    m["patches"] = pl
    o, dlg = m["ptrs"]["dialogue"] - BASE, []
    while True:
        p = u16(d, o + 2 * len(dlg))
        if not (BASE + 0x17 <= p < BASE + len(d)) or len(dlg) > 40:
            break
        dlg.append(d[p - BASE:d.index(b"\xff", p - BASE)])
    m["dialogue"] = dlg
    return m


def decode48_town(c, typ):
    """Town tile: 8 rows x 3 BE words; `typ` says which word is the sky mask (gtmcga @3B4B table).
    Returns (pixels 8x8 or None, sky 8x8 bools per pixel PAIR as in the driver: both bits set)."""
    px, sky = [], []
    for row in range(8):
        w = [(c[row * 6 + 2 * i] << 8) | c[row * 6 + 2 * i + 1] for i in range(3)]
        a, b, cc, mk = w[0], w[1], w[2], 0
        if typ == 1:
            cc, mk = 0, w[2]
        elif typ == 2:
            b, mk = 0, w[1]
        elif typ == 3:
            a, mk = 0, w[0]
        elif typ == 4:
            mk = 0xFFFF
        p = [(((cc >> (15 - x)) & 1) << 2) | (((b >> (15 - x)) & 1) << 1) | ((a >> (15 - x)) & 1)
             for x in range(16)]
        px.append([MCGA[(p[2 * k] << 3) | p[2 * k + 1]] for k in range(8)])
        sky.append([((mk >> (14 - 2 * k)) & 3) == 3 for k in range(8)])
    return px, sky


def decode48_sprite(c):
    """NPC/hero sprite cell as converted by gtmcga @3A71: colour 0 transparent, white -> black."""
    px = []
    for row in range(8):
        r = c[row * 6:row * 6 + 6]
        a, b, cc = (r[0] << 8) | r[1], (r[2] << 8) | r[3], (r[4] << 8) | r[5]
        p = [(((cc >> (15 - x)) & 1) << 2) | (((b >> (15 - x)) & 1) << 1) | ((a >> (15 - x)) & 1)
             for x in range(16)]
        out = []
        for k in range(8):
            l, r_ = p[2 * k], p[2 * k + 1]
            if l == 0 and r_ == 0:
                out.append(None)          # mask bit: pair fully transparent (@3C76)
            else:
                out.append(MCGA[((0 if l == 7 else l) << 3) | (0 if r_ == 7 else r_)])
        px.append(out)
    return px


def load_town_bank(idx):
    d = open(os.path.join(ROOT, "extracted", "ZELRES2", "dec", f"{TOWN_BANKS[idx]:03d}_data.dec"), "rb").read()
    off_block, off_anim = u16(d, 2), u16(d, 4)
    types = d[6:off_block]
    n = (len(d) - 0x100) // 48
    cells = [decode48_town(d[0x100 + i * 48:0x100 + (i + 1) * 48], types[i] if i < len(types) and types[i] < 5 else 0)
             for i in range(n)]
    block = list(d[off_block + 1:off_block + 1 + d[off_block]])
    anim = [(d[off_anim + 1 + 2 * i], d[off_anim + 2 + 2 * i]) for i in range(d[off_anim])] if off_anim < 0x100 else []
    return cells, block, anim


def load_npc_bank(idx):
    d = open(os.path.join(ROOT, "extracted", "ZELRES2", "dec", f"{TOWN_NPC_BANKS[idx]:03d}_data.dec"), "rb").read()
    frames = [[list(d[s * 48 + f * 6:s * 48 + f * 6 + 6]) for f in range(8)] for s in range(5)]
    cells = [decode48_sprite(d[0x100 + i * 48:0x100 + (i + 1) * 48]) for i in range((len(d) - 0x100) // 48)]
    return frames, cells


def render_town(m, scale=2, objects=True):
    cells, block, _ = load_town_bank(m["tileset"])
    frames, sprites = load_npc_bank(m["gfx"])
    w, h, top = m["width"] * 8, TOWN_ROWS * 8, 12
    im = Image.new("RGB", (w, h + top + 12), BG)
    dr = ImageDraw.Draw(im)
    dr.rectangle([0, top, w - 1, top + 23], fill=SKY)
    for x, col in enumerate(m["cols"]):
        for y, v in enumerate(col):
            if v >= len(cells):
                dr.rectangle([x * 8, top + y * 8, x * 8 + 7, top + y * 8 + 7], fill=(255, 0, 255))
                continue
            px, sky = cells[v]
            for yy in range(8):
                for xx in range(8):
                    if y < 3 and sky[yy][xx]:      # gtmcga @31D8: rows 0-2 blend with the backdrop
                        continue
                    im.putpixel((x * 8 + xx, top + y * 8 + yy), px[yy][xx])
    if objects:
        for n in m["npcs"]:                      # NPC sprite, frame 0 of the facing side (@34EC)
            fr = frames[n["sprite"]][0 if n["left"] else 4] if n["sprite"] < 5 else None
            if fr:
                for i, ci in enumerate(fr):
                    if ci and ci - 1 < len(sprites):
                        blit(im, sprites[ci - 1], (n["col"] + i // 3) * 8, top + (5 + i % 3) * 8)
        for x, v in enumerate(m["cols"]):
            if v[7] in block:                    # town.bin 686E: ground cells in the block list stop the hero
                dr.line([x * 8, top + 63, x * 8 + 7, top + 63], fill=(255, 0, 0))
    im = im.resize((im.width * scale, im.height * scale), Image.NEAREST)
    if objects:
        dr = ImageDraw.Draw(im)
        dr.text((2, 0), f"{m['label']}  (region {m['map_id']}, {['cpat', 'mpat', 'dpat'][m['tileset']]})", fill=(255, 255, 255))
        for col, dest in m["doors"]:
            x = col * 8 * scale
            name = SHOPS[dest] if dest < 8 else "past" if dest == 0xFF else f"cave{dest - 8}"
            dr.rectangle([x, top * scale, x + 8 * scale - 1, (top + 64) * scale - 1], outline=(255, 255, 0))
            dr.text((x - 8, (top + 64) * scale), name, fill=(255, 255, 0))
        for n in m["npcs"]:
            x, y = n["col"] * 8 * scale, (top + 40) * scale
            dr.rectangle([x, y, x + 16 * scale - 1, y + 24 * scale - 1], outline=(0, 255, 255))
            dr.text((x, y - 10), f"n{n['script']}", fill=(0, 255, 255))
    return im


def text_grid_town(m):
    sym = lambda v: "." if v == 0 else chr(48 + v) if v < 10 else chr(55 + v) if v < 36 else chr(61 + v) if v < 62 else "#"  # noqa
    return "\n".join("".join(sym(m["cols"][x][y]) for x in range(m["width"])) for y in range(TOWN_ROWS))


def info_town(m, name):
    p = m["ptrs"]
    print(f"{name}: town, region {m['map_id']} '{m['label']}'  width {m['width']}  tileset {['cpat', 'mpat', 'dpat'][m['tileset']]}"
          f"  npc gfx {['mman', 'cman'][m['gfx']]}  music {TOWN_MUSIC[m['music']] if m['music'] < 4 else m['music']}"
          f"  town_flags {m['town_flags']:#04x}  C013 {m['w13']:#x}")
    print("  ptrs: " + " ".join(f"{k}={v:04X}" for k, v in p.items()))
    print("  exits (flags,dest,gfx,tileset):", " ".join("(" + ",".join(f"{b:02x}" for b in e) + ")" for e in m["exits"]))
    print("  doors (col,dest):", " ".join(f"({c},{SHOPS[d] if d < 8 else 'past' if d == 0xFF else f'cave{d - 8}'})" for c, d in m["doors"]))
    print("  cavern entries (col,row,side,map):", " ".join(f"({c},{r},{s},{mp:#04x})" for c, r, s, mp in m["caves"]))
    print(f"  {len(m['npcs'])} NPCs (col sprite facing type flags script):", " ".join(
        f"({n['col']},{n['sprite']},{'L' if n['left'] else 'R'},{NPC_TYPES[n['type']]},{n['flags']:02x},#{n['script']})"
        for n in m["npcs"]), f" walk range {m['range']}")
    for fp, mask, pokes in m["patches"]:
        print(f"  patch: if [{fp:04X}]&{mask:02X}: " + " ".join(f"[{a:04X}]={v:02X}" for a, v in pokes))
    print(f"  {len(m['dialogue'])} dialogue strings")


def name_of(path):
    mm = re.search(r"(ZELRES\d)/(?:dec/)?(\d{3})_", path)
    return resource_names().get((mm.group(1), int(mm.group(2)))) if mm else os.path.basename(path)


def main():
    args = sys.argv[1:]

    def opt(name, default):
        return int(args[args.index(name) + 1], 0) if name in args else default

    scale, objects = opt("--scale", 2), "--no-objects" not in args
    if args and args[0] in ("--all", "--town"):
        outdir = args[1]
        os.makedirs(outdir, exist_ok=True)
        jobs = [] if args[0] == "--town" else [("ZELRES3", i) for i in range(20, 51)]
        jobs += [("ZELRES2", i) for i in range(36, 46)]
        for arc, idx in jobs:
            path = os.path.join(ROOT, "extracted", arc, "dec", f"{idx:03d}_data.dec")
            if not os.path.exists(path):
                continue
            name = name_of(path) or f"{arc.lower()}_{idx}"
            d = open(path, "rb").read()
            out = os.path.join(outdir, name.replace(".mdt", ".png"))
            if is_town(d):
                m = parse_town(d)
                render_town(m, scale, objects).save(out)
                info_town(m, name)
            else:
                m = parse(d)
                render(m, scale, objects).save(out)
                info(m, name)
        return
    src = args[0]
    d = open(src, "rb").read()
    if is_town(d):
        m = parse_town(d)
        if "--text" in args:
            print(text_grid_town(m))
            return
        if "--info" in args:
            info_town(m, name_of(src))
        if len(args) > 1 and not args[1].startswith("--"):
            render_town(m, scale, objects).save(args[1])
            print(f"{args[1]}: {name_of(src)} town map '{m['label']}' {m['width']}x{TOWN_ROWS} cells,"
                  f" tileset {['cpat', 'mpat', 'dpat'][m['tileset']]}")
        return
    m = parse(d)
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
