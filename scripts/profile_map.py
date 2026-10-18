"""Resolve analysis/profile.txt (from MA2_PROFILE=1) against build/Release/ma2.map.

Usage: python -I scripts/profile_map.py [profile.txt] [ma2.map] [top]
Prints, per sampled thread, the hottest functions by sample share.
"""
import bisect
import re
import sys
from collections import Counter

prof = sys.argv[1] if len(sys.argv) > 1 else "analysis/profile.txt"
mapf = sys.argv[2] if len(sys.argv) > 2 else "build/Release/ma2.map"
top = int(sys.argv[3]) if len(sys.argv) > 3 else 25

# MSVC map: " 0001:00000000  sub_00012000  0000000140001000 f   recomp_0000.obj"
syms = []
pref = None
for line in open(mapf, errors="replace"):
    m = re.match(r"\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s+(?:f\s+)?(\S+)?", line)
    if m:
        syms.append((int(m.group(2), 16), m.group(1), m.group(3) or ""))
    m2 = re.search(r"Preferred load address is ([0-9a-fA-F]+)", line)
    if m2:
        pref = int(m2.group(1), 16)
syms.sort()
addrs = [s[0] for s in syms]
pref = pref or 0x140000000


def name(off):
    va = pref + off
    i = bisect.bisect_right(addrs, va) - 1
    if i < 0:
        return "?"
    return "%s (%s)" % (syms[i][1], syms[i][2])


thread = None
per = {}
for line in open(prof, errors="replace"):
    m = re.match(r"thread (\d+) samples (\d+)", line)
    if m:
        thread = (m.group(1), int(m.group(2)))
        per[thread] = Counter()
        continue
    m = re.match(r"\s+(\d+) exe\+0x([0-9A-Fa-f]+)", line)
    if m and thread:
        per[thread][name(int(m.group(2), 16))] += int(m.group(1))
        continue
    m = re.match(r"\s+(\d+) mod (\S+)\+0x([0-9A-Fa-f]+)", line)
    if m and thread:
        per[thread]["[dll] " + m.group(2)] += int(m.group(1))
        continue
    m = re.match(r"\s+(\d+) abs 0x([0-9A-Fa-f]+)", line)
    if m and thread:
        per[thread]["<outside exe, e.g. system DLL>"] += int(m.group(1))

for (tid, total), c in sorted(per.items(), key=lambda kv: -kv[0][1]):
    print("=== thread %s, %d samples" % (tid, total))
    for fn, n in c.most_common(top):
        print("  %5.1f%%  %s" % (100.0 * n / total, fn))
