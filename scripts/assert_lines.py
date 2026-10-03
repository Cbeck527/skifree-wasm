#!/usr/bin/env python3
"""List the ski2.c assert line numbers found in each decompiled function.

    scripts/assert_lines.py [analysis/ski32/decompiled-game.c]

Prints address, name, size and the min-max assert line per function, in
address order. Line ranges are approximate source positions, not function
boundaries (helpers are inlined).
"""
import re
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "analysis/ski32/decompiled-game.c"
header = re.compile(r"// ---- (\S+) @ (\S+) \((\d+) bytes")
call = re.compile(r"(?:AssertFailed|FUN_00401240)\(s_\w+_ski2_c_\w+,(0x[0-9a-f]+|\d+)\)")

funcs, cur = [], None
for line in open(path):
    m = header.match(line)
    if m:
        cur = (m.group(2), m.group(1), int(m.group(3)), [])
        funcs.append(cur)
    elif cur:
        cur[3].extend(int(x, 0) for x in call.findall(line))

for addr, name, size, lines in funcs:
    if lines:
        print(f"{addr}  {name:24s} {size:5d}b  lines {min(lines):5d}-{max(lines):5d}")
