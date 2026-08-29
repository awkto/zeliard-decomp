#!/usr/bin/env python3
"""Diff the port's C gd decoder against tools/grp2png.py's reference one.

`port/zeliard --gd-art NAME OUT.png` renders one intro/ending resource exactly
the way `tools/grp2png.py`'s `render_gd()` does (the decoder that produced
docs/screenshots/intro_art.png): the two unpackers, the plane -> bit-weight map
of the gdmcga entry point the demo calls, the 4-bit pixel pairing and the
16x16 additive blend palette.  Every one of the 31 `gd` resources and every one
of their sub-images must come out identical.

usage: compare_gdart.py [GAMEDIR]      (run from port/)
"""
import glob
import os
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))   # port/
TOOLS = os.path.join(os.path.dirname(ROOT), "tools")
sys.path.insert(0, TOOLS)

from PIL import Image                                    # noqa: E402
import grp2png as G                                      # noqa: E402

GAMEDIR = sys.argv[1] if len(sys.argv) > 1 else "../zeliard"
OUT = "/tmp/zel_gdart.png"


def main():
    names = {}
    for (arc, idx), nm in G.resource_names().items():
        names.setdefault(nm, (arc, idx))
    ok = bad = 0
    # hou.grp is the one entry where the two tables disagree on purpose: the
    # gdmcga sprite table at 3617 is {u16 ptr, u8 rows, u8 wbytes} (CX is read
    # as the word at +2, so CL = rows), which makes its frames 6 x 32 and
    # 4 x 24, not the 32 x 6 / 24 x 4 that tools/grp2png.py and
    # docs/CUTSCENES.md still have.  See the port README.
    for name in G.GD_ART:
        if name == "hou.grp":
            print("  note hou.grp: geometry corrected in port/gd.c (see 3617); not diffed")
            continue
        arc, idx = names[name]
        r = subprocess.run([os.path.join(ROOT, "zeliard"), "--dir", GAMEDIR,
                            "--gd-art", name, OUT], capture_output=True)
        if r.returncode:
            print(f"  FAIL {name}: {r.stderr.decode().strip()[:80]}")
            bad += 1
            continue
        hits = glob.glob(os.path.join(os.path.dirname(ROOT), "extracted", arc, "dec", f"{idx:03d}_*.dec"))
        if not hits:
            print(f"  SKIP {name}: no extracted/{arc}/dec/{idx:03d}_*.dec")
            continue
        data = open(hits[0], "rb").read()
        ref = G.render_gd(data, name, 1)
        got = Image.open(OUT).convert("RGB")
        if got.size != ref.size or got.tobytes() != ref.tobytes():
            print(f"  FAIL {name}: {got.size} vs {ref.size}")
            bad += 1
        else:
            ok += 1
    print(f"gd art: {ok}/{ok + bad} resources render identically to tools/grp2png.py")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
