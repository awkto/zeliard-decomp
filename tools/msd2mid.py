#!/usr/bin/env python3
"""msd2mid.py - convert Zeliard .msd music scores to standard MIDI.

usage: msd2mid.py FILE.dec OUT.mid [--mt] [--jr] [--loops N] [--dump]
       msd2mid.py --all OUTDIR [--loops N]

FILE.dec is a decompressed ZELRES entry ({u16 lenA,u16 lenB} blobA blobB, see
docs/MUSIC.md).  By default blob B's AdLib arrangement is converted (6 OPL2
melodic tracks + the rhythm track); --jr converts the 3-voice Tandy tracks of
blob B (what MSCJR/MSCSTD play); --mt converts blob A (the MT-32 arrangement,
already MIDI-shaped, with MT-32 patch numbers mapped to GM).

Timing: score ticks are 118.35 Hz * (256-T)/256 (driver tick = INT8/2; the
score advances on the driver ticks where the tempo accumulator does NOT carry);
scores use 24 ticks per quarter, so the MIDI is written with PPQ=24 and a tempo
meta event of 51,913,590/(256-T) microseconds per quarter for every tempo byte T.

Loops: the whole machine state (every track's position, counters, remaining
durations) is hashed every tick; the first repeated state gives the loop
[start,end).  The body is emitted once (plus "loop start"/"loop end" markers) or
N times with --loops N.  Songs that end with FF on every track simply end.
"""
import argparse
import math
import os
import struct
import sys

# --------------------------------------------------------------------------
# constants
INT8_HZ = 1193182 / 0x13B1          # 236.70 Hz (ZELIARD.EXE reprograms PIT ch0)
DRIVER_HZ = INT8_HZ / 2             # the driver runs every 2nd INT 8
PPQ = 24                            # score ticks per quarter note


def tempo_us_per_quarter(t):
    """tempo byte -> microseconds per quarter note (24 score ticks)."""
    ticks_per_s = score_ticks_per_s(t)
    return int(round(PPQ * 1e6 / ticks_per_s))


def score_ticks_per_s(t):
    return DRIVER_HZ * (256 - t) / 256.0


# Where the 17 scores live (docs/RESOURCES.md).  name -> (archive, index)
SCORES = {
    'zopn': (1, 39), 'zend': (1, 38),
    'mgt1': (2, 46), 'mgt2': (2, 47), 'ugm1': (2, 48), 'ugm2': (2, 49),
    'mus1': (3, 85), 'mus2': (3, 86), 'mus3': (3, 87), 'mus4': (3, 88),
    'mus5': (3, 89), 'mus6': (3, 90), 'mus7': (3, 91), 'mus8': (3, 92),
    'mbos': (3, 93), 'mfan': (3, 94), 'mmao': (3, 95),
}
SCORE_DESC = {
    'zopn': 'opening (title/opdemo)', 'zend': 'ending (enddemo)',
    'mgt1': 'town 1 (Muralla/Satono)', 'mgt2': 'town 2', 'ugm1': 'underground/shop 1',
    'ugm2': 'underground/shop 2', 'mus1': 'cavern 1', 'mus2': 'cavern 2',
    'mus3': 'cavern 3', 'mus4': 'cavern 4', 'mus5': 'cavern 5', 'mus6': 'cavern 6',
    'mus7': 'cavern 7', 'mus8': 'cavern 8', 'mbos': 'boss', 'mfan': 'fanfare (jingle)',
    'mmao': 'final boss (Jashiin)',
}

# OPL rhythm bits -> GM drum note
DRUM_NOTES = {0x10: 36, 0x08: 38, 0x04: 45, 0x02: 49, 0x01: 42}   # BD SD TT CY HH
DRUM_LEVEL_IDX = {0x10: 0, 0x01: 1, 0x08: 2, 0x04: 3, 0x02: 4}   # [di+0F+idx]

