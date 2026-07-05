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


def word_at(addr):
    return mem.getShort(addr) & 0xFFFF


first = word_at(base)
img_end = seg_off + mem.getSize()
if seg_off <= first < img_end:
    count = (first - seg_off) // 2
    for i in range(count):
        tgt = word_at(base.add(i * 2))
        if not (seg_off <= tgt < img_end):
            continue
        a = space.getAddress(tgt)
        disassemble(a)
        if getFunctionAt(a) is None:
            createFunction(a, "vec_%02d_%04x" % (i, tgt))
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
