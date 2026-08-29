#!/usr/bin/env python3
"""Compare a port screenshot (320x200) with a DOSBox capture from tools/run_dosbox.sh
(320x240: the 640x400 DOSBox window at y=40 downscaled 2:1, so game y = shot y - 20).

Reports the percentage of identical pixels over the visible playfield (x 48..271,
y 14..157) with the hero (3x3 cells at screen cell (12,10)) and the door arch
(5x4 cells from screen cell (11,9)) masked out, plus the per-row match.

With --box X Y W H the comparison is restricted to that rectangle of the port
image (game coordinates) and no masking is applied - used for the enemy-sprite
check (docs/screenshots/cavern_enemy.png).

usage: compare_shot.py PORT.png DOSBOX.png [--diff OUT.png] [--no-mask]
                       [--box X Y W H] [--label TEXT]
"""
import sys
from PIL import Image

def main():
    a = Image.open(sys.argv[1]).convert("RGB")
    b = Image.open(sys.argv[2]).convert("RGB")
    diff_out = sys.argv[sys.argv.index("--diff") + 1] if "--diff" in sys.argv else None
    mask = "--no-mask" not in sys.argv
    box = None
    if "--box" in sys.argv:
        i = sys.argv.index("--box")
        box = tuple(int(v) for v in sys.argv[i + 1:i + 5])
        mask = False
    label = sys.argv[sys.argv.index("--label") + 1] if "--label" in sys.argv else None
    oy = 20 if b.height == 240 else 0
    A, B = a.load(), b.load()
    x0, y0, w, h = box if box else (48, 14, 224, 144)
    hero = (48 + 12 * 8, 14 + 10 * 8, 24, 24)
    arch = (48 + 11 * 8, 14 + 9 * 8, 40, 32)
    def masked(x, y):
        for (mx, my, mw, mh) in (hero, arch):
            if mx <= x < mx + mw and my <= y < my + mh:
                return True
        return False
    diff = Image.new("RGB", (w, h), (0, 0, 0)) if diff_out else None
    tot = same = 0
    rows = []
    for y in range(y0, y0 + h):
        rt = rs = 0
        for x in range(x0, x0 + w):
            if mask and masked(x, y):
                continue
            rt += 1
            eq = A[x, y] == B[x, y + oy]
            rs += eq
            if diff and not eq:
                diff.putpixel((x - x0, y - y0), A[x, y])
        tot += rt; same += rs
        rows.append((y, rs, rt))
    what = label or ("playfield" if not box else f"box {x0},{y0} {w}x{h}")
    print(f"{what} match: {same}/{tot} = {100.0 * same / tot:.2f}% ({'hero+arch masked' if mask else 'unmasked'})")
    bad = [(y, s, t) for (y, s, t) in rows if s != t]
    if bad:
        print("rows with differences (y, same/total):", " ".join(f"{y}:{s}/{t}" for y, s, t in bad[:40]))
    if diff:
        diff.save(diff_out)
        print("diff (port pixels where they differ) written to", diff_out)

if __name__ == "__main__":
    main()
