#!/usr/bin/env python3
"""jrtrace.py - run the real MSCJR.DRV / SNDJR.DRV machine code and log every
byte it writes to the SN76496 (port C0h).

This is the oracle for port/test_audio.c's Tandy checks: nothing here is a
re-implementation of the drivers, the original 16-bit code is executed by the
Unicorn CPU emulator exactly as the game's kernel would call it (INT 60h for
the service interface, a far call to seg:0100 for the music tick and to
seg:1100 for the sound tick, both driven at the 236.7 Hz INT 8 rate, sound
first, as STICK does).  Layout is the game's: both drivers share one segment,
MSCJR.DRV at :0100, SNDJR.DRV at :1100, the first 256 bytes of the segment are
the state page (FF21.. sync bytes, FF26 stopped, FF27/FF28 off flags, FF75 the
sound request).

usage: jrtrace.py [--zeliard DIR] [--root DIR] SCRIPT...
  A SCRIPT item is one of
    music:NAME      INT 60h AX=0 with that score's blob B (mgt1 .. mmao)
    sfx:ID          FF75 = ID (consumed by the next sound tick)
    int60:AX[:CL]   a raw INT 60h call
    run:N           run N INT 8 ticks
  Output: one `tick byte` pair (decimal tick, hex byte) per chip write, then
  `fnv32 XXXXXXXX` and `count N` over the stream (the same hash test_audio
  computes).  Needs `pip install unicorn`.
"""
import argparse
import os
import struct
import sys

from unicorn import (Uc, UC_ARCH_X86, UC_MODE_16, UC_HOOK_INSN, UC_HOOK_INTR, UcError)
from unicorn.x86_const import (UC_X86_REG_AX, UC_X86_REG_CX, UC_X86_REG_DS,
                               UC_X86_REG_ES, UC_X86_REG_SS, UC_X86_REG_SP,
                               UC_X86_REG_CS, UC_X86_REG_IP, UC_X86_REG_SI,
                               UC_X86_INS_OUT, UC_X86_REG_DX, UC_X86_REG_EFLAGS)

SEG = 0x1000
BLOBSEG = 0x2000
STUB = 0x9000                      # segment of the call stubs
SCORES = {
    'mgt1': (1, 46), 'ugm1': (1, 48), 'mgt2': (1, 47), 'ugm2': (1, 49),
    'mus1': (2, 85), 'mus2': (2, 86), 'mus3': (2, 87), 'mus4': (2, 88),
    'mus5': (2, 89), 'mus6': (2, 90), 'mus7': (2, 91), 'mus8': (2, 92),
    'mbos': (2, 93), 'mmao': (2, 95), 'zopn': (0, 39), 'zend': (0, 38),
    'mfan': (2, 94),
}


def lin(seg, off):
    return (seg << 4) + off


