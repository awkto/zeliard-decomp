#!/usr/bin/env python3
"""tools/harness/pixdiff.py ORACLE_DIR PORT_DIR [--scenario FILE.zs] -- per-frame pixel compare.

Compares frame_NNNNN.png of the two trace dirs for every frame that has a PNG on both
sides (run both with --png-every K).  Prints one machine-readable line first:
  PIXDIFF compared=N diff_frames=M first=F bbox=x0,y0,x1,y1 npix=P allowed=A
and exits 0 (pixels match) / 1 (they differ) / 2 (nothing to compare, or bad header).

Pixel allow rules come from the scenario's header lines
  # allow-px: x0,y0,x1,y1 [frames A-B] -- reason
(rectangle inclusive; "frames" optional, A-B or a single A; the reason is mandatory;
"--", " - " or an em dash separates it).  Allowed pixels are ignored before judging.
Reuses tracediff.png_frames / write_pngs for the side-by-side image of the first frame.
"""
import argparse, os, re, sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import tracediff
from PIL import Image, ImageChops

RULE = re.compile(r"^\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*,\s*(\d+)\s*(?:frames?\s+(\d+)(?:\s*-\s*(\d+))?)?\s*(?:—|--|\s-\s)\s*(\S.*)$")


def load_rules(path):
    rules = []
    for ln in open(path, errors="replace"):
        m = re.match(r"#\s*allow-px:(.*)$", ln)
        if not m:
            continue
        r = RULE.match(m.group(1).rstrip())
        if not r:
            sys.exit("pixdiff: %s: bad '# allow-px:' line (need x0,y0,x1,y1 [frames A-B] -- reason): %s" % (path, ln.strip()))
        x0, y0, x1, y1, fa, fb, why = r.groups()
        fa = int(fa) if fa else 0
        fb = int(fb) if fb else (fa if r.group(5) else 10**9)
        rules.append((int(x0), int(y0), int(x1), int(y1), fa, fb, why))
    return rules


def walk_frames(d):
    """The pictures that have no frame boundary (#117 walk-in, #119 side pictures), keyed
    (last boundary + 1, running index k, kind).  walk_<f>_<k>.png: the cave walk-in.
    side_<f>_<k>_<tag>.png: a timer wait or dissolve (the tag, e.g. w60F4 / dis7038, only labels
    the picture: the two sides are matched by f and k)."""
    out = {}
    for name in os.listdir(d):
        m = re.fullmatch(r"(walk|dsv)_(\d+)_(\d+)\.png", name)
        if m:
            out[(int(m.group(2)), int(m.group(3)), m.group(1))] = os.path.join(d, name)
            continue
        m = re.fullmatch(r"side_(\d+)_(\d+)_(\w+)\.png", name)
        if m:
            out[(int(m.group(1)), int(m.group(2)), "side")] = os.path.join(d, name)
    return out


def kdesc(k):
    return "%s %d,%d" % ({"walk": "walk-in picture", "dsv": "dissolve picture"}.get(k[2], "side picture"), k[0], k[1])


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("oracle_dir")
    ap.add_argument("port_dir")
    ap.add_argument("--scenario", help=".zs file whose '# allow-px:' lines apply")
    ap.add_argument("--out", help="dir for the first-diff image (default PORT_DIR/pixdiff)")
    a = ap.parse_args()
    rules = load_rules(a.scenario) if a.scenario else []
    op, pp = tracediff.png_frames(a.oracle_dir), tracediff.png_frames(a.port_dir)
    common = sorted(set(op) & set(pp))
    # the walk-in (fight.bin 7C6E) has no frame boundary: both sides write walk_<next boundary>_<k>.png
    # (#117), keyed here by (frame, k, "walk") and compared like frames; one side having more is a difference.
    # The screen dissolve (gmmcga 2130, #118) is the same: dsv_<next boundary>_<k>.png after each of its 8 passes
    wo, wp = walk_frames(a.oracle_dir), walk_frames(a.port_dir)
    walk_missing = sorted(set(wo) ^ set(wp))
    for k in set(wo) & set(wp):
        op[k], pp[k] = wo[k], wp[k]
        common.append(k)
    common.sort(key=lambda k: (k[0], k[1], k[2]) if isinstance(k, tuple) else (k, -1, ""))
    if not common:
        print("PIXDIFF compared=0")
        print("pixdiff: no frame has a PNG on both sides (oracle %d, port %d)" % (len(op), len(pp)))
        return 2
    first, nfr, bbox, npix, allowed = None, 0, None, 0, 0
    for f in common:
        o = Image.open(op[f]).convert("RGB")
        p = Image.open(pp[f]).convert("RGB")
        if o.size != p.size:
            o = o.resize(p.size, Image.NEAREST)
        d = ImageChops.difference(o, p).convert("L").point(lambda v: 255 if v else 0)
        for x0, y0, x1, y1, fa, fb, _ in rules:
            if fa <= (f[0] if isinstance(f, tuple) else f) <= fb:
                box = (x0, y0, x1 + 1, y1 + 1)
                allowed += sum(1 for v in d.crop(box).getdata() if v)
                d.paste(0, box)
        bb = d.getbbox()
        if not bb:
            continue
        nfr += 1
        n = sum(1 for v in d.getdata() if v)
        npix += n
        bbox = bb if bbox is None else (min(bbox[0], bb[0]), min(bbox[1], bb[1]), max(bbox[2], bb[2]), max(bbox[3], bb[3]))
        if first is None:
            first = f
            fbb = bb
    if first is None and walk_missing:
        print("PIXDIFF compared=%d diff_frames=%d first=%d,%d bbox=0,0,0,0 npix=0 allowed=%d"
              % (len(common), len(walk_missing), walk_missing[0][0], walk_missing[0][1], allowed))
        print("pixdiff: %d walk-in / side picture(s) on only one side (oracle %d, port %d), first %s on the %s side"
              % (len(walk_missing), len(wo), len(wp), kdesc(walk_missing[0]),
                 "oracle" if walk_missing[0] in wo else "port"))
        return 1
    if first is None:
        print("PIXDIFF compared=%d diff_frames=0 allowed=%d" % (len(common), allowed))
        print("pixdiff: %d frames identical (%d allowed pixels ignored)" % (len(common), allowed))
        return 0
    nfr += len(walk_missing)
    fdesc = "%d,%d" % first[:2] if isinstance(first, tuple) else "%d" % first
    print("PIXDIFF compared=%d diff_frames=%d first=%s bbox=%d,%d,%d,%d npix=%d allowed=%d"
          % (len(common), nfr, fdesc, bbox[0], bbox[1], bbox[2] - 1, bbox[3] - 1, npix, allowed))
    print("pixdiff: first diverging %s %s (bbox %s of that frame); images:"
          % (kdesc(first).rsplit(" ", 1)[0] if isinstance(first, tuple) else "frame", fdesc, fbb))
    out = a.out or os.path.join(a.port_dir, "pixdiff")
    if isinstance(first, tuple):
        os.makedirs(out, exist_ok=True)
        o = Image.open(op[first]).convert("RGB"); p = Image.open(pp[first]).convert("RGB")
        d = ImageChops.difference(o, p)
        im = Image.new("RGB", (o.width * 3, o.height))
        for i, x in enumerate((o, p, d)):
            im.paste(x, (i * o.width, 0))
        path = os.path.join(out, "%s_%05d_%03d.png" % (first[2], first[0], first[1]))
        im.save(path)
        print("  " + path)
    else:
        tracediff.write_pngs(a.oracle_dir, a.port_dir, first, 1, out)
    return 1


if __name__ == "__main__":
    sys.exit(main())
