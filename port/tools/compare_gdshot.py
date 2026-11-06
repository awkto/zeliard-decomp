#!/usr/bin/env python3
"""Compare a DOSBox capture of a cutscene (port/tools/capture_video_mode.sh) with the
port's own frame in the same --video mode, and find the frame that matches best.

The capture is taken at a wall-clock second, the port is deterministic by frame, so the
two clocks are lined up by sweeping: for each candidate frame the port renders the
cutscene in MODE and the percentage of identical pixels over the whole screen is
reported.  The best frame is what the capture was showing.

usage: compare_gdshot.py MODE ACT CAPTURE.png LO HI [STEP]   [--zeliard PATH] [--dir GAMEDIR] [--ending]
       compare_gdshot.py MODE ACT CAPTURE.png FRAME --diff OUT.png
"""
import numpy as np
import os, subprocess, sys, tempfile
from concurrent.futures import ThreadPoolExecutor
from PIL import Image, ImageChops

def arg(name, default=None):
    return sys.argv[sys.argv.index(name) + 1] if name in sys.argv else default

HERE = os.path.dirname(os.path.abspath(__file__))
ZEL = arg("--zeliard", os.path.join(HERE, "..", "zeliard"))
DIR = arg("--dir", os.path.join(HERE, "..", "..", "zeliard"))

def render(mode, act, frame, path):
    cmd = [ZEL, "--dir", DIR, "--video", mode]
    cmd += ["--ending"] if "--ending" in sys.argv else ["--intro-act", str(act)]
    cmd += ["--screenshot", str(frame), path]
    subprocess.run(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=120)
    return Image.open(path).convert("RGB")

def load_capture(p, size):
    b = Image.open(p).convert("RGB")
    if b.size != size:
        sx, sy = b.width // size[0], b.height // size[1]
        if sx >= 1 and sy >= 1 and sx * size[0] == b.width and sy * size[1] == b.height:
            b = b.resize(size, Image.NEAREST)
    return b

def match(a, b):
    if a.size != b.size:
        return -1.0
    x = np.asarray(a, dtype=np.int16) != np.asarray(b, dtype=np.int16)
    return 100.0 * (1.0 - x.any(axis=2).mean())

def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    for opt in ("--zeliard", "--dir", "--diff"):
        if opt in sys.argv:
            args.remove(sys.argv[sys.argv.index(opt) + 1])
    mode, act, cap = args[0], int(args[1]), args[2]
    tmp = tempfile.mkdtemp()
    if len(args) == 4:                       # one frame (+ optional diff image)
        a = render(mode, act, int(args[3]), os.path.join(tmp, "p.png"))
        b = load_capture(cap, a.size)
        print("%.2f%%" % match(a, b))
        out = arg("--diff")
        if out:
            ImageChops.difference(a, b).point(lambda v: 255 if v else 0).save(out)
        return
    lo, hi = int(args[3]), int(args[4])
    step = int(args[5]) if len(args) > 5 else 4
    frames = list(range(lo, hi + 1, step))
    def one(f):
        a = render(mode, act, f, os.path.join(tmp, "p%d.png" % f))
        return f, match(a, load_capture(cap, a.size))
    with ThreadPoolExecutor(max_workers=4) as ex:
        res = sorted(ex.map(one, frames), key=lambda r: -r[1])
    for f, m in res[:3]:
        print("frame %d  %.2f%%" % (f, m))
main()
