#!/usr/bin/env python3
"""Turn the JSON from tools/recon_swat4.py into the markdown body of
docs/IMPORTS.md. A DLL counts as "of the game" when it is one of the files
analysed by the recon; everything else is an OS API the runtime must provide.

Usage: tools/imports_report.py recon.json > /tmp/imports_body.md
"""
import collections, json, sys

J = json.load(open(sys.argv[1]))
OWN = {f.lower() for tag in J for f in J[tag]}


def os_dll(name):
    return name.lower() not in OWN


out = []
for tag, title in [("base", "SWAT 4 (base)"),
                   ("exp", "SWAT 4: The Stetchkov Syndicate (expansion)")]:
    out.append(f"## {title}\n")
    per_dll = collections.defaultdict(set)
    for f, i in J[tag].items():
        for dll, fns in i.get("imports", {}).items():
            if os_dll(dll):
                per_dll[dll.upper()].update(fns)
    for dll in sorted(per_dll):
        fns = sorted(per_dll[dll])
        out.append(f"### {dll}  ({len(fns)} functions)\n")
        out.append("`" + "`, `".join(fns) + "`\n")
print("\n".join(out))