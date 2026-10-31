"""tools/harness/schema.py -- read tools/harness/fields.tsv and decode / encode
a BASE image through it (docs/HARNESS.md §4).  Shared by tools/tracediff.py and
tools/harness/port2base.py; no third-party imports."""
import os
import re

HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_SCHEMA = os.path.join(HERE, "fields.tsv")
MODES = {"fight", "town", "any"}
SITE_MODE = {"717F": "fight", "68ED": "town"}


class Field:
    def __init__(self, name, addr, size, mode, note):
        self.name, self.addr, self.size, self.mode, self.note = name, addr, size, mode, note

    def applies(self, mode):
        return self.mode == "any" or self.mode == mode


class RecordList:
    def __init__(self, name, addr, ptr, stride, mode, maxn, end, dead, note):
        self.name, self.addr, self.ptr, self.stride, self.mode = name, addr, ptr, stride, mode
        self.maxn, self.end, self.dead, self.note = maxn, end, dead, note
        self.members = []                       # [(field, off, size, note)]

    def applies(self, mode):
        return self.mode == "any" or self.mode == mode


def _match(spec):
    m = re.fullmatch(r"\+([0-9A-Fa-f]+):([0-9]+):([0-9A-Fa-f]+)", spec)
    if not m:
        raise ValueError("bad match spec %r (want +OFF:SIZE:HEX)" % spec)
    return int(m.group(1), 16), int(m.group(2)), int(m.group(3), 16)


class Schema:
    def __init__(self, path=DEFAULT_SCHEMA):
        self.path = path
        self.fields = []                        # scalar Field rows
        self.lists = {}                         # name -> RecordList
        self.by_name = {}
        with open(path) as fh:
            for ln, raw in enumerate(fh, 1):
                line = raw.rstrip("\n")
                if not line.strip() or line.lstrip().startswith("#"):
                    continue
                cols = line.split("\t")
                if len(cols) < 4:
                    raise ValueError("%s:%d: want name addr size mode [note]" % (path, ln))
                name, addr, size, mode = cols[:4]
                note = cols[4] if len(cols) > 4 else ""
                if mode not in MODES:
                    raise ValueError("%s:%d: bad mode %r" % (path, ln, mode))
                where = "%s:%d" % (path, ln)
                if name.endswith("[]"):
                    self._list_header(name[:-2], addr, size, mode, note, where)
                elif "[]." in name:
                    lname, member = name.split("[].", 1)
                    if lname not in self.lists:
                        raise ValueError("%s: member of undeclared list %s" % (where, lname))
                    if not addr.startswith("+"):
                        raise ValueError("%s: member offset must be +HEX" % where)
                    self.lists[lname].members.append((member, int(addr[1:], 16), int(size), note))
                else:
                    f = Field(name, int(addr, 16), int(size), mode, note)
                    if name in self.by_name:
                        raise ValueError("%s: duplicate field %s" % (where, name))
                    self.fields.append(f)
                    self.by_name[name] = f

    def _list_header(self, name, addr, stride, mode, note, where):
        spec = note.split(" -- ", 1)[0]
        maxn, end, dead = 256, None, None
        for tok in spec.split():
            k, _, v = tok.partition("=")
            if k == "max":
                maxn = int(v, 0)
            elif k == "end":
                end = _match(v)
            elif k == "dead":
                dead = _match(v)
            else:
                raise ValueError("%s: unknown list spec %r" % (where, tok))
        ptr = addr.startswith("[")
        a = int(addr.strip("[]"), 16)
        self.lists[name] = RecordList(name, a, ptr, int(stride), mode, maxn, end, dead, note)

    # ------------------------------------------------------------------ names
    def knows(self, name):
        """is `name` a field the schema can decode (scalar or list-derived)?"""
        if name in self.by_name:
            return True
        m = re.fullmatch(r"([A-Za-z_]\w*)(?:\.n|\[(\d+)\]\.(\w+))", name)
        if not m or m.group(1) not in self.lists:
            return False
        if name.endswith(".n") and m.group(2) is None:
            return True
        L = self.lists[m.group(1)]
        member = m.group(3)
        return member in [x[0] for x in L.members] or (member == "live" and L.dead is not None)

    @staticmethod
    def list_of(name):
        m = re.match(r"([A-Za-z_]\w*)(?:\.n$|\[)", name)
        return m.group(1) if m else None

    def mode_of(self, name):
        if name in self.by_name:
            return self.by_name[name].mode
        lname = self.list_of(name)
        return self.lists[lname].mode if lname in self.lists else None

    # ----------------------------------------------------------------- decode
    @staticmethod
    def _get(base, addr, size):
        addr &= 0xFFFF
        if addr + size <= 0x10000:
            b = bytes(base[addr:addr + size])
        else:
            b = bytes(base[(addr + i) & 0xFFFF] for i in range(size))
        if size <= 2:
            return int.from_bytes(b, "little")
        return b.hex()

    def list_start(self, L, base):
        return self._get(base, L.addr, 2) if L.ptr else L.addr

    def decode(self, base, mode):
        """{name: value} for every field of `mode` in one 64 KB BASE image"""
        out = {}
        for f in self.fields:
            if f.applies(mode):
                out[f.name] = self._get(base, f.addr, f.size)
        for L in self.lists.values():
            if not L.applies(mode):
                continue
            p = self.list_start(L, base)
            n = 0
            while n < L.maxn:
                rec = (p + n * L.stride) & 0xFFFF
                if L.end and self._get(base, rec + L.end[0], L.end[1]) == L.end[2]:
                    break
                if L.dead is not None:
                    live = self._get(base, rec + L.dead[0], L.dead[1]) != L.dead[2]
                    out["%s[%d].live" % (L.name, n)] = int(live)
                    if not live:
                        n += 1
                        continue
                for member, off, size, _ in L.members:
                    out["%s[%d].%s" % (L.name, n, member)] = self._get(base, rec + off, size)
                n += 1
            out[L.name + ".n"] = n
        return out

    # ----------------------------------------------------------------- encode
    def encode(self, fields, mode, base, list_area=0xD000):
        """write a port trace's `fields` into the bytearray `base` (port2base).
        A [PTR] list is laid out from `list_area` and the pointer set to it."""
        for name, v in fields.items():
            f = self.by_name.get(name)
            if f is None or not f.applies(mode):
                continue
            self._put(base, f.addr, f.size, v)
        for L in self.lists.values():
            if not L.applies(mode) or (L.name + ".n") not in fields:
                continue
            n = fields[L.name + ".n"]
            p = list_area if L.ptr else L.addr
            if L.ptr:
                self._put(base, L.addr, 2, p)
                list_area += (n + 1) * L.stride
            for i in range(n):
                rec = p + i * L.stride
                pre = "%s[%d]." % (L.name, i)
                if L.dead is not None and not fields.get(pre + "live", 1):
                    self._put(base, rec + L.dead[0], L.dead[1], L.dead[2])
                    continue
                for member, off, size, _ in L.members:
                    if pre + member in fields:
                        self._put(base, rec + off, size, fields[pre + member])
            if L.end and n < L.maxn:
                self._put(base, p + n * L.stride + L.end[0], L.end[1], L.end[2])
        return base

    @staticmethod
    def _put(base, addr, size, v):
        if isinstance(v, str):
            b = bytes.fromhex(v)[:size].ljust(size, b"\0")
        else:
            b = int(v).to_bytes(size, "little")
        for i in range(size):
            base[(addr + i) & 0xFFFF] = b[i]