# MT-32 preset number (0-based) -> GM program (approximate, the usual mapping)
MT32_TO_GM = [
    0, 1, 0, 2, 4, 4, 5, 3, 16, 17, 18, 16, 19, 19, 19, 21, 6, 6, 6, 7, 7, 7, 8, 8,
    62, 63, 62, 63, 38, 39, 38, 39, 88, 90, 52, 92, 97, 99, 98, 85, 100, 101, 68,
    89, 87, 91, 95, 80, 48, 48, 49, 45, 40, 40, 42, 42, 43, 46, 46, 24, 25, 27, 27,
    104, 32, 32, 33, 33, 36, 36, 35, 35, 73, 73, 72, 72, 74, 75, 64, 65, 66, 67, 71,
    71, 68, 69, 70, 22, 56, 56, 57, 57, 60, 60, 58, 61, 61, 11, 11, 12, 88, 9, 14,
    13, 12, 107, 111, 77, 78, 78, 76, 76, 47, 117, 118, 119, 118, 116, 116, 119,
    115, 112, 55, 124, 123, 0, 12, 0,
]
MT32_NAMES = [
    'AcouPiano1', 'AcouPiano2', 'AcouPiano3', 'ElecPiano1', 'ElecPiano2', 'ElecPiano3',
    'ElecPiano4', 'Honkytonk', 'Elec Org1', 'Elec Org2', 'Elec Org3', 'Elec Org4',
    'PipeOrg1', 'PipeOrg2', 'PipeOrg3', 'Accordion', 'Harpsi1', 'Harpsi2', 'Harpsi3',
    'Clavi1', 'Clavi2', 'Clavi3', 'Celesta1', 'Celesta2', 'SynBrass1', 'SynBrass2',
    'SynBrass3', 'SynBrass4', 'SynBass1', 'SynBass2', 'SynBass3', 'SynBass4', 'Fantasy',
    'HarmoPan', 'Chorale', 'Glasses', 'Soundtrack', 'Atmosphere', 'WarmBell', 'FunnyVox',
    'EchoBell', 'IceRain', 'Oboe2001', 'EchoPan', 'DrSolo', 'Schooldaze', 'BellSinger',
    'SquareWave', 'StrSect1', 'StrSect2', 'StrSect3', 'Pizzicato', 'Violin1', 'Violin2',
    'Cello1', 'Cello2', 'Contrabass', 'Harp1', 'Harp2', 'Guitar1', 'Guitar2', 'ElecGtr1',
    'ElecGtr2', 'Sitar', 'AcouBass1', 'AcouBass2', 'ElecBass1', 'ElecBass2', 'SlapBass1',
    'SlapBass2', 'Fretless1', 'Fretless2', 'Flute1', 'Flute2', 'Piccolo1', 'Piccolo2',
    'Recorder', 'PanPipes', 'Sax1', 'Sax2', 'Sax3', 'Sax4', 'Clarinet1', 'Clarinet2',
    'Oboe', 'EnglHorn', 'Bassoon', 'Harmonica', 'Trumpet1', 'Trumpet2', 'Trombone1',
    'Trombone2', 'FrHorn1', 'FrHorn2', 'Tuba', 'BrsSect1', 'BrsSect2', 'Vibe1', 'Vibe2',
    'SynMallet', 'Windbell', 'Glock', 'TubeBell', 'Xylophone', 'Marimba', 'Koto', 'Sho',
    'Shakuhachi', 'Whistle1', 'Whistle2', 'BottleBlow', 'BreathPipe', 'Timpani',
    'MelodicTom', 'DeepSnare', 'ElecPerc1', 'ElecPerc2', 'Taiko', 'TaikoRim', 'Cymbal',
    'Castanets', 'Triangle', 'OrcheHit', 'Telephone', 'BirdTweet', 'OneNoteJam',
    'WaterBell', 'JungleTune',
]


class ScoreError(Exception):
    pass


# --------------------------------------------------------------------------
# MIDI writer
def vlq(n):
    out = [n & 0x7F]
    n >>= 7
    while n:
        out.append(0x80 | (n & 0x7F))
        n >>= 7
    return bytes(reversed(out))


class MidiTrack:
    def __init__(self, name=None):
        self.events = []        # (tick, order, bytes)
        self.seq = 0
        if name:
            self.meta(0, 0x03, name.encode('latin-1'))

    def add(self, tick, data, order=1):
        self.events.append((tick, order, self.seq, data))
        self.seq += 1

    def meta(self, tick, kind, payload, order=0):
        self.add(tick, bytes([0xFF, kind]) + vlq(len(payload)) + payload, order)

    def tempo(self, tick, us):
        self.meta(tick, 0x51, struct.pack('>I', us)[1:])

    def marker(self, tick, text):
        self.meta(tick, 0x06, text.encode('latin-1'))

    def note_on(self, tick, ch, note, vel):
        self.add(tick, bytes([0x90 | ch, note & 0x7F, max(1, min(127, vel))]), 2)

    def note_off(self, tick, ch, note):
        self.add(tick, bytes([0x80 | ch, note & 0x7F, 0]), 1)

    def program(self, tick, ch, prog):
        self.add(tick, bytes([0xC0 | ch, prog & 0x7F]), 0)

    def cc(self, tick, ch, ctl, val):
        self.add(tick, bytes([0xB0 | ch, ctl & 0x7F, max(0, min(127, val))]), 0)

    def bend(self, tick, ch, lsb, msb):
        self.add(tick, bytes([0xE0 | ch, lsb & 0x7F, msb & 0x7F]), 0)

    def render(self, end_tick):
        self.events.sort()
        out = bytearray()
        last = 0
        for tick, _o, _s, data in self.events:
            out += vlq(tick - last) + data
            last = tick
        out += vlq(max(0, end_tick - last)) + b'\xff\x2f\x00'
        return b'MTrk' + struct.pack('>I', len(out)) + bytes(out)


def write_midi(path, tracks, end_tick):
    with open(path, 'wb') as f:
        f.write(b'MThd' + struct.pack('>IHHH', 6, 1, len(tracks), PPQ))
        for t in tracks:
            f.write(t.render(end_tick))


