#!/usr/bin/env python3
"""tools/harness/port2base.py PORT_DIR OUT_DIR [--flip F:ADDR] [--poke F:ADDR:VAL]

Build a synthetic *oracle* directory from a port trace (issue #48): for every
line of PORT_DIR/frames.jsonl, encode its `fields` through the schema into a
64 KB BASE image (unknown bytes stay 0, the [C010] object list is laid out at
D000) and write OUT_DIR/frames.jsonl (oracle format, no `fields`) + base.bin.
PNGs are copied.  This exercises tracediff's decode path end to end; --flip
xors one byte (F = trace frame, ADDR = BASE offset) to plant a divergence."""
import argparse
import json
import os
import shutil
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from schema import Schema, SITE_MODE, DEFAULT_SCHEMA  # noqa: E402


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("port_dir")
    ap.add_argument("out_dir")
    ap.add_argument("--schema", default=DEFAULT_SCHEMA)
    ap.add_argument("--flip", action="append", default=[], metavar="F:ADDR", help="xor 0x01 into BASE:ADDR of frame F")
    ap.add_argument("--poke", action="append", default=[], metavar="F:ADDR:VAL", help="set byte BASE:ADDR of frame F")
    ap.add_argument("--no-png", action="store_true")
    a = ap.parse_args()
    sch = Schema(a.schema)
    edits = {}
    for s in a.flip:
        f, addr = s.split(":")
        edits.setdefault(int(f, 0), []).append((int(addr, 16), None))
    for s in a.poke:
        f, addr, val = s.split(":")
        edits.setdefault(int(f, 0), []).append((int(addr, 16), int(val, 0)))
    os.makedirs(a.out_dir, exist_ok=True)
    n = 0
    with open(os.path.join(a.port_dir, "frames.jsonl")) as src, \
         open(os.path.join(a.out_dir, "frames.jsonl"), "w") as fj, \
         open(os.path.join(a.out_dir, "base.bin"), "wb") as fb:
        for line in src:
            d = json.loads(line)
            if d["f"] != n:
                sys.exit("port2base: frame %d where %d was expected (base.bin is indexed by f)" % (d["f"], n))
            base = bytearray(0x10000)
            sch.encode(d.get("fields", {}), SITE_MODE.get(d["site"], "any"), base)
            for addr, val in edits.get(n, []):
                base[addr & 0xFFFF] = (base[addr & 0xFFFF] ^ 1) if val is None else (val & 0xFF)
            fb.write(base)
            fj.write(json.dumps({"f": d["f"], "site": d["site"], "base": "0x0000",
                                 "rng_calls": d["rng_calls"], "input": d["input"]}) + "\n")
            n += 1
    if not a.no_png:
        for name in os.listdir(a.port_dir):
            if name.startswith("frame_") and name.endswith(".png"):
                shutil.copyfile(os.path.join(a.port_dir, name), os.path.join(a.out_dir, name))
    print("port2base: %d frames -> %s" % (n, a.out_dir))


if __name__ == "__main__":
    main()
