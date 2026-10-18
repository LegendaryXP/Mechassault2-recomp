"""Map return addresses in a watchdog guest-stack dump to lifted functions.

Usage: python -I scripts/guest_stack.py analysis/runN_stderr.txt [functions.json] [max]
"""
import bisect
import json
import re
import sys

log = sys.argv[1]
fj = sys.argv[2] if len(sys.argv) > 2 else r"C:/Mechassault2/xboxrecomp/tools/disasm/output/functions.json"
limit = int(sys.argv[3]) if len(sys.argv) > 3 else 30

funcs = json.load(open(fj))
if isinstance(funcs, dict):
    funcs = funcs.get("functions", list(funcs.values()))
rows = []
for f in funcs:
    s = f.get("start") or f.get("address")
    s = int(s, 16) if isinstance(s, str) else s
    rows.append((s, f.get("size", 0) or 1))
rows.sort()
starts = [r[0] for r in rows]

# Section map from the loader's own listing at the top of the log.
text = open(log, errors="replace").read()
sections = [(int(va, 16), int(sz), name.strip())
            for name, va, sz in re.findall(r"\[\s*\d+\] (.{12}) VA=0x([0-9A-F]{8}) vsize=(\d+)", text)]


def section(a):
    for va, sz, name in sections:
        if va <= a < va + sz:
            return name
    return "?"


def owner(a):
    i = bisect.bisect_right(starts, a) - 1
    if i >= 0 and a < rows[i][0] + rows[i][1]:
        return rows[i][0]
    return None


n = 0
for slot, val in re.findall(r"GS ([0-9A-F]{8}) ([0-9A-F]{8})", text):
    v = int(val, 16)
    o = owner(v) if 0x12000 <= v < 0x425000 else None
    if o is not None:
        print("%s  ret 0x%08X  in sub_%08X  [%s]" % (slot, v, o, section(v)))
        n += 1
        if n >= limit:
            break
