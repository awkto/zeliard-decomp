#!/usr/bin/env python3
"""trace_eq.py REF.jsonl CAND.jsonl N -- compare two port traces frame by frame.

The reference is the uninterrupted run; the candidate is a run that loaded a
state saved at boundary N and continued.  Every reference line with f >= N+1
must be byte-for-byte equal (as parsed JSON) to the candidate's line with the
same f, including `site`, `input`, `rng_calls` and every field.

usage: trace_eq.py REF.jsonl CAND.jsonl N [--first-only]
exit: 0 equal, 1 first divergence, 2 usage/IO error
"""
import json
import sys


def load(path):
    out = {}
    with open(path) as f:
        for lineno, line in enumerate(f, 1):
            line = line.strip()
            if not line:
                continue
            try:
                obj = json.loads(line)
            except ValueError as e:
                sys.stderr.write("%s:%d: bad JSON: %s\n" % (path, lineno, e))
                sys.exit(2)
            out[obj["f"]] = obj
    return out


def main():
    if len(sys.argv) < 4:
        sys.stderr.write(__doc__)
        return 2
    ref_path, cand_path, n = sys.argv[1], sys.argv[2], int(sys.argv[3], 0)
    ref = load(ref_path)
    cand = load(cand_path)
    ref_frames = [f for f in ref if f >= n + 1]
    cand_frames = sorted(cand)
    if not ref_frames:
        sys.stderr.write("trace_eq: no reference frames >= %d\n" % (n + 1))
        return 2
    if cand_frames and cand_frames[0] != n + 1:
        sys.stderr.write("trace_eq: candidate starts at frame %d, expected %d\n"
                         % (cand_frames[0], n + 1))
        return 1
    for f in sorted(ref_frames):
        a = ref[f]
        b = cand.get(f)
        if b is None:
            print("MISSING frame %d in candidate" % f)
            return 1
        if a == b:
            continue
        # locate the first differing key, so the report is actionable
        keys = sorted(set(a) | set(b))
        detail = ""
        for k in keys:
            if a.get(k) != b.get(k):
                if k == "fields":
                    for fk in sorted(set(a.get("fields", {})) | set(b.get("fields", {}))):
                        if a.get("fields", {}).get(fk) != b.get("fields", {}).get(fk):
                            detail = "field %s: ref=%r cand=%r" % (
                                fk, a["fields"].get(fk), b["fields"].get(fk))
                            break
                else:
                    detail = "%s: ref=%r cand=%r" % (k, a.get(k), b.get(k))
                if detail:
                    break
        print("FIRST DIVERGENCE at frame %d: %s" % (f, detail or "records differ"))
        return 1
    if len(cand_frames) != len(ref_frames):
        print("frame count differs: reference %d, candidate %d"
              % (len(ref_frames), len(cand_frames)))
        return 1
    print("trace_eq: %d frames identical from frame %d" % (len(ref_frames), n + 1))
    return 0


if __name__ == "__main__":
    sys.exit(main())
