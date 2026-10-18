"""Verified guest call chain at a HalReturnToFirmware exit (or a watchdog dump).

Keeps only stack values that are the address right after a `call` instruction,
so stale data and saved registers drop out.

Usage: python -I scripts/exit_chain.py analysis/runN_stderr.txt [watchdog]
"""
import bisect
import glob
import json
import re
import sys

ASM = r"C:/Mechassault2/xboxrecomp/tools/disasm/output/asm/*.asm"
FUNCS = r"C:/Mechassault2/xboxrecomp/tools/disasm/output/functions.json"

funcs = json.load(open(FUNCS))
if isinstance(funcs, dict):
    funcs = funcs.get("functions", list(funcs.values()))
rows = sorted(((int(f["start"], 16) if isinstance(f["start"], str) else f["start"]),
               f.get("size", 1) or 1) for f in funcs)
starts = [r[0] for r in rows]

after_call = {}
for fn in glob.glob(ASM):
    prev = None
    for ln in open(fn, errors="replace"):
        m = re.match(r"  (0x[0-9A-F]{8})  [0-9a-f]+\s+(\S+)\s*(.*)", ln)
        if m:
            if prev:
                after_call[int(m.group(1), 16)] = prev
            prev = m.group(3).strip() if m.group(2) == "call" else None

text = open(sys.argv[1], errors="replace").read()
if len(sys.argv) > 2 and sys.argv[2] == "watchdog":
    i = text.index("[WATCHDOG]")
    seg = text[i:]
else:
    i = text.index("exit requested")
    seg = text[i:text.index("HalReturnToFirmware", i)]

for slot, val in re.findall(r"GS ([0-9A-F]{8}) ([0-9A-F]{8})", seg):
    v = int(val, 16)
    if v in after_call:
        j = bisect.bisect_right(starts, v) - 1
        print("%s  ret 0x%08X  in sub_%08X   (called %s)" % (slot, v, rows[j][0], after_call[v][:40]))