# --------------------------------------------------------------------------
# event-log based playback model.  The simulators below do not write MIDI
# directly; they append (tick, kind, ...) records to a Log so that loops can be
# unrolled afterwards.
class Log:
    def __init__(self):
        self.ev = []            # (tick, track, kind, args)
        self.tempo_changes = []  # (tick, T)
        self.warnings = []

    def add(self, tick, track, kind, *args):
        self.ev.append((tick, track, kind, args))

    def warn(self, msg):
        if msg not in self.warnings:
            self.warnings.append(msg)


def att_to_velocity(att_units):
    """OPL2 total-level units (0.75 dB each) -> MIDI velocity, GM curve
    (40*log10(v/127) dB)."""
    db = 0.75 * att_units
    v = 127 * 10 ** (-db / 40.0)
    return max(8, min(127, int(round(v))))


# --------------------------------------------------------------------------
# Blob B (STD/JR/ADLIB) header
class BlobB:
    def __init__(self, data):
        if len(data) < 0x23 or data[0] != 4:
            raise ScoreError('blob B: bad header (byte 0 = %r)' % (data[:1],))
        self.data = data
        w = struct.unpack_from('<13H', data, 1)
        self.adlib_tracks = list(w[0:6])
        self.jr_tracks = list(w[6:9])
        self.rhythm_track = w[9]
        self.opl_instruments = w[10]
        self.instruments = w[11]
        self.dur_tables = w[12]
        self.length = struct.unpack_from('<H', data, 0x1B)[0]

    def opl_patch(self, n):
        o = self.opl_instruments + 15 * n
        return self.data[o:o + 15]

    def std_patch(self, n):
        o = self.instruments + 17 * n
        return self.data[o:o + 17]


def gm_program_for_patch(patch, mean_pitch):
    """Heuristic GM program for an OPL2 patch (the OPL patches have no names)."""
    if len(patch) < 15:
        return 80
    car_20, car_63, car_83, byte14 = patch[7], patch[11], patch[13], patch[14]
    sustaining = bool(car_20 & 0x20)
    sustain_level = car_83 >> 4          # 0 = loud sustain, 15 = none
    additive = byte14 & 1
    low = mean_pitch < 48
    if sustaining and sustain_level <= 4:
        if low:
            return 39 if additive else 38          # synth bass 2 / 1
        return 80 if additive else 81              # square / saw lead
    if low:
        return 33 if additive else 34              # electric bass finger / pick
    return 4 if additive else 5                    # electric piano 1 / 2


# --------------------------------------------------------------------------
# Common flow-control opcodes (F0-FF) shared by every blob-B track kind
class FlowMixin:
    """Implements F0-FF; `self.blob` (bytes), `self.pos`, `self.counters[4]`,
    `self.durtab` (offset), `self.ret`, `self.saved_durtab`, `self.ended`,
    `self.g` (shared Globals) are required."""

    def u8(self):
        v = self.blob[self.pos]
        self.pos += 1
        return v

    def i8(self):
        v = self.u8()
        return v - 256 if v & 0x80 else v

    def u16(self):
        v = struct.unpack_from('<H', self.blob, self.pos)[0]
        self.pos += 2
        return v

    def dur(self, idx):
        if self.durtab is None:
            raise ScoreError('%s: note before any F0 duration-table select' % self.name)
        return self.blob[self.durtab + idx]

    def flow(self, op, allow_f4):
        g = self.g
        if op == 0xF0:
            self.durtab = self.hdr.dur_tables + 8 * self.u8()
        elif op == 0xF1:
            i = self.u8()
            g.sync[i] = (g.sync[i] + 1) & 0xFF
        elif op == 0xF2:
            i = self.u8()
            g.sync[i] = (g.sync[i] - 1) & 0xFF
        elif op == 0xF3:
            i = self.u8()
            g.sync[i] = self.u8()
        elif op == 0xF4:
            if allow_f4:
                i = self.u8()
                v = self.u8()
                a = self.u16()
                if g.sync[i] == v:
                    self.pos = a
        elif op == 0xF5:
            b = self.u8()
            self.counters[b & 3] = b >> 2
        elif op == 0xF6:
            a = self.u16()
            c = a >> 14
            self.counters[c] = (self.counters[c] - 1) & 0xFF
            if self.counters[c] != 0:
                self.pos = a & 0x3FFF
        elif op == 0xF7:
            a = self.u16()
            c = a >> 14
            self.counters[c] = (self.counters[c] - 1) & 0xFF
            if self.counters[c] == 0:
                self.pos = a & 0x3FFF
        elif op == 0xF8:
            v = self.u8()
            a = self.u16()
            if self.counters[a >> 14] == v:
                self.pos = a & 0x3FFF
        elif op == 0xF9:
            i = self.u8()
            self.counters[i & 3] = (self.counters[i & 3] + 1) & 0xFF
        elif op == 0xFA:
            i = self.u8()
            self.counters[i & 3] = (self.counters[i & 3] - 1) & 0xFF
        elif op == 0xFB:
            self.pos = self.u16()
        elif op == 0xFC:
            a = self.u16()
            self.ret = self.pos
            self.saved_durtab = self.durtab
            self.pos = a
        elif op == 0xFD:
            v = self.u8()
            a = self.u16()
            if self.counters[a >> 14] == v:
                self.ret = self.pos
                self.saved_durtab = self.durtab
                self.pos = a & 0x3FFF
        elif op == 0xFE:
            if self.ret is None:
                raise ScoreError('%s: FE (return) without call at %04X' % (self.name, self.pos - 1))
            self.pos = self.ret
            self.durtab = self.saved_durtab
        elif op == 0xFF:
            self.ended = True
        else:
            raise ScoreError('%s: unknown flow op %02X' % (self.name, op))


