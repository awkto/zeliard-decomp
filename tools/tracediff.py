#!/usr/bin/env python3
"""tools/tracediff.py ORACLE_DIR PORT_DIR [--png] -- the harness diff (docs/HARNESS.md §4, #48).

ORACLE_DIR: frames.jsonl ({"f","site","base","rng_calls","input"} per boundary)
            + base.bin (65536 bytes of BASE per frame, frame f at f*65536)
            + optional frame_NNNNN.png
PORT_DIR:   `zeliard --trace DIR` output (frames.jsonl with "fields")

The oracle's BASE images are decoded through tools/harness/fields.tsv; every
field the port emitted is compared frame by frame (aligned on "f"), along with
site, input and rng_calls.  Fields in tools/harness/allow.tsv are skipped.
Prints the first diverging frame with field names and both values, a summary,
and with --png writes oracle | port | diff images around it.

Exit status: 0 clean, 1 divergence, 2 bad input (unknown field names, missing
files, malformed schema)."""
import argparse
import fnmatch
import json
import mmap
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "harness"))
from schema import Schema, site_modes, DEFAULT_SCHEMA  # noqa: E402

DEFAULT_ALLOW = os.path.join(os.path.dirname(os.path.abspath(__file__)), "harness", "allow.tsv")
FRAME = 0x10000
ABSENT = "<absent>"


def die(msg):
    print("tracediff: " + msg, file=sys.stderr)
    sys.exit(2)


def load_jsonl(path):
    out = {}
    try:
        with open(path) as fh:
            for ln, line in enumerate(fh, 1):
                if not line.strip():
                    continue
                try:
                    d = json.loads(line)
                except ValueError as e:
                    die("%s:%d: %s" % (path, ln, e))
                out[d["f"]] = d
    except OSError as e:
        die(str(e))
    return out


def load_allow(path):
    rules = []
    if not path or not os.path.exists(path):
        return rules
    with open(path) as fh:
        for ln, line in enumerate(fh, 1):
            if not line.strip() or line.lstrip().startswith("#"):
                continue
            cols = line.rstrip("\n").split("\t")
            pat = cols[0].strip()
            mode = cols[1].strip() if len(cols) > 1 and cols[1].strip() else "any"
            if mode not in ("fight", "town", "any"):
                die("%s:%d: bad mode %r" % (path, ln, mode))
            rules.append((pat, mode))
    return rules


def allowed(rules, name, mode):
    return any((m == "any" or m == mode) and fnmatch.fnmatchcase(name, p) for p, m in rules)


