#!/usr/bin/env python3
"""script_cut.py N SCRIPT -- drop the first N token-frames from a --script.

The port's script language (docs/HARNESS.md section 2) consumes exactly one
token-frame per harness boundary, so the boundary numbered N applies token N
(0-based) and the frame after it is frame N+1.  A state saved at boundary N is
therefore continued by the script with its first N frames removed -- which is
what this prints, run-length coded the same way the port's --emit-script does.

usage: script_cut.py N SCRIPT
       script_cut.py N --file PATH
"""
import sys


def tokenize(s):
    """-> [(letters, count)] in order; letters is the token text ('' for a
    typed-key token, which carries its key in the count-1 following bytes)."""
    toks = []
    i, n = 0, len(s)
    while i < n:
        while i < n and s[i] in " ,\t\n":
            i += 1
        if i >= n:
            break
        if s[i] == "'":
            # a typed key: 'X where X is the key, then an optional count
            key = s[i + 1] if i + 1 < n else ""
            i += 2
            num = 0
            while i < n and s[i].isdigit():
                num = num * 10 + int(s[i]); i += 1
            toks.append(("'" + key, num or 1))
        else:
            start = i
            while i < n and s[i] not in " ,\t\n" and not s[i].isdigit():
                i += 1
            letters = s[start:i]
            num = 0
            while i < n and s[i].isdigit():
                num = num * 10 + int(s[i]); i += 1
            if letters:
                toks.append((letters, num or 1))
    return toks


def main():
    if len(sys.argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    n = int(sys.argv[1], 0)
    if sys.argv[2] == "--file":
        script = open(sys.argv[3]).read()
    else:
        script = sys.argv[2]
    out = []
    for letters, count in tokenize(script):
        if n >= count:
            n -= count
            continue
        count -= n
        n = 0
        out.append("%s%u" % (letters, count))
    print(" ".join(out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
