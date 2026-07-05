#!/usr/bin/env python3
"""Recover original resource names for Zeliard SAR entries.

Two sources:
 1. STICK.BIN's built-in table at mem 0xF68 (file 0xE68): 11-byte records
    {u8 archive, u8 res# (1-based), char name[9] ASCIIZ} — the .MDT maps.
 2. Request blocks embedded in code (GAME.BIN, overlays, drivers):
    {u8 archive 0..2, u8 res# 1..96, printable name ending .ext, NUL}.
    The name after the two bytes is dead weight to the kernel (it only uses
    it when res#==0) but the developers left the original filenames in.

Output: docs/RESOURCES.md name map.
"""
import re
import sys
from pathlib import Path

NAME_RE = re.compile(rb"([a-zA-Z][a-zA-Z0-9_]{1,7}\.[a-zA-Z]{2,3})\x00")


def scan_blob(blob: bytes, source: str, found: dict) -> None:
    for m in NAME_RE.finditer(blob):
        start = m.start()
        if start < 2:
            continue
        archive, res = blob[start - 2], blob[start - 1]
        if archive > 2 or not (1 <= res <= 96):
            continue
        key = (archive, res - 1)
        name = m.group(1).decode().lower()
        found.setdefault(key, {}).setdefault(name, set()).add(source)


def main() -> None:
    found: dict = {}

    stick = Path("zeliard/STICK.BIN").read_bytes()
    pos = 0xE68
    while pos + 11 <= len(stick):
        archive, res = stick[pos], stick[pos + 1]
        raw = stick[pos + 2:pos + 11]
        name = raw.split(b"\x00")[0]
        if archive > 2 or not (1 <= res <= 96) or not NAME_RE.match(name + b"\x00"):
            break
        found.setdefault((archive, res - 1), {}).setdefault(
            name.decode().lower(), set()).add("STICK.BIN@F68")
        pos += 11

    for f in ["zeliard/GAME.BIN", "zeliard/STDPLY.BIN", "zeliard/STICK.BIN",
              *sorted(str(p) for p in Path("zeliard").glob("*.DRV"))]:
        scan_blob(Path(f).read_bytes(), Path(f).name, found)
    for f in sorted(Path("extracted").glob("ZELRES*/[0-9]*.bin")):
        scan_blob(f.read_bytes(), f"{f.parent.name}/{f.name}", found)
    for f in sorted(Path("extracted").glob("ZELRES*/dec/*.dec")):
        scan_blob(f.read_bytes(), f"{f.parent.parent.name}/dec/{f.name}", found)

    lines = ["# Recovered resource names", "",
             "`(archive, 0-based index)` → original filename(s); "
             "sources are the binaries whose request blocks reference them.", ""]
    lines.append("| Archive | Index | Name(s) | Referenced by |")
    lines.append("|---|---|---|---|")
    for (archive, idx) in sorted(found):
        names = found[(archive, idx)]
        namestr = ", ".join(sorted(names))
        srcs = ", ".join(sorted(set().union(*names.values())))
        lines.append(f"| ZELRES{archive + 1} | {idx} | {namestr} | {srcs} |")
    total = sum(1 for _ in found)
    lines.append("")
    lines.append(f"{total} of 194 entries named.")
    out = Path("docs/RESOURCES.md")
    out.write_text("\n".join(lines) + "\n")
    print(f"{total} entries named -> {out}")
    for (archive, idx) in sorted(found):
        print(f"  ZELRES{archive+1}[{idx:2d}] = {', '.join(sorted(found[(archive, idx)]))}")


if __name__ == "__main__":
    main()