def fmt(v):
    if v is None or v == ABSENT:
        return ABSENT
    if isinstance(v, int):
        return "0x%X (%d)" % (v, v)
    if isinstance(v, str) and len(v) > 24:
        return "%s... (%d bytes)" % (v[:24], len(v) // 2)
    return repr(v) if not isinstance(v, str) else v


def hex_detail(name, ov, pv, limit=6):
    """for a long hex field: where the bytes differ"""
    if not (isinstance(ov, str) and isinstance(pv, str) and len(ov) == len(pv)):
        return ""
    try:
        ob, pb = bytes.fromhex(ov), bytes.fromhex(pv)
    except ValueError:                      # '<absent>', site names: not a hex blob
        return ""
    offs = [i for i in range(len(ob)) if ob[i] != pb[i]]
    if not offs:
        return ""
    parts = []
    for i in offs[:limit]:
        where = "+%X" % i
        if name == "ring":
            where += " (row %d col %d)" % divmod(i, 0x24)
        parts.append("%s o=%02X p=%02X" % (where, ob[i], pb[i]))
    more = " ..." if len(offs) > limit else ""
    return "      %d bytes differ: %s%s" % (len(offs), ", ".join(parts), more)


def modelled_members(sch, port):
    """{list: set(member)} compared record by record: what the port emits
    anywhere in its trace plus every schema member not noted "not modelled"
    (so an oracle-only record, or a live one the port never had, still shows)"""
    seen = {}
    for d in port.values():
        for n in d.get("fields", {}):
            m = re.fullmatch(r"(\w+)\[\d+\]\.(\w+)", n)
            if m:
                seen.setdefault(m.group(1), set()).add(m.group(2))
    for lname, L in sch.lists.items():
        s = seen.setdefault(lname, set())
        s.update(x[0] for x in L.members if not x[3].startswith("not modelled"))
        if L.dead:
            s.add("live")
    return seen


def compare_frame(sch, rules, od, pd, base, members, mode):
    """[(name, oracle, port)] for one aligned frame; `mode` = the port line's
    field mode (schema.site_modes)"""
    diffs = []
    if od.get("site") != pd.get("site"):
        diffs.append(("site", od.get("site"), pd.get("site")))
        return diffs                            # different loops: fields are not comparable
    if mode is None:
        return diffs
    for k in ("input", "rng_calls"):
        if od.get(k) != pd.get(k) and not allowed(rules, k, mode):
            diffs.append((k, od.get(k), pd.get(k)))
    ofields = sch.decode(base, mode) if base is not None else od.get("fields", {})
    pfields = pd.get("fields", {})
    names = [n for n in pfields if sch.mode_of(n) in ("any", mode) and Schema.list_of(n) not in sch.lists]
    # Record lists the port models: the count and (dead= lists) each record's
    # liveness are compared; a record's members only while it is live on BOTH
    # sides.  A dead or past-the-end record holds whatever the last occupant
    # left (or memory never written), which is not state -- and a count or
    # liveness mismatch is already reported by .n / .live.
    for lname, L in sch.lists.items():
        if not L.applies(mode) or (lname + ".n") not in pfields:
            continue
        names.append(lname + ".n")
        on, pn = ofields.get(lname + ".n", 0), pfields.get(lname + ".n", 0)
        for i in range(min(on, pn)):
            pre = "%s[%d]." % (lname, i)
            if L.dead is not None:
                names.append(pre + "live")
                if not (ofields.get(pre + "live") and pfields.get(pre + "live", 1)):
                    continue
            names.extend(pre + m for m in sorted(members.get(lname, ())) if m != "live")
    for n in sorted(set(names), key=natkey):
        if allowed(rules, n, mode):
            continue
        ov, pv = ofields.get(n, ABSENT), pfields.get(n, ABSENT)
        if ov != pv:
            diffs.append((n, ov, pv))
    return diffs


def natkey(s):
    return [int(t) if t.isdigit() else t for t in re.split(r"(\d+)", s)]


def png_frames(d):
    out = {}
    for name in os.listdir(d):
        m = re.fullmatch(r"frame_(\d+)\.png", name)
        if m:
            out[int(m.group(1))] = os.path.join(d, name)
    return out


def write_pngs(odir, pdir, fdiv, context, outdir):
    try:
        from PIL import Image, ImageChops
    except ImportError:
        print("  --png: Pillow is not installed, no images written")
        return
    op, pp = png_frames(odir), png_frames(pdir)
    common = sorted(set(op) & set(pp))
    if not common:
        print("  --png: no frame with a PNG on both sides (use --png-every on both)")
        return
    before = [f for f in common if f <= fdiv][-context:]
    after = [f for f in common if f > fdiv][:context]
    os.makedirs(outdir, exist_ok=True)
    for f in before + after:
        o = Image.open(op[f]).convert("RGB")
        p = Image.open(pp[f]).convert("RGB")
        if o.size != p.size:
            o = o.resize(p.size, Image.NEAREST)
        w, h = p.size
        mask = ImageChops.difference(o, p).convert("L").point(lambda v: 255 if v else 0)
        diff = Image.blend(p, Image.new("RGB", p.size), 0.6)
        diff.paste((255, 0, 0), mask=mask)
        sheet = Image.new("RGB", (w * 3 + 8, h), (40, 40, 40))
        sheet.paste(o, (0, 0))
        sheet.paste(p, (w + 4, 0))
        sheet.paste(diff, (2 * w + 8, 0))
        path = os.path.join(outdir, "diff_%05d.png" % f)
        sheet.save(path)
        nd = sum(1 for v in mask.getdata() if v)
        print("  wrote %s  (oracle | port | diff, %d pixels differ)%s" % (path, nd, "  <- first divergence" if f == fdiv else ""))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("oracle_dir")
    ap.add_argument("port_dir")
    ap.add_argument("--schema", default=DEFAULT_SCHEMA)
    ap.add_argument("--allow", default=DEFAULT_ALLOW, help="allow-list (default tools/harness/allow.tsv; '' for none)")
    ap.add_argument("--png", action="store_true", help="write oracle|port|diff PNGs around the first divergence")
    ap.add_argument("--out", help="directory for --png (default PORT_DIR/tracediff)")
    ap.add_argument("--context", type=int, default=2, help="PNG frames on each side of the divergence")
    ap.add_argument("--show", type=int, default=40, help="diverging fields to list (default 40)")
    ap.add_argument("--first-only", action="store_true", help="stop at the first diverging frame (no summary scan)")
    a = ap.parse_args()

    try:
        sch = Schema(a.schema)
    except (OSError, ValueError) as e:
        die(str(e))
    rules = load_allow(a.allow)
    port = load_jsonl(os.path.join(a.port_dir, "frames.jsonl"))
    orac = load_jsonl(os.path.join(a.oracle_dir, "frames.jsonl"))
    unknown = sorted(set(n for d in port.values() for n in d.get("fields", {}) if not sch.knows(n)), key=natkey)
    if unknown:
        die("the port emits fields the schema does not define (%s): %s" % (a.schema, ", ".join(unknown[:20])))
    bpath = os.path.join(a.oracle_dir, "base.bin")
    mm, nbase = None, 0
    if os.path.exists(bpath):
        try:
            bf = open(bpath, "rb")
            size = os.fstat(bf.fileno()).st_size
            mm = mmap.mmap(bf.fileno(), 0, access=mmap.ACCESS_READ) if size else b""
        except OSError as e:
            die(str(e))
        nbase = size // FRAME
    elif all("fields" in d for d in orac.values()):
        print("tracediff: %s has no base.bin but named fields: comparing trace against trace" % a.oracle_dir)
    else:
        die("%s: no base.bin" % a.oracle_dir)

    common = sorted(set(port) & set(orac))
    members = modelled_members(sch, port)
    modes = site_modes(port)
    first = None
    per_field = {}                              # name -> (first frame, count)
    ndiv_frames = 0
    for f in common:
        if mm is not None and f >= nbase:
            die("base.bin holds %d frames, frames.jsonl names frame %d" % (nbase, f))
        base = mm[f * FRAME:(f + 1) * FRAME] if mm is not None else None
        diffs = compare_frame(sch, rules, orac[f], port[f], base, members, modes[f])
        if not diffs:
            continue
        ndiv_frames += 1
        for n, _, _ in diffs:
            ff, c = per_field.get(n, (f, 0))
            per_field[n] = (ff, c + 1)
        if first is None:
            first = (f, diffs)
            if a.first_only:
                break

    only_o = sorted(set(orac) - set(port))
    only_p = sorted(set(port) - set(orac))
    print("tracediff: %d aligned frames (oracle %d, port %d), schema %d fields + %d lists, %d allow rules"
          % (len(common), len(orac), len(port), len(sch.fields), len(sch.lists), len(rules)))
    if first is None and not only_o and not only_p:
        print("clean: no divergence")
        return 0
    if first is not None:
        f, diffs = first
        od, pd = orac[f], port[f]
        print("\nFIRST DIVERGENCE at frame %d  (site %s, input %r, oracle rng_calls %s / port %s)"
              % (f, pd.get("site"), pd.get("input"), od.get("rng_calls"), pd.get("rng_calls")))
        print("  %d field(s) differ:" % len(diffs))
        w = max(len(n) for n, _, _ in diffs[:a.show])
        for n, ov, pv in diffs[:a.show]:
            print("    %-*s  oracle %-22s port %s" % (w, n, fmt(ov), fmt(pv)))
            det = hex_detail(n, ov, pv)
            if det:
                print(det)
        if len(diffs) > a.show:
            print("    ... and %d more (--show N)" % (len(diffs) - a.show))
        if not a.first_only:
            print("\nsummary: %d of %d frames diverge; %d distinct fields" % (ndiv_frames, len(common), len(per_field)))
            top = sorted(per_field.items(), key=lambda kv: (kv[1][0], -kv[1][1], natkey(kv[0])))[:15]
            for n, (ff, c) in top:
                print("    %-28s first at frame %-6d differs in %d frame(s)" % (n, ff, c))
        if a.png:
            write_pngs(a.oracle_dir, a.port_dir, f, a.context, a.out or os.path.join(a.port_dir, "tracediff"))
    if only_o or only_p:
        print("\ntrace length differs: %d frame(s) only in the oracle (first %s), %d only in the port (first %s)"
              % (len(only_o), only_o[0] if only_o else "-", len(only_p), only_p[0] if only_p else "-"))
    return 1


if __name__ == "__main__":
    sys.exit(main())
