#!/usr/bin/env python3
"""Match DOSBox captures taken during the shop-entry dissolve (#118) against the port's own
dissolve pictures in the same --video mode.

The port writes one picture per pass (dsv_*_K.png under --trace --png-every 1) plus the
town frame before and the shop's first frame after.  A capture is taken at an arbitrary
moment of the real game's dissolve, so each one is matched to whichever of those ten
pictures it is closest to; the report is the percentage of identical pixels over the
whole screen (and over the playfield rows 14..157 x 48..271, mapped to the mode's own
geometry by the caller's choice of --box).

usage: compare_dsv.py MODE TRACE_DIR CAPTURE.png... [--box X0 Y0 X1 Y1]
       (the box is in the capture's own pixels)
"""
import glob, os, sys
import numpy as np
from PIL import Image

def load(p):
    return np.asarray(Image.open(p).convert("RGB"), dtype=np.int16)

def main():
    args = [a for a in sys.argv[1:]]
    box = None
    if "--box" in args:
        i = args.index("--box")
        box = tuple(int(v) for v in args[i + 1:i + 5])
        del args[i:i + 5]
    mode, tdir, caps = args[0], args[1], args[2:]
    cands = {}
    town = sorted(glob.glob(os.path.join(tdir, "frame_*.png")))
    dsv = sorted(glob.glob(os.path.join(tdir, "dsv_*.png")))
    if town: cands["town"] = load(town[0])
    for i, p in enumerate(dsv): cands["pass%d" % (i + 1)] = load(p)
    if len(town) > 1:
        after = [p for p in town if p > dsv[0].replace("dsv_", "frame_")] if dsv else town[1:]
        if after: cands["shop"] = load(after[0])
    for c in caps:
        b = load(c)
        ref = next(iter(cands.values()))
        if b.shape[0] == 2 * ref.shape[0] and b.shape[1] == ref.shape[1]:
            b = b[::2]                  # a line-doubled capture (EGA), sampled back down
        res = []
        for k, a in cands.items():
            if a.shape != b.shape:
                res.append((-1, k)); continue
            aa, bb = a, b
            if box: aa, bb = a[box[1]:box[3], box[0]:box[2]], b[box[1]:box[3], box[0]:box[2]]
            res.append((100.0 * (1 - (aa != bb).any(axis=2).mean()), k))
        res.sort(reverse=True)
        print("%-40s best %s %.2f%%  next %s %.2f%%" % (os.path.basename(c), res[0][1], res[0][0], res[1][1], res[1][0]))

main()