class Globals:
    def __init__(self, tempo=0x7F):
        self.sync = [0] * 5      # FF21..FF25 (F1-F4 index 0-2; 3,4 = fade rate/level)
        self.tempo = tempo
        self.tempo_log = []


# --------------------------------------------------------------------------
# Melodic track (STD / JR / ADLIB flavours)
class MelodicChan(FlowMixin):
    def __init__(self, hdr, g, log, idx, start, flavour, name):
        self.hdr = hdr
        self.blob = hdr.data
        self.g = g
        self.log = log
        self.idx = idx
        self.name = name
        self.flavour = flavour          # 'adlib' | 'jr' | 'std'
        self.pos = start
        self.remaining = 1
        self.octave = 3
        self.gate = 1
        self.att = 0x7F
        self.detune = 0
        self.durtab = None
        self.instr = None
        self.counters = [0, 0, 0, 0]
        self.ret = None
        self.saved_durtab = None
        self.hold_no_release = False    # bit4: next byte was E7
        self.legato_pending = False     # bit5: E7 executed
        self.ended = False
        self.cur_note = None
        self.notes = 0
        self.pitches = []
        self.instr_uses = {}            # instr -> [pitch sum, count]

    def state(self):
        return (self.pos, self.remaining, self.octave, self.gate, self.att, self.detune,
                self.durtab, self.instr, tuple(self.counters), self.ret, self.saved_durtab,
                self.hold_no_release, self.legato_pending, self.ended, self.cur_note)

    def midi_note(self, pitch):
        if self.flavour == 'adlib':
            base = 12 * (self.octave + 1)
        elif self.flavour == 'jr':
            base = 12 * (max(self.octave, 1) + 2)
        else:
            base = 12 * (self.octave + 3)
        return base + pitch - 1

    def velocity(self):
        """note-on velocity = the patch's own carrier level (the channel
        attenuation goes to CC7, see cc7())."""
        if self.flavour == 'adlib' and self.instr is not None:
            p = self.hdr.opl_patch(self.instr)
            if len(p) == 15:
                return att_to_velocity(p[9] & 0x3F)
        return 127

    def cc7(self):
        return att_to_velocity(self.att >> 1)

    def release(self, tick):
        if self.cur_note is not None:
            self.log.add(tick, self.idx, 'off', self.cur_note)
            self.cur_note = None

    def key_on(self, tick, note, legato):
        if self.cur_note is not None:
            self.log.add(tick, self.idx, 'off', self.cur_note)
        self.log.add(tick, self.idx, 'on', note, self.velocity(), legato)
        self.cur_note = note
        self.notes += 1
        self.pitches.append(note)
        if self.instr is not None:
            u = self.instr_uses.setdefault(self.instr, [0, 0])
            u[0] += note
            u[1] += 1

    def tick(self, t):
        if self.ended:
            return
        self.remaining -= 1
        if self.remaining != 0:
            if self.gate >= self.remaining and not self.hold_no_release:
                self.release(t)
            return
        guard = 0
        while True:
            guard += 1
            if guard > 10000:
                raise ScoreError('%s: runaway command loop at %04X' % (self.name, self.pos))
            b = self.u8()
            if b < 0x80:
                self.note(t, b)
                return
            self.command(t, b)
            if self.ended:
                self.release(t)
                return

    def note(self, t, b):
        self.hold_no_release = (self.pos < len(self.blob) and self.blob[self.pos] == 0xE7)
        self.remaining = self.dur(b >> 4)
        if self.remaining == 0:
            self.log.warn('%s: zero duration at %04X' % (self.name, self.pos - 1))
            self.remaining = 1
        p = b & 0xF
        if p == 0:
            self.release(t)
            return
        if p == 0xF:
            return
        if p > 12:
            self.log.warn('%s: pitch %d (beyond B) at %04X' % (self.name, p, self.pos - 1))
        note = self.midi_note(p)
        legato = self.legato_pending
        self.legato_pending = False
        self.key_on(t, note, legato)

    def command(self, t, b):
        f = self.flavour
        if b < 0xC0:
            self.instr = b & 0x3F
            self.log.add(t, self.idx, 'instr', self.instr)
        elif b < 0xD0:
            s = b & 0xF
            if s >= 8:
                s -= 16
            if s >= 0:
                self.att = max(0, self.att - 4 * (s + 1))
            else:
                self.att += 4 * (-s)
                if f == 'adlib':
                    self.att = 0x3F if self.att >= 0x40 else self.att
                else:
                    self.att = 0x7F if self.att >= 0x80 else self.att
            self.log.add(t, self.idx, 'vol', self.cc7())
        elif b < 0xD8:
            self.octave = b & 7
        elif b < 0xE0:
            self.gate = self.dur(b & 7)
        elif b == 0xE0:
            self.g.tempo = self.u8()
            self.g.tempo_log.append((t, self.g.tempo))
        elif b == 0xE1:
            v = self.i8()
            self.detune = (v >> 3) if f == 'jr' else v
        elif b == 0xE2:
            if self.u8() != 0:
                self.pos += 5
        elif b == 0xE3:
            self.octave = (self.octave - 1) & 0xFF
        elif b == 0xE4:
            self.octave = (self.octave + 1) & 0xFF
        elif b == 0xE5:
            self.att = self.u8()
            self.log.add(t, self.idx, 'vol', self.cc7())
        elif b == 0xE7:
            self.legato_pending = True
        elif b in (0xE6, 0xE8, 0xED, 0xEE, 0xEF):
            pass
        elif b in (0xE9, 0xEA, 0xEB):
            if f == 'adlib':
                pass                                  # no argument on ADLIB
            elif f == 'jr' and b == 0xEB:
                pass
            else:
                self.u8()                             # STD skips 1; JR E9/EA take 1
        elif b == 0xEC:
            if f != 'adlib':
                if self.u8() != 0:
                    self.u8()
        else:
            self.flow(b, allow_f4=(f == 'adlib'))