class Machine:
    def __init__(self, zdir):
        self.uc = Uc(UC_ARCH_X86, UC_MODE_16)
        uc = self.uc
        uc.mem_map(0, 0x100000)
        msc = open(os.path.join(zdir, 'MSCJR.DRV'), 'rb').read()
        snd = open(os.path.join(zdir, 'SNDJR.DRV'), 'rb').read()
        uc.mem_write(lin(SEG, 0x100), msc)
        uc.mem_write(lin(SEG, 0x1100), snd)
        uc.mem_write(0x60 * 4, struct.pack('<HH', 0x0103, SEG))     # INT 60h
        uc.mem_write(lin(SEG, 0x09), b'\xff')                       # sound driver idle
        # stubs: int 60h / far call music tick / far call sound tick, each ends in hlt
        uc.mem_write(lin(STUB, 0x00), b'\xcd\x60\xf4')
        uc.mem_write(lin(STUB, 0x10), b'\x9a\x00\x01' + struct.pack('<H', SEG) + b'\xf4')
        uc.mem_write(lin(STUB, 0x20), b'\x9a\x00\x11' + struct.pack('<H', SEG) + b'\xf4')
        self.tick = 0
        self.log = []
        uc.hook_add(UC_HOOK_INSN, self._out, None, 1, 0, UC_X86_INS_OUT)
        uc.hook_add(UC_HOOK_INTR, self._intr)

    def _intr(self, uc, intno, user):
        # real-mode INT n: push FLAGS, CS, IP; load the IVT vector
        if intno != 0x60:
            raise RuntimeError('unexpected INT %02x' % intno)
        ss = uc.reg_read(UC_X86_REG_SS)
        sp = uc.reg_read(UC_X86_REG_SP)
        ip = uc.reg_read(UC_X86_REG_IP)
        cs = uc.reg_read(UC_X86_REG_CS)
        fl = uc.reg_read(UC_X86_REG_EFLAGS) & 0xFFFF
        sp -= 6
        uc.mem_write(lin(ss, sp), struct.pack('<HHH', ip, cs, fl))
        uc.reg_write(UC_X86_REG_SP, sp)
        uc.reg_write(UC_X86_REG_CS, SEG)
        uc.reg_write(UC_X86_REG_IP, 0x0103)

    def _out(self, uc, port, size, value, user):
        if port == 0xC0:
            self.log.append((self.tick, value & 0xFF))

    def _run(self, off, end):
        uc = self.uc
        uc.reg_write(UC_X86_REG_SS, 0x3000)
        uc.reg_write(UC_X86_REG_SP, 0xFFF0)
        uc.reg_write(UC_X86_REG_DS, SEG)
        uc.reg_write(UC_X86_REG_ES, SEG)
        uc.reg_write(UC_X86_REG_CS, STUB)
        uc.emu_start(off, end, count=200000)

    def int60(self, ax, cl=0, si=0, ds=SEG):
        self.uc.reg_write(UC_X86_REG_AX, ax)
        self.uc.reg_write(UC_X86_REG_CX, cl)
        self.uc.reg_write(UC_X86_REG_SI, si)
        self._run(0x00, 0x02)

    def start_music(self, blob):
        self.uc.mem_write(lin(BLOBSEG, 0), blob)
        uc = self.uc
        uc.reg_write(UC_X86_REG_AX, 0)
        uc.reg_write(UC_X86_REG_SI, 0)
        uc.reg_write(UC_X86_REG_SS, 0x3000)
        uc.reg_write(UC_X86_REG_SP, 0xFFF0)
        uc.reg_write(UC_X86_REG_ES, SEG)
        uc.reg_write(UC_X86_REG_DS, BLOBSEG)
        uc.reg_write(UC_X86_REG_CS, STUB)
        uc.emu_start(0x00, 0x02, count=200000)

    def sfx(self, i):
        self.uc.mem_write(lin(SEG, 0x75), bytes([i]))

    def run(self, n):
        for _ in range(n):
            self._run(0x20, 0x25)      # sound tick: STICK [FF10]
            self._run(0x10, 0x15)      # music tick: STICK [FF0C]
            self.tick += 1


def blob_b(root, name):
    arc, idx = SCORES[name]
    p = os.path.join(root, 'extracted', 'ZELRES%d' % (arc + 1), 'dec', '%03d_data.dec' % idx)
    d = open(p, 'rb').read()
    la, lb = struct.unpack_from('<HH', d, 0)
    return d[4 + la:4 + la + lb]


def fnv(log):
    h = 2166136261
    for t, b in log:
        for x in struct.pack('<IB', t, b):
            h = ((h ^ x) * 16777619) & 0xFFFFFFFF
    return h


def main():
    here = os.path.dirname(os.path.abspath(__file__))
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--root', default=os.path.join(here, '..'))
    ap.add_argument('--zeliard', default=None)
    ap.add_argument('--quiet', action='store_true', help='print only the hash and count')
    ap.add_argument('script', nargs='+')
    a = ap.parse_args()
    zdir = a.zeliard or os.path.join(a.root, 'zeliard')
    m = Machine(zdir)
    for item in a.script:
        k, _, v = item.partition(':')
        if k == 'music':
            m.start_music(blob_b(a.root, v))
        elif k == 'sfx':
            m.sfx(int(v))
        elif k == 'int60':
            parts = v.split(':')
            m.int60(int(parts[0], 0), int(parts[1], 0) if len(parts) > 1 else 0)
        elif k == 'enable':
            m.int60(2, 0xFF if int(v) else 0)
        elif k == 'pause':
            m.int60(3, 0xFF if int(v) else 0)
        elif k == 'fade':
            m.uc.mem_write(lin(SEG, 0x24), bytes([int(v)]))
        elif k == 'sfxen':
            m.uc.mem_write(lin(SEG, 0x27), b'\x00' if int(v) else b'\xff')
        elif k == 'stop':
            m.int60(1)
        elif k == 'run':
            m.run(int(v))
        else:
            sys.exit('bad script item ' + item)
    if not a.quiet:
        for t, b in m.log:
            print('%d %02x' % (t, b))
    print('fnv32 %08x' % fnv(m.log))
    print('count %d' % len(m.log))


if __name__ == '__main__':
    main()
