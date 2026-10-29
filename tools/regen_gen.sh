#!/bin/sh
# Regenerates gen/ (recompiled C) from your own SWAT 4 files.
# Usage: tools/regen_gen.sh "/path/to/SWAT 4"      (the folder with Content/)
# Needs Python 3.10+: pip install capstone pefile
# The module list and load addresses live in tools/swat4_modules.py.
set -e
G=$(cd "${1:?usage: tools/regen_gen.sh <SWAT 4 root>}" && pwd)
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
SYS="$G/Content/System"
[ -f "$SYS/Swat4.exe" ] || { echo "missing $SYS/Swat4.exe (choose the GOG install root)" >&2; exit 1; }
mkdir -p analysis gen
export LIFT_GAME=swat4
python3 tools/swat4_regen.py "$SYS" gen analysis
python3 tools/emit_modules.py
echo "gen/ is ready ($(find gen -name '*.c' | wc -l) C files)"