#!/usr/bin/env python3
"""Match DOSBox captures taken while the intro's sand (gd_fx_sand) runs against the port.

The sand is the dither wipe the storm demo (act 3) draws over the picture window (x 16..303,
rows 16..119) between pictures; it lasts about 0.6 s of the real game, so a burst of captures
(port/tools/capture_video_mode.sh with a time every 0.25 s) catches it in several states.  Each
capture is compared with every frame the port renders in LO..HI (act 3, --video MODE) over the
window only, and the best frame and its identical-pixel percentage are printed.

usage: compare_sand.py MODE LO HI CAPTURE.png... [--zeliard PATH] [--dir GAMEDIR]
"""
import numpy as np, os, subprocess, sys, tempfile
from PIL import Image

def arg(name, default=None):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default

HERE = os.path.dirname(os.path.abspath(__file__))
ZEL = arg("--zeliard", os.path.join(HERE, "..", "zeliard"))
DIR = arg("--dir", os.path.join(HERE, "..", "..", "zeliard"))

# the window (16,16)-(304,120) in each mode's own pixels (x0, y0, x1, y1)
BOX = {"mcga": (16, 16, 304, 120), "cga": (16, 16, 304, 120), "tandy": (16, 16, 304, 120),
       "cga2": (32, 16, 608, 120), "ega": (32, 16, 608, 120), "hgc": (40 + 32, 58, 40 + 608, 211)}

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    for opt in ("--zeliard", "--dir"):
        if opt in sys.argv:
            args.remove(sys.argv[sys.argv.index(opt) + 1])
    mode, lo, hi, caps = args[0], int(args[1]), int(args[2]), args[3:]
    x0, y0, x1, y1 = BOX[mode]
    tmp = tempfile.mkdtemp()
    frames = {}
    for f in range(lo, hi + 1):
        p = os.path.join(tmp, "f%d.png" % f)
        subprocess.run([ZEL, "--dir", DIR, "--video", mode, "--intro-act", "3", "--screenshot", str(f), p],
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
        im = np.asarray(Image.open(p).convert("RGB"), dtype=np.int16)
        full = im.shape
        frames[f] = im[y0:y1, x0:x1]
    for c in caps:
        b = np.asarray(Image.open(c).convert("RGB"), dtype=np.int16)
        if b.shape[0] * b.shape[1] == 2 * full[0] * full[1] or b.shape[:2] == (full[0] * 2, full[1] * 2):
            b = b[::b.shape[0] // full[0], ::b.shape[1] // full[1]]       # a doubled capture, sampled back down
        b = b[y0:y1, x0:x1]
        best = max(((100.0 * (1 - (a != b).any(axis=2).mean()), f) for f, a in frames.items()))
        print("%-32s frame %d  %.2f%%" % (os.path.basename(c), best[1], best[0]))

main()
