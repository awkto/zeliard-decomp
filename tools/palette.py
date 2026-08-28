#!/usr/bin/env python3
"""Zeliard MCGA palette — derived from GAME.BIN, routine @A41B (video mode 4 = MCGA).

GAME.BIN dispatches on the video-mode index [FF14] (jump table @A3ED):
  EGA  -> @A3FE: int 10h AX=1002h with the 17-byte palette block @A409
  MCGA -> @A41B: for left in 0..7: for right in 0..7:
             DAC[left*8 + right] = BASE[left] + BASE[right]   (per R,G,B; 0..0x3E of 0x3F)
BASE is the 8-entry RGB table @A456 (each component 0x00 or 0x1F).

Why pairs: cell pixel data is PC-88 style 16x8 three-bitplane; the MCGA driver's
service [0x2044] (GMMCGA @2C2A) packs each pair of adjacent 3-bit pixels into one
6-bit VGA index = left<<3 | right, so a 16x8 cell becomes 8x8 on MCGA and every
on-screen colour is the additive blend of two of the eight PC-88 colours.

usage: palette.py [--gpl OUT.gpl]   (prints the 64 entries; --gpl writes a GIMP palette)
"""
import sys

# GAME.BIN @A456 — 3-bit pixel value -> (R,G,B) in 5-bit DAC units
BASE = [
    (0x00, 0x00, 0x00),  # 0 black
    (0x1F, 0x1F, 0x1F),  # 1 white
    (0x1F, 0x00, 0x00),  # 2 red
    (0x00, 0x1F, 0x00),  # 3 green
    (0x00, 0x1F, 0x1F),  # 4 cyan
    (0x00, 0x00, 0x1F),  # 5 blue
    (0x1F, 0x1F, 0x00),  # 6 yellow
    (0x1F, 0x00, 0x1F),  # 7 magenta
]

# GAME.BIN @A409 — EGA palette registers 0..15 + overscan (int 10h AX=1002h)
EGA_REGS = [0x00, 0x3F, 0x24, 0x12, 0x1B, 0x09, 0x36, 0x2D,
            0x38, 0x07, 0x04, 0x02, 0x03, 0x01, 0x06, 0x05]
EGA_OVERSCAN = 0x00


def dac6() -> list:
    """64 entries of (r,g,b) in 6-bit DAC units, index = left*8 + right."""
    return [tuple(BASE[l][c] + BASE[r][c] for c in range(3))
            for l in range(8) for r in range(8)]


def rgb8() -> list:
    """64 entries of 8-bit (r,g,b), scaled like DOSBox (round(v*255/63)) — verified
    pixel-exact against DOSBox captures of the status screen and town."""
    return [tuple(round(v * 255 / 63) for v in e) for e in dac6()]


MCGA = rgb8()


def main() -> None:
    pal = rgb8()
    if len(sys.argv) > 2 and sys.argv[1] == "--gpl":
        with open(sys.argv[2], "w") as f:
            f.write("GIMP Palette\nName: Zeliard MCGA\nColumns: 8\n#\n")
            for i, (r, g, b) in enumerate(pal):
                f.write(f"{r:3d} {g:3d} {b:3d}\t{i:02d} L{i >> 3} R{i & 7}\n")
        print(f"wrote {sys.argv[2]}")
        return
    for i, (r, g, b) in enumerate(pal):
        print(f"{i:2d} (L{i >> 3} R{i & 7}) #{r:02x}{g:02x}{b:02x}")


if __name__ == "__main__":
    main()
