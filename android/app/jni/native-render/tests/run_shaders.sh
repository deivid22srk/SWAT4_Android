#!/bin/bash
# Harness desktop do native-render (camada shader, sem GPU):
#  1. compila o gerador de casos (nvr_test_shaders.cpp)
#  2. roda: emite SPIR-V para cada caso
#  3. valida cada .spv com spirv-val (se disponivel)
# Uso: run_shaders.sh [dir-saidas]
set -u
# Raiz do repo derivada deste script (funciona no checkout do CI e no host)
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
NVR="$(cd "$SCRIPT_DIR/.." && pwd)"
ROOT="$(cd "$NVR/../.." && pwd)"
OUT=${1:-/tmp/nvr-spv}
SPIRV_VAL="${SPIRV_VAL:-/tmp/spirv-tools/build/tools/spirv-val}"
BINDIR=/tmp/nvr-test
mkdir -p "$BINDIR" "$OUT"

echo "== build do gerador =="
g++ -std=c++17 -g0 -O1 -Wall -Wextra -Wno-unused-parameter \
  -I"$NVR/src" -I"$NVR/src/shader" -I"$NVR/thirdparty/spirv_inc" \
  -I"$ROOT/dxvk/include/native/directx" -I"$ROOT/dxvk/include/native/windows" \
  -I"$ROOT/dxvk/include" \
  "$NVR/tests/nvr_test_shaders.cpp" \
  "$NVR/src/shader/nvr_dx9bc.cpp" "$NVR/src/shader/nvr_compile.cpp" \
  "$NVR/src/shader/nvr_spirv.cpp" "$NVR/src/shader/nvr_fff.cpp" \
  "$NVR/src/nvr_common.cpp" "$NVR/src/nvr_format.cpp" \
  -o "$BINDIR/nvr_test_shaders" -l:libvulkan.so.1 -L/usr/lib/x86_64-linux-gnu 2>&1 | head -30 || exit 1

echo "== geracao =="
rm -f "$OUT"/*.spv
"$BINDIR/nvr_test_shaders" "$OUT"
GEN=$?

echo
echo "== validacao spirv-val =="
if [ ! -x "$SPIRV_VAL" ]; then
  echo "spirv-val ausente em $SPIRV_VAL — pulei a validacao estrutural"
  exit $GEN
fi
FAIL=0
N=0
for f in "$OUT"/*.spv; do
  N=$((N+1))
  if ! "$SPIRV_VAL" --target-env vulkan1.1 "$f" > /tmp/spv-val.log 2>&1; then
    FAIL=$((FAIL+1))
    echo "INVALID: $(basename "$f")"
    head -6 /tmp/spv-val.log
  fi
done
echo
echo "spirv-val: $((N-FAIL))/$N validos"
if [ "$FAIL" -gt 0 ] || [ "$GEN" -gt 0 ]; then exit $((FAIL+GEN)); fi
echo "TODOS OS CASOS OK"