# --------------------------------------------------------------------------
# OPL rhythm track
class RhythmChan(FlowMixin):
    def __init__(self, hdr, g, log, idx, start, name):
        self.hdr = hdr
        self.blob = hdr.data
        self.g = g
        self.log = log
        self.idx = idx
        self.name = name
        self.pos = start
        self.remaining = 1
        self.default_dur = 0
        self.levels = [0, 0, 0, 0, 0]     # BD HH SD TT CY, OPL level units
        self.durtab = None
        self.counters = [0, 0, 0, 0]
        self.ret = None
        self.saved_durtab = None
        self.ended = False
        self.notes = 0
        self.sounding = set()

    def state(self):
        return (self.pos, self.remaining, self.default_dur, tuple(self.levels), self.durtab,
                tuple(self.counters), self.ret, self.saved_durtab, self.ended)

    def tick(self, t):
        if self.ended:
            return
        self.remaining -= 1
        if self.remaining != 0:
            return
        guard = 0
        while True:
            guard += 1
            if guard > 10000:
                raise ScoreError('%s: runaway command loop' % self.name)
            b = self.u8()
            if b < 0x80:
                mask = b & 0x1F
                if mask:
                    for bit, note in DRUM_NOTES.items():
                        if mask & bit:
                            vel = att_to_velocity(self.levels[DRUM_LEVEL_IDX[bit]])
                            self.log.add(t, self.idx, 'drum', note, vel)
                            self.notes += 1
                self.remaining = self.u8() if b & 0x20 else self.default_dur
                if self.remaining == 0:
                    self.log.warn('%s: zero drum duration at %04X' % (self.name, self.pos - 1))
                    self.remaining = 1
                return
            if b < 0xA0:
                pass                                        # key-off drums: nothing in MIDI
            elif b < 0xC8:
                self.default_dur = self.dur(b & 7)
            elif b < 0xCD:
                i = b - 0xC8
                v = (self.levels[i] & 0x3F) + self.i8()
                self.levels[i] = max(0, min(0x3F, v))
            elif b < 0xD2:
                i = b - 0xCD
                self.levels[i] = (self.u8() * 4) & 0xFF
            else:
                self.flow(0xF0 | (b & 0xF), allow_f4=True)
            if self.ended:
                return


# --------------------------------------------------------------------------
# Blob A (MT-32)
class BlobA:
    def __init__(self, data):
        self.data = data
        self.note_tables, self.dur_tables = struct.unpack_from('<HH', data, 0)
        self.chans = []
        for i in range(9):
            ch = data[4 + 3 * i]
            off = struct.unpack_from('<H', data, 5 + 3 * i)[0]
            self.chans.append((ch, off))


