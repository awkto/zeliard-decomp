# Ghidra headless post-script (Jython): decompile all functions to one C file.
# usage: analyzeHeadless proj -import bin -postScript ghidra_dump_c.py <outfile>
import sys
from ghidra.app.decompiler import DecompInterface
from ghidra.util.task import ConsoleTaskMonitor

args = getScriptArgs()
out_path = args[0] if args else "/tmp/decomp.c"

# Seed functions from the overlay's entry-vector table at the image base:
# leading words are near pointers; the first one marks the table's end.
from ghidra.program.model.symbol import SourceType
mem = currentProgram.getMemory()
base = currentProgram.getMinAddress()
space = base.getAddressSpace()
seg_off = base.getOffset()
img_end = seg_off + mem.getSize()


def word_at(addr):
    return mem.getShort(addr) & 0xFFFF


def seed(addr, name):
    disassemble(addr)
    if getFunctionAt(addr) is None:
        createFunction(addr, name)


def seed_table(off, count, prefix):
    for i in range(count):
        tgt = word_at(space.getAddress(off + i * 2))
        if seg_off <= tgt < img_end:
            seed(space.getAddress(tgt), "%s%02d_%04x" % (prefix, i, tgt))


# Optional explicit seeds (arg 2, comma-separated):
#   0x100          -> entry point at that offset
#   table:0x10C:11 -> near-pointer vector table of N entries at that offset
seeded = False
if len(args) > 1 and args[1]:
    for spec in args[1].split(","):
        spec = spec.strip()
        if spec.startswith("table:"):
            _, off, n = spec.split(":")
            seed_table(int(off, 16), int(n, 0), "vec_")
        else:
            a = space.getAddress(int(spec, 16))
            seed(a, "entry_%04x" % a.getOffset())
        seeded = True

# Default: overlay-style table at the image base whose first word marks its end.
first = word_at(base)
if not seeded and seg_off < first < img_end and (first - seg_off) % 2 == 0:
    seed_table(seg_off, (first - seg_off) // 2, "vec_")
    seeded = True
if not seeded:
    seed(base, "entry_%04x" % seg_off)
analyzeChanges(currentProgram)

ifc = DecompInterface()
ifc.openProgram(currentProgram)
monitor = ConsoleTaskMonitor()

lines = []
fm = currentProgram.getFunctionManager()
for func in fm.getFunctions(True):
    res = ifc.decompileFunction(func, 60, monitor)
    lines.append("/* ===== %s @ %s ===== */" % (func.getName(), func.getEntryPoint()))
    if res.decompileCompleted():
        lines.append(res.getDecompiledFunction().getC())
    else:
        lines.append("/* decompile failed: %s */" % res.getErrorMessage())

f = open(out_path, "w")
f.write("\n".join(lines))
f.close()
print("wrote %s (%d functions)" % (out_path, fm.getFunctionCount()))
