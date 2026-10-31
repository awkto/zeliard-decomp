#!/usr/bin/env python3
"""tools/harness/contact_sheet.py ORACLE_DIR PORT_DIR --frame N [--span K] --out sheet.png

One image for triage (docs/HARNESS.md §8): a row per frame N-K..N+K, columns
oracle | port | diff (changed pixels in red over a dimmed oracle frame), 2x
scaled, each row captioned with the frame number, the site, the input and the
names of the fields that differ in that frame (decoded by tools/tracediff.py
through tools/harness/schema.py, with the allow-list applied).

Needs frame_NNNNN.png for every row in BOTH dirs, i.e. traces made with
`--png-every 1` (oracle: tools/oracle.sh ... --png-every 1; port: zeliard
--trace DIR ... --png-every 1; or run_scenarios.sh --png).  Rows whose frame
does not exist in the traces (wait-loop traces have gaps) are skipped; a frame
with no PNG is an error that prints the commands to rerun.

Needs Pillow: present on the claude-test droplet; if pro lacks it, run it there.
Exit: 0 written, 2 bad input / missing PNGs."""
import argparse
import json
import mmap
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.dirname(HERE))       # tools/  -> tracediff
sys.path.insert(0, HERE)                        # schema
import tracediff  # noqa: E402
from schema import Schema, DEFAULT_SCHEMA  # noqa: E402

SCALE = 2


def die(msg):
    print("contact_sheet: " + msg, file=sys.stderr)
    sys.exit(2)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("oracle_dir")
    ap.add_argument("port_dir")
    ap.add_argument("--frame", type=int, required=True, help="centre frame (usually the first divergence)")
    ap.add_argument("--span", type=int, default=2, help="rows on each side of the centre (default 2)")
    ap.add_argument("--out", required=True, help="output PNG")
    ap.add_argument("--schema", default=DEFAULT_SCHEMA)
    ap.add_argument("--allow", default=tracediff.DEFAULT_ALLOW)
    ap.add_argument("--max-names", type=int, default=6, help="field names listed per caption (default 6)")
    a = ap.parse_args()
    try:
        from PIL import Image, ImageChops, ImageDraw
    except ImportError:
        die("Pillow is not installed here; run this on the claude-test droplet (docs/HARNESS.md §8)")

    sch = Schema(a.schema)
    rules = tracediff.load_allow(a.allow)
    port = tracediff.load_jsonl(os.path.join(a.port_dir, "frames.jsonl"))
    orac = tracediff.load_jsonl(os.path.join(a.oracle_dir, "frames.jsonl"))
    common = sorted(set(port) & set(orac))
    if not common:
        die("no aligned frames in %s and %s" % (a.oracle_dir, a.port_dir))
    # the span is taken in frame numbers; rows are the aligned frames inside it
    rows = [f for f in common if a.frame - a.span <= f <= a.frame + a.span]
    if not rows:
        die("no aligned frame within %d of %d (trace has %d..%d)" % (a.span, a.frame, common[0], common[-1]))

    miss = []
    modes = tracediff.site_modes(port)
    for f in rows:
        for d in (a.oracle_dir, a.port_dir):
            if not os.path.exists(os.path.join(d, "frame_%05d.png" % f)):
                miss.append((d, f))
    if miss:
        print("contact_sheet: %d PNG(s) missing, e.g. %s/frame_%05d.png" % (len(miss), miss[0][0], miss[0][1]), file=sys.stderr)
        print("  The traces need --png-every 1. Rerun the scenario with PNGs:\n"
              "    tools/harness/run_scenarios.sh --png NAME            (oracle + port, all frames)\n"
              "  or by hand:\n"
              "    tools/oracle.sh \"$SCRIPT\" --load SAVE --out %s --png-every 1\n"
              "    port/zeliard --dir zeliard --load SAVE --trace %s --script \"$SCRIPT\" --png-every 1"
              % (a.oracle_dir, a.port_dir), file=sys.stderr)
        sys.exit(2)

    bpath = os.path.join(a.oracle_dir, "base.bin")
    mm = None
    if os.path.exists(bpath) and os.path.getsize(bpath):
        bf = open(bpath, "rb")
        mm = mmap.mmap(bf.fileno(), 0, access=mmap.ACCESS_READ)
    members = tracediff.modelled_members(sch, port)

    cells = []                                  # (frame, o, p, diff, caption, npix)
    for f in rows:
        o = Image.open(os.path.join(a.oracle_dir, "frame_%05d.png" % f)).convert("RGB")
        p = Image.open(os.path.join(a.port_dir, "frame_%05d.png" % f)).convert("RGB")
        if o.size != p.size:
            o = o.resize(p.size, Image.NEAREST)
        mask = ImageChops.difference(o, p).convert("L").point(lambda v: 255 if v else 0)
        diff = Image.blend(o, Image.new("RGB", o.size), 0.6)
        diff.paste((255, 0, 0), mask=mask)
        base = mm[f * tracediff.FRAME:(f + 1) * tracediff.FRAME] if mm is not None else None
        diffs = tracediff.compare_frame(sch, rules, orac[f], port[f], base, members, modes[f])
        npix = sum(1 for v in mask.getdata() if v)
        names = [n for n, _, _ in diffs]
        shown = ", ".join(names[:a.max_names]) + (" +%d more" % (len(names) - a.max_names) if len(names) > a.max_names else "")
        so, sp = orac[f].get("site"), port[f].get("site")
        cap = "frame %d%s  site %s  input %r  |  %s  |  %d px differ" % (
            f, " <== centre" if f == a.frame else "", sp if so == sp else "oracle %s / port %s" % (so, sp), port[f].get("input"),
            ("fields: " + shown) if names else "fields: none differ", npix)
        cells.append((f, o, p, diff, cap, bool(names) or npix > 0))

    w, h = cells[0][1].size
    w, h = w * SCALE, h * SCALE
    gap, cap_h = 6, 18
    heads = ("oracle", "port", "diff (red = changed)")
    W = 3 * w + 2 * gap
    H = 18 + len(cells) * (cap_h + h + gap)
    sheet = Image.new("RGB", (W, H), (30, 30, 30))
    dr = ImageDraw.Draw(sheet)
    for i, t in enumerate(heads):
        dr.text((i * (w + gap) + 4, 3), t, fill=(200, 200, 200))
    y = 18
    for f, o, p, diff, cap, bad in cells:
        dr.text((4, y + 3), cap, fill=(255, 120, 120) if bad else (150, 220, 150))
        y += cap_h
        for i, im in enumerate((o, p, diff)):
            sheet.paste(im.resize((w, h), Image.NEAREST), (i * (w + gap), y))
        y += h + gap
    os.makedirs(os.path.dirname(os.path.abspath(a.out)), exist_ok=True)
    sheet.save(a.out)
    print("wrote %s (%dx%d, %d rows: frames %s)" % (a.out, W, H, len(cells), ",".join(str(c[0]) for c in cells)))
    for c in cells:
        print("  " + c[4])
    return 0


if __name__ == "__main__":
    sys.exit(main())