class MtChan:
    def __init__(self, hdr, g, log, idx, midi_ch, start, name):
        self.hdr = hdr
        self.blob = hdr.data
        self.g = g
        self.log = log
        self.idx = idx
        self.name = name
        self.pos = start
        self.midi_ch = midi_ch
        self.remaining = 1
        self.gate_rem = 1
        self.cur = None
        self.vel = 0x40
        self.volume = 0x7F
        self.program = None
        self.pan = 0x40
        self.notetab = None
        self.durtab = None
        self.counters = [0, 0, 0, 0]
        self.ended = False
        self.notes = 0
        self.pitches = []
        self.sounding = set()

    def state(self):
        return (self.pos, self.remaining, self.gate_rem, self.cur, self.vel, self.volume,
                self.program, self.pan, self.notetab, self.durtab, tuple(self.counters),
                self.ended, tuple(sorted(self.sounding)))

    def u8(self):
        v = self.blob[self.pos]
        self.pos += 1
        return v

    def u16(self):
        v = struct.unpack_from('<H', self.blob, self.pos)[0]
        self.pos += 2
        return v

    def all_off(self, t):
        for n in sorted(self.sounding):
            self.log.add(t, self.idx, 'off', n)
        self.sounding.clear()
        self.cur = None

    def tick(self, t):
        if self.ended:
            return
        self.gate_rem -= 1
        if self.gate_rem == 0:
            self.all_off(t)
        self.remaining -= 1
        if self.remaining != 0:
            return
        guard = 0
        while True:
            guard += 1
            if guard > 10000:
                raise ScoreError('%s: runaway command loop' % self.name)
            b = self.u8()
            if b < 0x80:
                if self.notetab is None or self.durtab is None:
                    raise ScoreError('%s: note before E9/F0 table select' % self.name)
                note = self.blob[self.notetab + (b & 0xF)]
                d = self.blob[self.durtab + 2 * (b >> 4)]
                gate = self.blob[self.durtab + 2 * (b >> 4) + 1]
                self.remaining = d
                if note == 0x80:
                    self.all_off(t)
                elif note == 0xFF:
                    lsb, msb = self.u8(), self.u8()
                    self.log.add(t, self.idx, 'bend', lsb, msb)
                else:
                    self.gate_rem = gate
                    if note != self.cur:
                        self.cur = note
                        if note in self.sounding:        # chord note retriggered
                            self.log.add(t, self.idx, 'off', note)
                        self.log.add(t, self.idx, 'on', note, self.vel, False)
                        self.sounding.add(note)
                        self.notes += 1
                        self.pitches.append(note)
                if self.remaining != 0:
                    return
                continue
            if b < 0xC0:
                self.pan = (b & 0x3F) * 2
                self.log.add(t, self.idx, 'cc', 10, self.pan)
            elif b == 0xE0:
                self.g.tempo = (~self.u8()) & 0xFF
                self.g.tempo_log.append((t, self.g.tempo))
            elif b == 0xE1:
                lsb, msb = self.u8(), self.u8()
                self.log.add(t, self.idx, 'bend', lsb, msb)
            elif b == 0xE2:
                self.vel = self.u8()
            elif b == 0xE5:
                self.volume = self.u8()
                self.log.add(t, self.idx, 'cc', 7, self.volume)
            elif b == 0xE6:
                self.program = self.u8()
                self.log.add(t, self.idx, 'program', self.program)
            elif b == 0xE7:
                ch = (self.u8() - 1) & 0xF
                if ch != self.midi_ch:
                    self.log.warn('%s: E7 changes the MIDI channel mid-track (%d -> %d)' % (self.name, self.midi_ch, ch))
                self.midi_ch = ch
            elif b == 0xE9:
                self.notetab = self.hdr.note_tables + 16 * self.u8()
            elif b == 0xF0:
                self.durtab = self.hdr.dur_tables + 16 * self.u8()
            elif b == 0xF1:
                i = self.u8()
                self.g.sync[i] = (self.g.sync[i] + 1) & 0xFF
            elif b == 0xF5:
                i = self.u8()
                self.counters[i & 3] = self.u8()
            elif b == 0xF6:
                i = self.u8()
                a = self.u16()
                self.counters[i & 3] = (self.counters[i & 3] - 1) & 0xFF
                if self.counters[i & 3] != 0:
                    self.pos = a
            elif b == 0xFB:
                self.pos = self.u16()
            elif b == 0xFF:
                self.ended = True
                self.all_off(t)
                return
            elif b in (0xE3, 0xE4, 0xE8, 0xEA, 0xEB, 0xEC, 0xED, 0xEE, 0xEF, 0xF2, 0xF3,
                       0xF4, 0xF7, 0xF8, 0xF9, 0xFA, 0xFC, 0xFD, 0xFE):
                pass
            else:
                raise ScoreError('%s: unknown MT op %02X' % (self.name, b))


# --------------------------------------------------------------------------
# simulation driver
MAX_TICKS = 400000


def simulate(chans, g, log):
    """Run all channels in lock-step until the state repeats or all end.
    Returns (end_tick, loop_start or None)."""
    seen = {}
    t = 0
    while t < MAX_TICKS:
        key = (g.tempo, tuple(g.sync), tuple(c.state() for c in chans))
        if key in seen:
            return t, seen[key]
        seen[key] = t
        for c in chans:
            c.tick(t)
        t += 1
        if all(c.ended for c in chans):
            return t, None
    raise ScoreError('no loop / end found within %d ticks' % MAX_TICKS)


def parse_file(path):
    d = open(path, 'rb').read()
    if len(d) < 4:
        raise ScoreError('file too short')
    la, lb = struct.unpack_from('<HH', d, 0)
    if 4 + la + lb != len(d):
        raise ScoreError('length mismatch: 4+%d+%d != %d' % (la, lb, len(d)))
    return d[4:4 + la], d[4 + la:4 + la + lb]


