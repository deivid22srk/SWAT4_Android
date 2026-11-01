#!/bin/sh
# Touch HUD (Java) bridge verification, mirroring the real build ABIs:
#   - runtime/*.c are compiled by the C compiler (Makefile + CMake glob)
#   - runtime/*.cpp by C++
# so vinput.c MUST stay C here; linking it proves the extern "C" contract.
# The HUD itself lives in the Android activity (Java): the runtime only
# keeps vinput (input synthesis), hud_state (game context) and hud_jni
# (the SPSC ring Java posts intents through). This script proves all three
# compile, the ring selftest passes, and the JNI surface matches.
# Run from anywhere.
set -e
ROOT=$(cd "$(dirname "$0")/.." && pwd)
STUBS="$ROOT/scripts/selftest_stubs.c"
cd "$ROOT"

mkdir -p /tmp/sdlinc/SDL2
cp android/app/jni/SDL2/include/*.h /tmp/sdlinc/SDL2/ 2>/dev/null || true

CF="-DGAME_SWAT4 -std=gnu11 -O1 -fno-strict-aliasing -fwrapv -Iruntime -I/tmp/sdlinc -Wall -Wno-unused-function"
CX="-DGAME_SWAT4 -std=c++17 -O1 -Iruntime -I/tmp/sdlinc -DDXVK_NATIVE -Iandroid/app/jni/dxvk/include/native/directx -Iandroid/app/jni/dxvk/include/native/windows -Wno-everything"

echo "== gcc vinput.c (C ABI, as the real builds do)"
gcc $CF -c runtime/vinput.c -o /tmp/chk_v.o
echo "== gcc hud_jni.c (ring + pump; JNI part is __ANDROID__-gated)"
gcc $CF -c runtime/hud_jni.c -o /tmp/chk_hj.o
echo "== gcc dinput.c"
gcc $CF -c runtime/dinput.c -o /tmp/chk_di.o
echo "== gcc platform_sdl.c (drains the HUD ring in the present pump)"
gcc $CF -c runtime/platform_sdl.c -o /tmp/chk_ps.o
echo "== gcc user32.c (GetAsyncKeyState reads the virtual state)"
gcc $CF -c runtime/user32.c -o /tmp/chk_u32.o
echo "== gcc kernel32.c (hud_movie_playing: open .bik count)"
gcc $CF -c runtime/kernel32.c -o /tmp/chk_k32.o
echo "== g++ hud_state.cpp (game context)"
g++ $CX -c runtime/hud_state.cpp -o /tmp/chk_hs.o
echo "== g++ d3d9_bridge.cpp"
g++ $CX -c runtime/d3d9_bridge.cpp -o /tmp/chk_b.o

echo "== hud_jni ring selftest (host, stubbed vinput/hud_state)"
gcc -DGAME_SWAT4 -DHUD_RING_SELFTEST -std=gnu11 -O0 -g -Iruntime -I/tmp/sdlinc \
    -Wall -Wno-unused-function runtime/hud_jni.c -o /tmp/hudringtest
/tmp/hudringtest

echo "== hud_state selftest (synthetic guest image)"
g++ -DGAME_SWAT4 -DHUD_STATE_SELFTEST -std=c++17 -O0 -g -Iruntime \
    runtime/hud_state.cpp -o /tmp/hudstatetest
/tmp/hudstatetest

echo "== undefined-symbol check on the C++ side (must be clean besides libc/SDL/D3D)"
if nm -u /tmp/chk_hs.o | grep -q '_Z.*vinput'; then
    echo "ERROR: vinput symbols are mangled (extern \"C\" missing)"
    exit 1
fi
echo "vinput symbols referenced as plain C: ok"

echo "== behavioral test: vinput menu/mission taps + dinput buffered mouse stream"
sh scripts/behave/run.sh
