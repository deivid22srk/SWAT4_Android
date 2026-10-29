#!/usr/bin/env python3
"""Phase 1 recon: PE facts, hashes and imports for every SWAT 4 module.

Usage: tools/recon_swat4.py "SWAT 4 root" out.json
Reads only the user's own files; writes a JSON report (no binaries) that
tools/imports_report.py turns into docs/IMPORTS.md.
"""
import hashlib, json, os, sys
import pefile

OWNER = {0x14c: "i386", 0x8664: "amd64"}


def digest(p, algo):
    h = hashlib.new(algo)
    with open(p, "rb") as f:
        for b in iter(lambda: f.read(1 << 20), b""):
            h.update(b)
    return h.hexdigest()


def info(p):
    pe = pefile.PE(p, fast_load=True)
    pe.parse_data_directories()
    d = {"path": p, "size": os.path.getsize(p), "sha1": digest(p, "sha1"),
         "sha256": digest(p, "sha256"),
         "machine": hex(pe.FILE_HEADER.Machine),
         "machine_name": OWNER.get(pe.FILE_HEADER.Machine, "?"),
         "dll": bool(pe.FILE_HEADER.Characteristics & 0x2000),
         "imagebase": hex(pe.OPTIONAL_HEADER.ImageBase),
         "sizeofimage": hex(pe.OPTIONAL_HEADER.SizeOfImage),
         "entry": hex(pe.OPTIONAL_HEADER.AddressOfEntryPoint),
         "nimports": 0, "nexports": 0, "imports": {}, "import_dlls": [],
         "has_reloc": hasattr(pe, "DIRECTORY_ENTRY_BASERELOC"),
         "sections": [s.Name.rstrip(b"\0").decode("ascii", "replace") for s in pe.sections]}
    if hasattr(pe, "DIRECTORY_ENTRY_IMPORT"):
        for e in pe.DIRECTORY_ENTRY_IMPORT:
            name = e.dll.decode("ascii", "replace")
            fns = [i.name.decode("ascii", "replace") if i.name else f"ord{i.ordinal}"
                   for i in e.imports]
            d["imports"][name] = fns
        d["nimports"] = sum(len(v) for v in d["imports"].values())
        d["import_dlls"] = sorted(d["imports"])
    if hasattr(pe, "DIRECTORY_ENTRY_EXPORT") and pe.DIRECTORY_ENTRY_EXPORT:
        d["nexports"] = len(pe.DIRECTORY_ENTRY_EXPORT.symbols)
    end = max((s.PointerToRawData + s.SizeOfRawData for s in pe.sections), default=0)
    d["overlay"] = os.path.getsize(p) - end
    pe.close()
    return d


def main():
    root, out = sys.argv[1], sys.argv[2]
    rep = {}
    for sub, tag in [("Content/System", "base"), ("ContentExpansion/System", "exp")]:
        d = os.path.join(root, sub)
        files = {}
        for f in sorted(os.listdir(d)):
            p = os.path.join(d, f)
            if os.path.isfile(p) and f.lower().endswith((".exe", ".dll")):
                try:
                    files[f] = info(p)
                except Exception as e:
                    files[f] = {"error": str(e), "size": os.path.getsize(p)}
        rep[tag] = files
    json.dump(rep, open(out, "w"), indent=1)
    for tag in rep:
        print(f"== {tag} ==")
        for f, i in rep[tag].items():
            if "error" in i:
                print(f"  ERR {f}: {i['error']}")
            else:
                print(f"  {f:32s} {i['size']:>9} {i['machine_name']:5s} "
                      f"{i['imagebase']:>10} imp={i['nimports']:>3} sha1={i['sha1'][:12]}")


if __name__ == "__main__":
    main()