def convert(path, out, mode='adlib', loops=1, dump=False, tempo0=0x7F):
    A, B = parse_file(path)
    g = Globals(tempo0)
    log = Log()
    chans = []
    if mode == 'mt':
        hdr = BlobA(A)
        for i, (ch, off) in enumerate(hdr.chans):
            chans.append(MtChan(hdr, g, log, i, ch, off, 'mt%d' % i))
        chan_names = ['MT-32 part %d (ch %d)' % (i + 1, ch + 1) for i, (ch, off) in enumerate(hdr.chans)]
        midi_chs = [ch & 0xF for ch, _ in hdr.chans]
    else:
        hdr = BlobB(B)
        if mode == 'adlib':
            for i, off in enumerate(hdr.adlib_tracks):
                chans.append(MelodicChan(hdr, g, log, i, off, 'adlib', 'adlib%d' % i))
            chans.append(RhythmChan(hdr, g, log, 6, hdr.rhythm_track, 'rhythm'))
            chan_names = ['OPL2 ch %d' % i for i in range(6)] + ['OPL2 rhythm']
            midi_chs = [0, 1, 2, 3, 4, 5, 9]
        else:
            fl = 'std' if mode == 'std' else 'jr'
            for i, off in enumerate(hdr.jr_tracks):
                chans.append(MelodicChan(hdr, g, log, i, off, fl, '%s%d' % (fl, i)))
            chan_names = ['SN76496 ch %d' % i for i in range(3)]
            midi_chs = [0, 1, 2]
    g.tempo_log.append((0, g.tempo))
    end, loop_start = simulate(chans, g, log)

    if dump:
        for tick, tr, kind, args in log.ev:
            print('%6d %-8s %-7s %s' % (tick, chan_names[tr], kind, ' '.join(str(a) for a in args)))

    # ---- build MIDI
    name = os.path.basename(path)
    body_len = end - (loop_start if loop_start is not None else end)
    reps = loops if loop_start is not None else 1
    total = end + body_len * (reps - 1)
    tracks = [MidiTrack('%s (%s)' % (name, mode))]
    ctrl = tracks[0]
    tempo_map = []
    for tick, T in g.tempo_log:
        tempo_map.append((tick, T))
    # tempo events inside the loop body repeat with it
    def emit_time(tick):
        """yield every output tick an input tick maps to."""
        if loop_start is None or tick < loop_start:
            yield tick
            return
        for r in range(reps):
            yield tick + r * body_len

    last_T = None
    for tick, T in tempo_map:
        for ot in emit_time(tick):
            ctrl.tempo(ot, tempo_us_per_quarter(T))
    if loop_start is not None:
        ctrl.marker(loop_start, 'loop start')
        for r in range(reps):
            ctrl.marker(loop_start + (r + 1) * body_len, 'loop end' if r == reps - 1 else 'loop')
    per = []
    for i, cn in enumerate(chan_names):
        mt = MidiTrack(cn)
        tracks.append(mt)
        per.append(mt)
    # programs (heuristic for OPL, table for MT-32)
    gm_progs = {}
    if mode == 'adlib':
        for c in chans[:6]:
            for instr, (s, n) in c.instr_uses.items():
                gm_progs.setdefault(instr, gm_program_for_patch(hdr.opl_patch(instr), s / n))
    for i, ch in enumerate(midi_chs):
        if ch != 9:
            per[i].cc(0, ch, 7, 100)
            if mode != 'mt':
                per[i].program(0, ch, 80)
    # unroll events
    open_notes = {}
    for tick, tr, kind, args in log.ev:
        ch = midi_chs[tr]
        for ot in emit_time(tick):
            mt = per[tr]
            if kind == 'on':
                note, vel, _legato = args
                mt.note_on(ot, ch, note, vel)
            elif kind == 'off':
                mt.note_off(ot, ch, args[0])
            elif kind == 'drum':
                note, vel = args
                mt.note_on(ot, 9, note, vel)
                mt.note_off(ot + 1, 9, note)
            elif kind == 'instr':
                mt.program(ot, ch, gm_progs.get(args[0], 80))
            elif kind == 'vol':
                mt.cc(ot, ch, 7, args[0])
            elif kind == 'program':
                p = args[0] & 0x7F
                mt.program(ot, ch, MT32_TO_GM[p] if ch != 9 else 0)
            elif kind == 'cc':
                mt.cc(ot, ch, args[0], args[1])
            elif kind == 'bend':
                mt.bend(ot, ch, args[0], args[1])
    # notes still sounding at the very end: close them
    for c in chans:
        cur = getattr(c, 'cur_note', None)
        if cur is not None:
            per[c.idx].note_off(total, midi_chs[c.idx], cur)
        for n in getattr(c, 'sounding', ()):
            per[c.idx].note_off(total, midi_chs[c.idx], n)
    if out:
        write_midi(out, tracks, total)

    # ---- summary
    seconds = 0.0
    tl = []
    for tick, T in tempo_map:                     # insertion order; last one at a tick wins
        if tl and tl[-1][0] == tick:
            tl[-1] = (tick, T)
        else:
            tl.append((tick, T))
    for i, (tick, T) in enumerate(tl):
        nxt = tl[i + 1][0] if i + 1 < len(tl) else end
        seconds += (nxt - tick) / score_ticks_per_s(T)
    body_s = 0.0
    if loop_start is not None:
        for i, (tick, T) in enumerate(tl):
            nxt = tl[i + 1][0] if i + 1 < len(tl) else end
            a, b = max(tick, loop_start), nxt
            if b > a:
                body_s += (b - a) / score_ticks_per_s(T)
    pitches = [p for c in chans for p in getattr(c, 'pitches', [])]
    summary = {
        'file': path, 'mode': mode, 'tracks': len(chans),
        'notes': sum(c.notes for c in chans),
        'tempo': sorted(set(T for _, T in tempo_map)),
        'end_tick': end, 'loop_start': loop_start, 'seconds': seconds, 'loop_seconds': body_s,
        'pitch_min': min(pitches) if pitches else None,
        'pitch_max': max(pitches) if pitches else None,
        'per_track_notes': [c.notes for c in chans],
        'instruments': sorted(gm_progs) if mode == 'adlib' else
                        sorted(set(c.program for c in chans if getattr(c, 'program', None) is not None)),
        'warnings': log.warnings,
    }
    return summary


