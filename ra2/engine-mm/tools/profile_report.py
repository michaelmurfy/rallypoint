#!/usr/bin/env python3
"""R-171: turn an RA2_PROFILE sample file into a per-function report.

usage: profile_report.py <ra2engine.exe> <profile.txt> [top_n]

Symbols come from `nm -C` on the (unstripped MinGW) exe; each sampled
module-relative address is charged to the nearest preceding text symbol.
Prints the top functions by SELF samples (where the CPU was) and by
INCLUSIVE samples (on the stack).
"""
import bisect
import subprocess
import sys
from collections import Counter


def symbols(exe):
    out = subprocess.run(["nm", "-C", "--defined-only", exe], capture_output=True, text=True).stdout
    syms = []
    image_base = None
    for line in out.splitlines():
        parts = line.split(" ", 2)
        if len(parts) < 3 or parts[1] not in ("T", "t"):
            if len(parts) == 3 and parts[2] == "__image_base__":
                image_base = int(parts[0], 16)
            continue
        syms.append((int(parts[0], 16), parts[2]))
    if image_base is None:
        image_base = 0x140000000
    syms.sort()
    return image_base, [a for a, _ in syms], [n for _, n in syms]


def main():
    exe, prof = sys.argv[1], sys.argv[2]
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 25
    base, addrs, names = symbols(exe)
    self_c, incl_c = Counter(), Counter()
    total = 0
    for line in open(prof):
        if line.startswith("# samples"):
            total = int(line.split()[2])
            continue
        kind, rva, n = line.split()
        va = base + int(rva, 16)
        i = bisect.bisect_right(addrs, va) - 1
        name = names[i] if 0 <= i < len(names) and va - addrs[i] < 1 << 20 else "?(outside exe)"
        (self_c if kind == "self" else incl_c)[name] += int(n)
    print(f"samples: {total}")
    for title, c in (("SELF", self_c), ("INCLUSIVE", incl_c)):
        print(f"\n== top {top} by {title} ==")
        for name, n in c.most_common(top):
            print(f"{100.0 * n / max(1, total):6.1f}%  {n:7d}  {name[:150]}")


if __name__ == "__main__":
    main()
