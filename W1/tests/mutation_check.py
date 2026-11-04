#!/usr/bin/env python3
"""mutation_check.py - do the tests for min_heap / huffman_types / symbols notice a bug?

Run from the repo root:   python3 W1/tests/mutation_check.py
For each "mutant" it copies W1/ and shared/ to a temp folder, makes ONE small deliberate
mistake in one of the four files, builds test_min_heap, test_types and test_symbols, and runs
them. A mutant is KILLED if the build fails or any test fails (the tests noticed the bug),
and SURVIVED if everything still passes (the tests missed it, or the change did not alter behaviour).
"""
import os, shutil, subprocess, sys, tempfile

TESTS = ["test_min_heap", "test_types", "test_symbols"]
H, C, T, S = "W1/include/min_heap.h", "W1/src/min_heap.cpp", "W1/include/huffman_types.h", "shared/include/symbols.hpp"

MUTANTS = [  # (file, original text, broken text, what the mistake is)
 (H, "a.weight == b.weight && a.id < b.id", "a.weight == b.weight && a.id > b.id", "tie-break reversed (larger id first)"),
 (H, "a.weight < b.weight ||", "a.weight > b.weight ||", "comparator turned into a max-heap"),
 (H, "kCapacity = 320", "kCapacity = 319", "capacity off by one"),
 (C, "heapLess(items_[child + 1], items_[child])", "heapLess(items_[child], items_[child + 1])", "sift-down picks the LARGER child"),
 (C, "if (size_ == kCapacity) return false;", "if (size_ == kCapacity - 1) return false;", "heap refuses the 320th item"),
 (C, "if (!heapLess(item, items_[parent])) break;", "if (heapLess(item, items_[parent])) break;", "sift-up condition inverted"),
 (C, "if (!heapLess(items_[child], last)) break;", "if (heapLess(items_[child], last)) break;", "sift-down stop condition inverted"),
 (T, "(totalBits() + 7) / 8", "totalBits() / 8", "totalBytes rounds down instead of up"),
 (T, "return headerBits + payloadBits;", "return payloadBits;", "totalBits forgets the header"),
 (T, "* 4;", "* 5;", "header uses 5 bits per length"),
 (T, "kFastBits = 10", "kFastBits = 9", "fast table of 9 bits instead of 10"),
 (S, "11, 13, 15, 17", "11, 12, 15, 17", "one wrong length base"),
 (S, "1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6,", "1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 7,", "one wrong distance extra-bit count"),
 (S, "<< 16) | (dist - 1)", "<< 15) | (dist - 1)", "length stored one bit too low in the match word"),
 (S, "(dist - 1) < 256 ?", "(dist - 1) < 255 ?", "distLow/distHigh switch off by one"),
 (S, "n + 2 <= kBlockTokens", "n + 2 < kBlockTokens", "hasRoomForToken off by one"),
 (S, "litlenFreq[kEob] = 1;", "litlenFreq[kEob] = 0;", "reset forgets the EOB count"),
 (S, "+ kTables.distExtra[ds];", "+ 0;", "extraBitsTotal ignores distance extra bits"),
 (S, "if (t.length != 0) addMatch", "if (t.length > 3) addMatch", "addToken ignores matches of length 3"),
 (S, "kMaxMatch    = 258", "kMaxMatch    = 257", "maximum match length 257"),
 (S, "++litlenFreq[b];", "++litlenFreq[b & 0x7F];", "literal counted in the wrong slot"),
]

def build_and_run(root):
    inc = ["-I", f"{root}/W1/include", "-I", f"{root}/shared/include", "-I", f"{root}/W1/tests"]
    objs = []
    for src in sorted(os.listdir(f"{root}/W1/src")):
        if not src.endswith(".cpp"): continue
        o = f"{root}/{src}.o"
        r = subprocess.run(["g++", "-std=c++17", "-O1", "-c", f"{root}/W1/src/{src}", "-o", o] + inc, capture_output=True, text=True)
        if r.returncode: return "build error"
        objs.append(o)
    for t in TESTS:
        exe = f"{root}/{t}"
        r = subprocess.run(["g++", "-std=c++17", "-O1", f"{root}/W1/tests/{t}.cpp"] + objs + ["-o", exe] + inc, capture_output=True, text=True)
        if r.returncode: return f"{t}: does not compile"
        r = subprocess.run([exe], capture_output=True, text=True)
        if r.returncode: return f"{t}: test failed"
    return None

def main():
    if not os.path.isdir("W1") or not os.path.isdir("shared"):
        sys.exit("Run this from the repo root (the folder that contains W1/ and shared/).")
    base = tempfile.mkdtemp()
    shutil.copytree("W1", f"{base}/W1"); shutil.copytree("shared", f"{base}/shared")
    shutil.rmtree(f"{base}/W1/tests/__pycache__", ignore_errors=True)
    sanity = build_and_run(base)
    if sanity: sys.exit(f"The UNMODIFIED code already fails ({sanity}). Fix that first.")
    print("Unmodified code: all three test programs pass.\n")
    killed = survived = notfound = 0
    for i, (path, old, new, what) in enumerate(MUTANTS, 1):
        work = tempfile.mkdtemp()
        shutil.copytree("W1", f"{work}/W1"); shutil.copytree("shared", f"{work}/shared")
        text = open(f"{work}/{path}").read()
        if old not in text:
            print(f"{i:2d}. NOT APPLIED (pattern not found in {path}): {what}"); notfound += 1; shutil.rmtree(work); continue
        open(f"{work}/{path}", "w").write(text.replace(old, new, 1))
        why = build_and_run(work)
        if why: print(f"{i:2d}. KILLED   {os.path.basename(path):17s} {what}   [{why}]"); killed += 1
        else:   print(f"{i:2d}. SURVIVED {os.path.basename(path):17s} {what}"); survived += 1
        shutil.rmtree(work)
    total = killed + survived
    print(f"\n{killed} of {total} applied mutants killed" + (f", {survived} survived" if survived else "") + (f", {notfound} not applied" if notfound else ""))
    shutil.rmtree(base, ignore_errors=True)
    sys.exit(1 if survived or notfound else 0)

main()

