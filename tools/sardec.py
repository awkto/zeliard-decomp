#!/usr/bin/env python3
"""Decompressor for Zeliard .SAR resource payloads.

Faithful port of the 8-opcode RLE engine in STICK.BIN @ 0x0D9D-0x0F16
(dispatch: first stream byte & 7 -> handler table at 0x0DBC).

Payload containers (kernel service [0x10C], AL modes):
  AL=2 "load+decompress":
      byte0 == 0  -> payload[1:] is one compressed stream
      byte0 != 0  -> {u8 flag, u16 lenA, u16 lenB} + streamA + streamB
                     (video-mode variants: mode 0 uses A, others use B)
  AL=3 "raw": stored as-is; code overlays start with entry-pointer word(s)
  AL=5 "raw, MT-32 variants": {u16 lenA, u16 lenB} + blobA + blobB
"""
import struct
import sys
from pathlib import Path


class Stream:
    def __init__(self, data: bytes):
        self.d = data
        self.si = 0
        self.dx = len(data)  # remaining-byte counter, mirrors DX exactly

    def lodsb(self, count_dx=True) -> int:
        b = self.d[self.si]
        self.si += 1
        if count_dx:
            self.dx -= 1
        return b


def decompress(stream: bytes) -> bytes:
    s = Stream(stream)
    op = s.lodsb() & 7  # 0x0DAD: lodsb; dec dx; and al,7
    out = bytearray()

    if op == 0:  # 0x0DCC: rest is literal
        out += s.d[s.si:s.si + s.dx]
        return bytes(out)

    if op in (1, 3):  # 0x0DD1 / 0x0E34: lookup-table RLE (hi/lo nibble key)
        table_at = s.si
        while s.lodsb() != 0xFF:  # 0x0E08: skip {key,val} pairs to 0xFF
            s.lodsb()
        while s.dx > 0:
            b = s.lodsb(count_dx=False)
            cx, al = 1, b
            bp = table_at
            if op == 1:  # keys have low nibble 0; match on b & 0xF0
                while (s.d[bp] & 0x0F) == 0:
                    if s.d[bp] == (b & 0xF0):
                        cx, al = (b & 0x0F) + 2, s.d[bp + 1]
                        break
                    bp += 2
            else:  # op 3: keys have high nibble 0; match on b & 0x0F
                while (s.d[bp] & 0xF0) == 0:
                    if s.d[bp] == (b & 0x0F):
                        cx, al = (b >> 4) + 2, s.d[bp + 1]
                        break
                    bp += 2
            out += bytes([al]) * cx
            s.dx -= 1
        return bytes(out)

    if op in (2, 4):  # 0x0E13 / 0x0E73: marker-nibble RLE
        marker = s.lodsb()
        while s.dx > 0:
            b = s.lodsb(count_dx=False)
            if op == 2 and (b & 0xF0) == marker:
                cx = (b & 0x0F) + 3
                al = s.lodsb()
                out += bytes([al]) * cx
            elif op == 4 and (b & 0x0F) == marker:
                cx = (b >> 4) + 3
                al = s.lodsb()
                out += bytes([al]) * cx
            else:
                out.append(b)
            s.dx -= 1
        return bytes(out)

    if op == 5:  # 0x0E9C: doubled byte -> count follows
        while s.dx > 0:
            b = s.lodsb(count_dx=False)
            cx = 1
            if s.si < len(s.d) and s.d[s.si] == b:
                cx = s.d[s.si + 1] + 2
                s.si += 2
                s.dx -= 2
            out += bytes([b]) * cx
            s.dx -= 1
        return bytes(out)

    if op == 6:  # 0x0EBA: byte-keyed pair table to 0xFFFF; explicit count byte
        table_at = s.si
        while True:  # skip table incl. 0xFFFF terminator (dx -= 2 per word)
            w = struct.unpack_from("<H", s.d, s.si)[0]
            s.si += 2
            s.dx -= 2
            if w == 0xFFFF:
                break
        while s.dx > 0:
            b = s.lodsb(count_dx=False)
            cx, al = 1, b
            bp = table_at
            while struct.unpack_from("<H", s.d, bp)[0] != 0xFFFF:
                if s.d[bp] == b:
                    cx = s.lodsb() + 2  # count byte from stream (dec dx)
                    al = s.d[bp + 1]
                    break
                bp += 2
            out += bytes([al]) * cx
            s.dx -= 1
        return bytes(out)

    # op == 7, 0x0EF5: escape byte -> {value, count} follow
    esc = s.lodsb()
    while s.dx > 0:
        b = s.lodsb(count_dx=False)
        if b == esc:
            val = s.lodsb(count_dx=False)
            cnt = s.lodsb(count_dx=False)
            s.dx -= 2
            out += bytes([val]) * (cnt + 3)
        else:
            out.append(b)
        s.dx -= 1
    return bytes(out)


def parse_container(payload: bytes):
    """Return (kind, parts). kind: 'plain' | 'variant' | 'raw'."""
    if not payload:
        return "raw", [payload]
    if payload[0] == 0:
        return "plain", [payload[1:]]
    if len(payload) >= 5:
        la, lb = struct.unpack_from("<HH", payload, 1)
        if 5 + la + lb == len(payload):
            return "variant", [payload[5:5 + la], payload[5 + la:]]
    return "raw", [payload]


def main() -> None:
    root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("extracted")
    stats = {"plain": 0, "variant": 0, "raw": 0, "fail": 0}
    for f in sorted(root.glob("ZELRES*/[0-9]*.bin")):
        payload = f.read_bytes()
        kind, parts = parse_container(payload)
        outs, sizes = [], []
        try:
            for i, part in enumerate(parts):
                data = decompress(part) if kind != "raw" else part
                outs.append(data)
                sizes.append(len(data))
        except (IndexError, struct.error):
            stats["fail"] += 1
            print(f"{f.parent.name}/{f.name}: {kind} FAILED")
            continue
        stats[kind] += 1
        dest = f.parent / "dec"
        dest.mkdir(exist_ok=True)
        for i, data in enumerate(outs):
            suffix = ("", "_A", "_B")[i if len(outs) > 1 else 0]
            (dest / (f.stem + suffix + ".dec")).write_bytes(data)
        ratio = " + ".join(str(n) for n in sizes)
        print(f"{f.parent.name}/{f.name}: {kind:7s} {len(payload):6d} -> {ratio}")
    print(f"\ntotals: {stats}")


if __name__ == "__main__":
    main()
