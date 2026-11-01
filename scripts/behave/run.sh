#!/bin/sh
# Behavioral test for the touch-HUD input path (vinput + dinput), host side.
# Compiles the real runtime sources with thin stubs and asserts the input
# contracts the field depends on — see scripts/behave/vinput_behav.c.
# Run from anywhere.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"

mkdir -p /tmp/sdlinc/SDL2
cp android/app/jni/SDL2/include/*.h /tmp/sdlinc/SDL2/ 2>/dev/null || true

gcc -DGAME_SWAT4 -std=gnu11 -O0 -g -Iruntime -I/tmp/sdlinc -Wall -Wno-unused-function \
    scripts/behave/vinput_behav.c -o /tmp/vinput_behav
/tmp/vinput_behav
