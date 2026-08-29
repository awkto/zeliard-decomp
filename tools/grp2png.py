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
  gd       intro/ending artwork (ttl1-3, end4-7, ame/hime/isi/oui/waku/...): a PC-88
           planar bitmap behind one of the demos' two unpackers, rendered through the
           gd* renderer's 256-entry blend palette (DAC[l*16+r] = C[l] + C[r], 16 base
           colours per record).  Each MCGA byte is a *pair* of PC-88 pixels, so the
           picture is half as wide on screen.  See docs/CUTSCENES.md.

usage: grp2png.py FILE OUT.png [--format F] [--skip N] [--cols N] [--scale N] [--pal N]
       grp2png.py --all OUTDIR          # every resource with a known format
Format is guessed from the resource name (docs/RESOURCES.md) when --format is omitted.
"""
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from palette import MCGA, gd_rgb8  # noqa: E402
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
    "roka.grp": ("cells48", 0), "dman.grp": ("cells32", 0),
    "king.grp": ("cells48", 0), "omoya.grp": ("cells48", 0), "armor.grp": ("cells48", 0),
    "bank.grp": ("cells48", 0), "church.grp": ("cells48", 0), "drug.grp": ("cells48", 0),
    "inn.grp": ("cells48", 0), "kenjya.grp": ("cells48", 0),
    "fman.grp": ("hero", 0), "font.grp": ("font", 6),
}
for _i in range(1, 9):
    FORMAT_BY_NAME[f"enp{_i}.grp"] = ("cells32", 0)



# ---------------------------------------------------------------------------
# gd* family — intro / ending artwork (docs/CUTSCENES.md)
#
# The demos load a .grp, run one of their own two unpackers over it and hand the
# result to gdmcga as a *planar* bitmap: `nplanes` consecutive planes of
# `wbytes * h` bytes, MSB = leftmost pixel, so every PC-88 pixel is `nplanes`
# bits.  gdmcga (@3032/3088/33B7/…) then packs each pair of adjacent PC-88
# pixels into one MCGA byte `left<<4 | right`, which is exactly a DAC index in
# the 256-entry blend palette (palette.gd_rgb8) — so the picture is half as wide
# on screen as it is in PC-88 pixels.
#
# Which bit-weight each plane gets is chosen by the *entry point* the demo calls,
# not by the file; the `mode` field below records the one the demo uses.


def gd_unpack_rle(d):
    """opdemo 6DE1 / enddemo 6939 — the packer used for ttl1-3.grp.

    Control byte b: bit 6 = 16-bit big-endian form (count = word & 0x3FFF,
    0xFFFF = end), else 6-bit count in b.  Bit 7 = run (one byte follows,
    repeated) vs. literal (count bytes follow).
    """
    out, i = bytearray(), 0
    while i < len(d):
        if d[i] & 0x40:
            v = (d[i] << 8) | d[i + 1]
            i += 2
            if v == 0xFFFF:
                break
            n = v & 0x3FFF
        else:
            n = d[i] & 0x3F
            v = d[i] << 8
            i += 1
        if v & 0x8000:
            out += bytes([d[i]]) * n
            i += 1
        else:
            out += d[i:i + n]
            i += n
    return bytes(out)


def gd_unpack_mask(d, delta=True):
    """opdemo 6D5E (= 6D63 + 6D8D) / enddemo 696D — everything except ttl1-3.

    `u16 nmask` then nmask bit-mask bytes then the payload: each mask bit (MSB
    first) emits one payload byte when set and a 0 byte when clear, so the
    output is nmask*8 bytes.  The result is then un-delta'd: the bytes carry
    2-bit fields XORed against the same field two pixels earlier (6D8D keeps
    the running value across the whole buffer), which turns PC-88 dither
    patterns into runs of zeros for the mask stage.
    """
    n = d[0] | (d[1] << 8)
    mp, dp = 2, 2 + n
    out = bytearray(n * 8)
    o = 0
    for k in range(n):
        m = d[mp + k]
        for b in range(8):
            if m & (0x80 >> b):
                out[o] = d[dp]
                dp += 1
            o += 1
    if not delta:
        return bytes(out)
    prev = 0
    for i, b in enumerate(out):
        r = 0
        for k in range(4):
            prev ^= (b >> (6 - 2 * k)) & 3
            r = (r << 2) | prev
        out[i] = r
    return bytes(out)


# plane bit-weights per gdmcga entry point (the "mode" column of GD_ART)
GD_MODES = {
    "p3":  [1, 2, 4],      # [0x3004] dissolve / [0x3010] direct — normal 3-plane art
    "p2h": [1, 8],         # [0x3002] — 2 planes as colours 0,1,8,9 (text overlay leaves 2,4 free)
    "p14": [1, 4],         # [0x3022] with AL=5 — plane mask
    "p1":  [1],           # single plane (fin.grp stencil, en72.grp plane)
    "p12": [1, 2],         # [0x3016] mouth-frame bank
    "p21": [2, 1],         # [0x3014] eye-frame bank (planes swapped)         # [0x3014]/[0x3016] — animation frame banks
    "spr": [1 | 4, 2],     # gdmcga 35CC sprite OR-draw — colours 0,5,2,7
    "ao":  "ao",           # [0x301A] — 2 planes -> 8 (both) / 10 (A) / 12 (B)
}

# name -> (unpacker, palette record, [(offset, wbytes, height, mode, count, stride)])
# Geometry is what the demo actually passes to gdmcga (CH = bytes per plane row
# = width/8 PC-88 px = width/4 MCGA px, CL = rows); see docs/CUTSCENES.md.
GD_ART = {
    # --- opdemo: prologue -------------------------------------------------
    "nec.grp":   ("mask", 2, [(0, 44, 104, "p3", 1, 0), (13728, 16, 64, "p3", 1, 0)]),
    # gdmcga's sprite table at 3617 is {u16 ptr, u8 rows, u8 wbytes} — 348E
    # reads CX as the *word at +2*, so CL (rows) is byte +2 and CH (the byte
    # width) is byte +3: four 6 x 32 frames 0x180 apart, then four 4 x 24
    # 0xC0 apart.  Decoded that way they are radiating orbs, not bolts.
    "hou.grp":   ("mask", 2, [(0, 6, 32, "spr", 4, 0x180), (0x600, 4, 24, "spr", 4, 0xC0)]),
    "dmaou.grp": ("mask", 3, [(0, 18, 32, "p12", 4, 0x480), (0x1380, 34, 48, "p21", 5, 0xCC0)]),
    # --- opdemo: title ----------------------------------------------------
    "ttl1.grp":  ("rle", 4, [(0, 49, 128, "p3", 1, 0)]),
    "ttl2.grp":  ("rle", 4, [(0, 40, 40, "p12", 1, 0)]),  # tile bank for [0x301C]
    "ttl3.grp":  ("rle", 4, [(0, 65, 112, "ao", 1, 0)]),
    # --- opdemo: the storm demo ------------------------------------------
    "waku.grp":  ("mask", 5, [(0, 80, 136, "p3", 1, 0)]),
    "ame.grp":   ("mask", 5, [(0, 72, 104, "p3", 1, 0)]),
    "hime.grp":  ("mask", 6, [(0, 72, 104, "p3", 1, 0)]),
    "isi.grp":   ("mask", 7, [(0, 72, 104, "p3", 1, 0)]),
    "oui.grp":   ("mask", 7, [(0, 72, 104, "p3", 1, 0)]),
    "sei.grp":   ("mask", 7, [(0, 36, 104, "p14", 1, 0)]),
    "yuu1.grp":  ("mask", 7, [(0, 72, 104, "p3", 1, 0)]),
    "yuu2.grp":  ("mask", 7, [(0, 49, 96, "p3", 1, 0)]),
    "yuu3.grp":  ("mask", 1, [(0, 64, 192, "p2h", 1, 0)]),
    "yuu4.grp":  ("mask", 1, [(0, 21, 160, "p3", 1, 0)]),
    "maop.grp":  ("mask", 8, [(0, 47, 88, "p3", 1, 0)]),
    # talking-head portraits: 96x88 picture, then mouth frames, then eye frames
    "yuup.grp":  ("mask", 6, [(0, 24, 88, "p3", 1, 0), (0x18C0, 9, 32, "p3", 6, 864),
                              (0x2D00, 11, 16, "p3", 6, 528)]),
    "oup.grp":   ("mask", 6, [(0, 24, 88, "p3", 1, 0), (0x18C0, 14, 32, "p3", 6, 1344),
                              (0x3840, 11, 16, "p3", 3, 528)]),
    # --- enddemo ----------------------------------------------------------
    # himp/seip carry their lip-sync banks past the portrait: enddemo's 0xBn
    # (6 mouths 9x24 stride 648 at 0x18C0, then 3 eyes 10x24 stride 720 at
    # 0x27F0) and 0x8n (one bank of 7x24 frames, stride 504, at 0x18C0).
    "himp.grp":  ("mask", 6, [(0, 24, 88, "p3", 1, 0), (0x18C0, 9, 24, "p3", 6, 648),
                              (0x27F0, 10, 24, "p3", 3, 720)]),
    "seip.grp":  ("mask", 6, [(0, 24, 88, "p3", 1, 0), (0x18C0, 7, 24, "p3", 6, 504)]),
    # new1: a 96x265 strip; enddemo 6A1E scrolls an 88-row window up through it
    "new1.grp":  ("mask", 6, [(0, 24, 265, "p3", 1, 0)]),
    "new2.grp":  ("mask", 7, [(0, 28, 100, "p3", 1, 0)]),
    "ne80.grp":  ("mask", 7, [(0, 26, 100, "p3", 1, 0)]),
    "ne81.grp":  ("mask", 7, [(0, 18, 81, "p3", 1, 0)]),
    "end5.grp":  ("mask", 7, [(0, 57, 154, "p3", 1, 0)]),
    "end4.grp":  ("mask", 7, [(0, 47, 114, "p3", 1, 0)]),
    "end6.grp":  ("mask", 7, [(0, 47, 114, "p3", 1, 0)]),
    # end7 has only TWO planes; en72.grp supplies the third (enddemo 68E5 copies it
    # over plane 2 at arena:93C0), so the demo picture is end7 + en72 combined.
    "end7.grp":  ("mask", 7, [(0, 80, 134, "p12", 1, 0)]),
    "fin.grp":   ("maskraw", 7, [(0, 38, 53, "p1", 2, 2014)]),
    "en72.grp":  ("raw", 7, [(0, 80, 134, "p1", 1, 0)]),
}

for _n in GD_ART:
    FORMAT_BY_NAME[_n] = ("gd", 0)


def gd_pixels(buf, off, wbytes, h, mode):
    """One sub-image -> list of rows of 4-bit PC-88 pixel values."""
    sz = wbytes * h
    weights = GD_MODES[mode]
    rows = []
    for y in range(h):
        row = []
        for xb in range(wbytes):
            if weights == "ao":
                a = buf[off + y * wbytes + xb] if off + y * wbytes + xb < len(buf) else 0
                b = buf[off + sz + y * wbytes + xb] if off + sz + y * wbytes + xb < len(buf) else 0
                for bit in range(8):
                    pa, pb = (a >> (7 - bit)) & 1, (b >> (7 - bit)) & 1
                    row.append(0 if not (pa | pb) else 8 if (pa & pb) else 12 if pb else 10)
            else:
                pl = []
                for i in range(len(weights)):
                    k = off + i * sz + y * wbytes + xb
                    pl.append(buf[k] if k < len(buf) else 0)
                for bit in range(8):
                    v = 0
                    for w, byte in zip(weights, pl):
                        if byte & (0x80 >> bit):
                            v |= w
                    row.append(v)
        rows.append(row)
    return rows


def render_gd(data, name, scale=2):
    unp, pal, parts = GD_ART[name]
    buf = {"rle": gd_unpack_rle, "mask": gd_unpack_mask,
           "maskraw": lambda d: gd_unpack_mask(d, delta=False),
           "raw": lambda d: d}[unp](data)
    dac = gd_rgb8(pal)
    tiles = []
    for off, wb, h, mode, count, stride in parts:
        for f in range(count):
            tiles.append(gd_pixels(buf, off + f * stride, wb, h, mode))
    w = max(len(t[0]) // 2 for t in tiles)
    total = sum(len(t) + 1 for t in tiles) - 1
    im = Image.new("RGB", (w, total), BG)
    y0 = 0
    for t in tiles:
        for y, row in enumerate(t):
            for x in range(len(row) // 2):
                im.putpixel((x, y0 + y), dac[row[2 * x] * 16 + row[2 * x + 1]])
        y0 += len(t) + 1
    return im.resize((im.width * scale, im.height * scale), Image.NEAREST)


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


def render(data, fmt, skip=0, cols=16, scale=3, pal=0, name=None):
    if fmt == "gd":
        return render_gd(data, name, max(1, scale - 1))
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
                path = os.path.join(ROOT, "extracted", arch, "dec", f"{idx:03d}_code.dec")
            if not os.path.exists(path):
                continue
            out = os.path.join(outdir, name.replace(".grp", ".png"))
            render(open(path, "rb").read(), fmt, skip, 16 if fmt != "hero" else 13,
                   scale, pal, name).save(out)
            print(f"{name:12s} {fmt:8s} -> {out}")
        return
    src, out = args[0], args[1]
    name = name_of(src)
    fmt = args[args.index("--format") + 1] if "--format" in args else FORMAT_BY_NAME.get(name, (None,))[0]
    if fmt is None:
        raise SystemExit(f"no known format for {src} ({name}); pass --format")
    skip = opt("--skip", FORMAT_BY_NAME.get(name, (None, 0))[1])
    render(open(src, "rb").read(), fmt, skip, opt("--cols", 16 if fmt != "hero" else 13),
           scale, pal, name).save(out)
    print(f"{out}: {name or src} as {fmt}")


if __name__ == "__main__":
    main()
