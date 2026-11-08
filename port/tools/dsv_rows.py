#!/usr/bin/env python3
"""Per-row view of a mid-dissolve capture (#118): for every playfield row of CAPTURE.png, which of the
port's pictures (the town before, passes 1..8) it matches best.  The real game's passes take 30-50 ms,
so a capture usually lands *inside* a pass: the rows already done show pass n and the rest pass n-1,
which is what this makes visible.

usage: dsv_rows.py TRACE_DIR CAPTURE.png [--x0 48 --x1 272 --y0 14 --y1 158 --sx 1 --sy 1]
"""
import glob, os, sys
import numpy as np
from PIL import Image

def arg(n, d):
    return int(sys.argv[sys.argv.index(n) + 1]) if n in sys.argv else d

def load(p):
    return np.asarray(Image.open(p).convert("RGB"), dtype=np.int16)

tdir, cap = sys.argv[1], sys.argv[2]
x0, x1, y0, y1 = arg("--x0", 48), arg("--x1", 272), arg("--y0", 14), arg("--y1", 158)
cands = [("town", load(sorted(glob.glob(os.path.join(tdir, "frame_*.png")))[0]))]
cands += [("p%d" % (i + 1), load(p)) for i, p in enumerate(sorted(glob.glob(os.path.join(tdir, "dsv_*.png"))))]
b = load(cap)
if b.shape[0] == 2 * cands[0][1].shape[0] and b.shape[1] == cands[0][1].shape[1]:
    b = b[::2]                      # a line-doubled capture (EGA), sampled back down
out = []
for y in range(y0, y1):
    best = max(((100.0 * (1 - (a[y, x0:x1] != b[y, x0:x1]).any(axis=1).mean()), n) for n, a in cands))
    ties = [n for n, a in cands if (100.0 * (1 - (a[y, x0:x1] != b[y, x0:x1]).any(axis=1).mean())) >= best[0] - 0.01]
    out.append((y, best[0], ties))
runs = []
for y, m, t in out:
    key = "/".join(t) if len(t) < 4 else "any"
    if runs and runs[-1][2] == key: runs[-1][1] = y
    else: runs.append([y, y, key, m])
for a, z, k, m in runs:
    print("rows %3d-%3d  %-12s %.1f%%" % (a, z, k, m))

exact = sum(1 for y, m, t in out if m >= 99.99)
print("%d of %d rows identical to one of the pictures" % (exact, len(out)))
