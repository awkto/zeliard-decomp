#!/usr/bin/env python3
"""Extractor for Game Arts .SAR resource archives (Zeliard).

Format:
  header: N x uint32le offsets (N = offsets[0] / 4; offsets point into file)
  entry:  uint32le payload_length, then payload bytes
"""
import struct
import sys
from pathlib import Path


def classify(data: bytes) -> str:
    """Rough guess at payload type for naming."""
    if len(data) < 8:
        return "tiny"
    # 16-bit real-mode code heuristics: common opcode prologue bytes
    head = data[:64]
    code_markers = (b"\xfa\xbc", b"\x0e\x1f", b"\x2e\xff", b"\x8c\xc8", b"\xcd\x21")
    if any(m in head for m in code_markers):
        return "code"
    return "data"


def extract(sar_path: Path, out_root: Path) -> list[dict]:
    raw = sar_path.read_bytes()
    (first,) = struct.unpack_from("<I", raw, 0)
    count = first // 4
    offsets = list(struct.unpack_from(f"<{count}I", raw, 0))
    out_dir = out_root / sar_path.stem
    out_dir.mkdir(parents=True, exist_ok=True)
    entries = []
    for i, off in enumerate(offsets):
        (length,) = struct.unpack_from("<I", raw, off)
        payload = raw[off + 4 : off + 4 + length]
        end = off + 4 + length
        nxt = offsets[i + 1] if i + 1 < count else len(raw)
        kind = classify(payload)
        name = f"{i:03d}_{kind}.bin"
        (out_dir / name).write_bytes(payload)
        entries.append(
            {
                "index": i,
                "offset": off,
                "length": length,
                "kind": kind,
                "gap": nxt - end,  # nonzero gap => format assumption wrong
                "head": payload[:16].hex(" "),
            }
        )
    return entries


def main() -> None:
    out_root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path("extracted")
    for sar in sorted(Path("zeliard").glob("*.SAR")):
        entries = extract(sar, out_root)
        bad = [e for e in entries if e["gap"] != 0]
        print(f"{sar.name}: {len(entries)} entries, "
              f"{sum(1 for e in entries if e['kind'] == 'code')} look like code, "
              f"{len(bad)} gap anomalies")
        for e in entries:
            flag = f"  GAP={e['gap']}" if e["gap"] else ""
            print(f"  [{e['index']:3d}] off=0x{e['offset']:06x} "
                  f"len={e['length']:6d} {e['kind']:4s} {e['head']}{flag}")


if __name__ == "__main__":
    main()