def note_name(n):
    return '%s%d' % (['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'][n % 12], n // 12 - 1)


def print_summary(s):
    ls = s['loop_start']
    print('%s [%s]: %d tracks, %d notes (%s), tempo %s, %d ticks = %.1f s%s, pitch %s..%s%s' % (
        os.path.basename(s['file']), s['mode'], s['tracks'], s['notes'],
        '/'.join(str(n) for n in s['per_track_notes']),
        '/'.join('0x%02X' % t for t in s['tempo']), s['end_tick'], s['seconds'],
        (', loop from tick %d (body %.1f s)' % (ls, s['loop_seconds'])) if ls is not None else ', ends',
        note_name(s['pitch_min']) if s['pitch_min'] is not None else '-',
        note_name(s['pitch_max']) if s['pitch_max'] is not None else '-',
        (' WARN: ' + '; '.join(s['warnings'])) if s['warnings'] else ''))


def find_score(name, root):
    arc, idx = SCORES[name]
    return os.path.join(root, 'extracted', 'ZELRES%d' % arc, 'dec', '%03d_data.dec' % idx)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('src', nargs='?', help='FILE.dec (or --all)')
    ap.add_argument('out', nargs='?', help='OUT.mid (or OUTDIR with --all)')
    ap.add_argument('--all', metavar='OUTDIR', help='convert every score (names from docs/RESOURCES.md)')
    ap.add_argument('--mt', action='store_true', help='convert blob A (MT-32 arrangement)')
    ap.add_argument('--jr', action='store_true', help='convert the 3 Tandy tracks of blob B')
    ap.add_argument('--std', action='store_true', help='Tandy tracks with the PC-speaker octave mapping')
    ap.add_argument('--loops', type=int, default=1, help='repeat the loop body N times (default 1)')
    ap.add_argument('--dump', action='store_true', help='print the decoded event stream')
    ap.add_argument('--md', action='store_true', help='with --all: print a markdown table')
    a = ap.parse_args()
    mode = 'mt' if a.mt else 'jr' if a.jr else 'std' if a.std else 'adlib'
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    if a.all:
        os.makedirs(a.all, exist_ok=True)
        rows = []
        fails = 0
        for name in SCORES:
            src = find_score(name, root)
            size = os.path.getsize(src)
            try:
                s = convert(src, os.path.join(a.all, name + '.mid'), 'adlib', a.loops)
                print_summary(s)
                sm = convert(src, os.path.join(a.all, name + '.mt32.mid'), 'mt', a.loops)
                print_summary(sm)
                sj = convert(src, os.path.join(a.all, name + '.tandy.mid'), 'jr', a.loops)
                print_summary(sj)
                rows.append((name, size, s, sm, sj))
            except ScoreError as e:
                fails += 1
                print('%s: FAILED: %s' % (name, e))
        if a.md:
            print()
            print('| Score | Resource | Size | Tempo | AdLib: tracks used / notes | Length | Loop | Range | MT-32: notes / patches |')
            print('|---|---|---|---|---|---|---|---|---|')
            for name, size, s, sm, sj in rows:
                arc, idx = SCORES[name]
                used = sum(1 for n in s['per_track_notes'] if n)
                loop = ('from %.1f s, body %.1f s' % (
                    s['seconds'] - s['loop_seconds'], s['loop_seconds'])) if s['loop_start'] is not None else 'none (ends)'
                print('| %s | ZELRES%d[%d] | %d | %s | %d / %d | %.1f s | %s | %s..%s | %d / %s |' % (
                    name, arc, idx, size, ', '.join('0x%02X' % t for t in s['tempo']), used, s['notes'],
                    s['seconds'], loop, note_name(s['pitch_min']), note_name(s['pitch_max']),
                    sm['notes'], ' '.join(MT32_NAMES[p] for p in sm['instruments'] if p < 128)))
        if fails:
            sys.exit(1)
        return
    if not a.src or not a.out:
        ap.error('need FILE.dec OUT.mid (or --all OUTDIR)')
    src = a.src
    if src in SCORES:
        src = find_score(src, root)
    try:
        s = convert(src, a.out, mode, a.loops, a.dump)
    except ScoreError as e:
        print('error: %s' % e, file=sys.stderr)
        sys.exit(1)
    print_summary(s)


if __name__ == '__main__':
    main()
