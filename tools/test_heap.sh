#!/bin/sh
# Compila e roda o teste do heap endurecido (tools/heap_test.c) contra o
# runtime/heap.c REAL, com um stub minimo de rt.h.
#   ./tools/test_heap.sh
set -e
cd "$(dirname "$0")/.."
INC=/tmp/heap_impl.inc
sed 's|#include "rt.h"||' runtime/heap.c > "$INC"
gcc -O1 -std=gnu11 -Wall -I /tmp -o /tmp/heap_test tools/heap_test.c -lpthread
/tmp/heap_test
