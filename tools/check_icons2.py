#!/usr/bin/env python3
"""Validate the regenerated HUD icon pack (2swat4-hud-icons.zip).

Checks per PNG: opens, declared size vs manifest (fogo=192, rest=128),
RGBA/alpha present, transparent border (outer 2px fully transparent),
non-trivial content (>=3% non-transparent pixels), no fully opaque frame.
Prints a table and a final OK/FAIL summary. Exit 0 iff all pass.
"""
import sys, os
from PIL import Image

def check(path, want):
    im = Image.open(path)
    im.load()
    errs = []
    if im.size != (want, want):
        errs.append(f"size {im.size} != {want}x{want}")
    if im.mode not in ("RGBA", "LA", "PA"):
        errs.append(f"mode {im.mode} has no alpha")
        return errs, im
    rgba = im.convert("RGBA")
    a = rgba.getchannel("A")
    w, h = rgba.size
    px = a.load()
    # border fully transparent (outer 2 px)
    border_bad = 0
    for i in range(w):
        for y in (0, 1, h-2, h-1):
            if px[i, y] != 0: border_bad += 1
        for x in (0, 1, w-2, w-1):
            if px[x, i] != 0: border_bad += 1
    if border_bad: errs.append(f"border not transparent ({border_bad} px)")
    # content ratio
    hist = a.histogram()
    opaque = sum(hist[200:])
    total = w*h
    if opaque < total*0.03:
        errs.append(f"nearly empty ({opaque*100//total}% opaque)")
    return errs, rgba

def main():
    d = sys.argv[1] if len(sys.argv) > 1 else "."
    names = sorted(f for f in os.listdir(d) if f.endswith(".png"))
    if len(names) != 42:
        print(f"WARN: {len(names)} PNGs (expected 42)")
    fails = 0
    for n in names:
        want = 192 if n == "fogo.png" else 128
        errs, rgba = check(os.path.join(d, n), want)
        if errs:
            fails += 1
            print(f"FAIL {n}: {'; '.join(errs)}")
        else:
            print(f"ok   {n} ({want}x{want})")
    print(f"\n{len(names)-fails} ok / {fails} fail")
    sys.exit(1 if fails else 0)

if __name__ == "__main__":
    main()
