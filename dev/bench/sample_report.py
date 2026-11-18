#!/usr/bin/env python3
"""Report of the sampling profiler of the bench (BENCH_SAMPLE of bench2.cpp).

sample_report.py <program> <samples file> [top] [focus]

Prints the share of the samples by function: own time (the function was running) and time with
everything it called. Inlined functions are named by the debug information (addr2line -i).
focus: a part of a function name; the callers and the callees of the functions with that name
and their own time by source line are printed too.
"""
import subprocess
import sys
from collections import Counter, defaultdict


def resolve(program, addrs):
    """address -> list of (function, file:line), the innermost inlined function first"""
    out = {}
    if not addrs:
        return out
    order = sorted(addrs)
    res = subprocess.run(["addr2line", "-a", "-i", "-f", "-C", "-e", program],
                         input="\n".join("%x" % a for a in order), capture_output=True, text=True).stdout
    cur = None
    lines = res.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        if line.startswith("0x"):
            cur = int(line, 16)
            out[cur] = []
            i += 1
            continue
        if cur is not None and i + 1 < len(lines) and line:
            func = line
            where = lines[i + 1].split("/")[-1].split(" ")[0]
            # the arguments make the names too long to read
            cut = func.find("(")
            if cut > 0:
                func = func[:cut]
            out[cur].append((func, where))
            i += 2
            continue
        i += 1
    return out


def main():
    program, samples = sys.argv[1], sys.argv[2]
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 45
    focus = sys.argv[4] if len(sys.argv) > 4 else None
    stacks = []
    base = end = 0
    with open(samples) as f:
        for line in f:
            parts = line.split()
            if not parts:
                continue
            if parts[0] == "range":
                base, end = int(parts[1], 16), int(parts[2], 16)
                continue
            # the two first frames are the signal handler and the return of the signal
            frames = [int(p, 16) for p in parts][2:]
            if frames:
                stacks.append(frames)
    addrs = set()
    for st in stacks:
        for i, a in enumerate(st):
            if base <= a < end:
                # a return address points after the call: one byte back is inside the call
                addrs.add(a - base - (1 if i else 0))
    names = resolve(program, addrs)

    def chain(st, i):
        a = st[i]
        if not (base <= a < end):
            return [("[libc and other libraries]", "")]
        return names.get(a - base - (1 if i else 0)) or [("?", "")]

    own = Counter()
    ownLine = Counter()
    incl = Counter()
    callers = defaultdict(Counter)
    callees = defaultdict(Counter)
    libOwner = Counter()
    for st in stacks:
        flat = []  # the functions from the innermost to the outermost
        for i in range(len(st)):
            flat.extend(chain(st, i))
        func, where = flat[0]
        own[func] += 1
        ownLine[(func, where)] += 1
        if func.startswith("[libc"):
            # the time of libc belongs to the first function of the program that called it
            for f2, w2 in flat[1:]:
                if not f2.startswith("[libc"):
                    libOwner[f2] += 1
                    break
        seen = set()
        for k, (f2, _) in enumerate(flat):
            if f2 not in seen:
                seen.add(f2)
                incl[f2] += 1
        if focus:
            done = set()
            for k, (f2, _) in enumerate(flat):
                if focus in f2 and f2 not in done:
                    done.add(f2)
                    if k + 1 < len(flat):
                        callers[f2][flat[k + 1][0]] += 1
                    callees[f2][flat[k - 1][0] if k > 0 else "(own)"] += 1
    total = len(stacks)
    print("%d samples of 1 ms" % total)
    print("\nOWN TIME")
    for func, n in own.most_common(top):
        print("%6.2f%%  %s" % (100.0 * n / total, func))
    print("\nTIME OF libc BY THE CALLING FUNCTION")
    for func, n in libOwner.most_common(15):
        print("%6.2f%%  %s" % (100.0 * n / total, func))
    print("\nTIME WITH EVERYTHING CALLED")
    for func, n in incl.most_common(top):
        print("%6.2f%%  %s" % (100.0 * n / total, func))
    if focus:
        for f2 in sorted(callers.keys() | callees.keys()):
            print("\n=== %s: %.2f%% with everything called, %.2f%% own" %
                  (f2, 100.0 * incl[f2] / total, 100.0 * own[f2] / total))
            print("  called from:")
            for c, n in callers[f2].most_common(8):
                print("  %6.2f%%  %s" % (100.0 * n / total, c))
            print("  the time goes to:")
            for c, n in callees[f2].most_common(20):
                print("  %6.2f%%  %s" % (100.0 * n / total, c))
            print("  own time by line:")
            lines = [(n, w) for (f3, w), n in ownLine.items() if f3 == f2]
            for n, w in sorted(lines, reverse=True)[:15]:
                print("  %6.2f%%  %s" % (100.0 * n / total, w))


main()
