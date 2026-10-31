#!/bin/sh
# Touch overlay (HUD) verification, mirroring the real build ABIs:
#   - runtime/*.c are compiled by the C compiler (Makefile + CMake glob)
#   - runtime/*.cpp by C++
# so vinput.c MUST stay C here; linking it proves the extern "C" contract.
# Then builds and runs the pure-logic selftest (TOUCH_SELFTEST: no SDL/D3D).
# Run from anywhere.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
STUBS="$ROOT/scripts/selftest_stubs.c"
cd "$ROOT"

mkdir -p /tmp/sdlinc/SDL2
cp -n android/app/jni/SDL2/include/*.h /tmp/sdlinc/SDL2/ 2>/dev/null || true

CF="-DGAME_SWAT4 -std=gnu11 -O1 -fno-strict-aliasing -fwrapv -Iruntime -I/tmp/sdlinc -Wall -Wno-unused-function"
CX="-DGAME_SWAT4 -std=c++17 -O1 -Iruntime -I/tmp/sdlinc -DDXVK_NATIVE -Iandroid/app/jni/dxvk/include/native/directx -Iandroid/app/jni/dxvk/include/native/windows -Wno-everything"

echo "== gcc vinput.c (C ABI, as the real builds do)"
gcc $CF -c runtime/vinput.c -o /tmp/chk_v.o
echo "== gcc dinput.c"
gcc $CF -c runtime/dinput.c -o /tmp/chk_di.o
echo "== gcc platform_sdl.c"
gcc $CF -c runtime/platform_sdl.c -o /tmp/chk_ps.o
echo "== g++ touch_swat4.cpp (DXVK d3d9 path, as the real builds compile it)"
g++ $CX -c runtime/touch_swat4.cpp -o /tmp/chk_t_d3d.o
echo "== g++ d3d9_bridge.cpp"
g++ $CX -c runtime/d3d9_bridge.cpp -o /tmp/chk_b.o

echo "== selftest build (vinput.o from gcc above = the real C ABI)"
g++ -DGAME_SWAT4 -DTOUCH_SELFTEST -std=c++17 -O0 -g -Iruntime -I/tmp/sdlinc \
    runtime/touch_swat4.cpp /tmp/chk_v.o $(gcc $CF -c "$STUBS" -o /tmp/chk_stub.o && echo /tmp/chk_stub.o) \
    -o /tmp/touchtest
echo "== selftest run (script sha256 $(sha256sum "$0" | cut -c1-16))"
/tmp/touchtest

echo "== undefined-symbol check on the C++ side (must be clean besides libc/SDL/D3D)"
if nm -u /tmp/chk_t_d3d.o | grep -q '_Z.*vinput'; then
    echo "ERROR: vinput symbols are mangled (extern \"C\" missing)"
    exit 1
fi
echo "vinput symbols referenced as plain C: ok"
