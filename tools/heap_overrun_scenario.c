/* shhxpott scenario: the 19952-byte Havok parse buffer writes ~90 bytes past
 * its requested end. With the 256-byte big-block rounding that must land in
 * slack: the neighbor survives, gfree reports the contained overrun, no
 * corruption event fires, and the heap keeps serving allocations.
 *
 * Build (same harness as tools/test_heap.sh):
 *   sed 's|#include "rt.h"||' runtime/heap.c > /tmp/heap_impl.inc
 *   gcc -O1 -std=gnu11 -I /tmp -o /tmp/heap_scn tools/heap_overrun_scenario.c -lpthread
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>

/* ---- stub do rt.h: exatamente o que heap.c usa (idem tools/heap_test.c) ---- */
static uint8_t *MEM;
#define M8(a)  (*(uint8_t *)(MEM + (uint32_t)(a)))
#define M16(a) (*(uint16_t *)(MEM + (uint32_t)(a)))
#define M32(a) (*(uint32_t *)(MEM + (uint32_t)(a)))
#define GUEST_HEAP_BASE 0x20000000u
#define GUEST_HEAP_END  0x40000000u
#define GUEST_VA_BASE   0x40000000u
#define GUEST_VA_END    0x60000000u
static int g_log_level = 1;
#define LOG(...) do { printf("eap-log] " __VA_ARGS__); printf("\n"); fflush(stdout); } while (0)

#include "heap_impl.inc"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failures++; } \
                              else { printf("ok: " __VA_ARGS__); printf("\n"); } } while (0)

int main(void) {
    MEM = malloc(1u << 30); /* host backing for the whole guest map */
    if (!MEM) return 2;

    uint32_t a = gmalloc(19952);          /* the Havok parse buffer */
    uint32_t b = gmalloc(8328);           /* neighbor that used to die */
    CHECK(a && b, "allocs a=%08x b=%08x (gsize a=%u)", a, b, a ? gsize(a) : 0);
    if (!a || !b) return 1;
    memset(MEM + b, 0x11, 8328);

    /* the overruns: junk past a's requested end - 90 bytes (the observed
     * shhxpott extent) and 150 bytes (headroom check), both inside the
     * guaranteed 192-byte big-block slack */
    memset(MEM + a + 19952, 0x42, 90);
    gfree(a);                             /* must report OVERRUN, contain it */

    int smashed = 0;
    unsigned char *bb = MEM + b;
    for (int i = 0; i < 8328; i++) if (bb[i] != 0x11) { smashed = 1; break; }
    CHECK(!smashed, "90-byte overrun absorbed, neighbor intact");

    uint32_t c = gmalloc(19952);
    CHECK(c != 0, "realloc after contained overrun (c=%08x)", c);
    if (!c) return 1;
    memset(MEM + c + 19952, 0x77, 150);
    gfree(c);
    smashed = 0;
    for (int i = 0; i < 8328; i++) if (bb[i] != 0x11) { smashed = 1; break; }
    CHECK(!smashed, "150-byte overrun absorbed, neighbor intact");

    for (int i = 0; i < 100; i++) {
        uint32_t p = gmalloc(19952);
        if (!p) { CHECK(0, "churn alloc %d", i); return 1; }
        memset(MEM + p, 0x33, 19952);
        gfree(p);
    }
    gfree(b);
    CHECK(1, "churn 100x19952 clean");

    printf(failures ? "== scenario FAIL (%d) ==\n" : "== shhxpott scenario PASS ==\n",
           failures);
    return failures ? 1 : 0;
}
