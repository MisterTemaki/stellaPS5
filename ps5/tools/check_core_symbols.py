#!/usr/bin/env python3
"""Stella PS5: no two parts of the eboot define the same global name (ps5/cores.mk).

  check_core_symbols.py <nm> <stamp> <group>=<object>,<object>... ...

Each group is a core (Stella's objects, a5200's, ProSystem's, Gearlynx's) or the frontend. A name two groups define
(strongly or weakly: a C++ inline function, a template) would be one function or variable for both at the final link,
taken from one of them: with two different meanings that is a silent bug. The C++ standard library's templates
(std::..., the same code in every group) are the exception. Exit 1 (and the names) when there is one; else <stamp>
is written.
"""
import subprocess
import sys

nm, stamp = sys.argv[1], sys.argv[2]
groups = {}
for arg in sys.argv[3:]:
    name, objs = arg.split("=", 1)
    groups[name] = [o for o in objs.split(",") if o]


def library_name(sym):
    # the standard library's and the compiler's own: libc++ (std::__1), libstdc++ (std::, __gnu_cxx), sanitizers
    if sym.startswith(("DW.ref.", "__asan", "___asan", "__odr_asan", "__sanitizer", "__ubsan", "__clang", "__cxa", "__gxx")):
        return True
    for p in ("_ZNSt", "_ZNKSt", "_ZSt", "_ZTVNSt", "_ZTINSt", "_ZTSNSt", "_ZTVSt", "_ZTISt", "_ZTSSt", "_ZGVZNSt",
              "_ZZNSt", "_ZZNKSt", "_ZN9__gnu_cxx", "_ZNK9__gnu_cxx", "_ZThn", "_ZTv", "_ZGVNSt", "_ZNSt3__1",
              "_ZNKSt3__1", "_ZNVKSt"):
        if sym.startswith(p):
            return True
    # function templates of std:: called with a group's own types are named after std:: too ("void std::sort<...>")
    if sym.startswith("_Z") and ("St3__1" in sym[:12] or sym.startswith(("_ZSt", "_ZNSt"))):
        return True
    return False


owner = {}
bad = []
for g, objs in groups.items():
    seen = set()
    for o in objs:
        out = subprocess.run([nm, "-g", "--defined-only", o], capture_output=True, text=True, check=True).stdout
        for line in out.splitlines():
            parts = line.split()
            if len(parts) < 3:
                continue
            sym = parts[2]
            if sym in seen or library_name(sym):
                continue
            seen.add(sym)
            if sym in owner and owner[sym] != g:
                bad.append(f"{sym}: {owner[sym]} and {g}")
            owner.setdefault(sym, g)
if bad:
    print("check_core_symbols: names defined by two parts of the eboot (rename one: frontend/fe_gearlynx_names.h):")
    for b in bad[:60]:
        print("  " + b)
    if len(bad) > 60:
        print(f"  ... and {len(bad) - 60} more")
    sys.exit(1)
open(stamp, "w").write(f"{len(owner)} names, no two parts share one\n")
print(f"check_core_symbols: {len(owner)} names, no two parts share one")
