/* Standalone test of the hardened guest heap (runtime/heap.c).
 *
 * Build (tools/test_heap.sh):
 *   sed 's|#include "rt.h"||' runtime/heap.c > <dir>/heap_impl.inc
 *   gcc -O1 -std=gnu11 -I <dir> -o /tmp/heap_test tools/heap_test.c -lpthread
 *
 * Exercita:
 *   1. smoke: alloc/read/write/free, alinhamento, gsize;
 *   2. reuso de bin (exact fit) e split;
 *   3. corrupt free-list link -> rebuild + relink, sem crash;
 *   4. gfree selvagem / double free / gsize lixo -> descartados;
 *   5. header corrompido detectado no gfree;
 *   6. chain quebrada -> quarentena, gmalloc segue vivo;
 *   7. martelada multi-thread (mutex, zero corrupcoes).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <pthread.h>
#include <sys/mman.h>

/* ---- stub do rt.h: exatamente o que heap.c usa ---- */
static uint8_t *MEM;
#define M8(a)  (*(uint8_t *)(MEM + (uint32_t)(a)))
#define M16(a) (*(uint16_t *)(MEM + (uint32_t)(a)))
#define M32(a) (*(uint32_t *)(MEM + (uint32_t)(a)))
#define GUEST_HEAP_BASE 0x20000000u
#define GUEST_HEAP_END  0x40000000u
#define GUEST_VA_BASE   0x40000000u
#define GUEST_VA_END    0x60000000u
static int g_log_level = 1;
#define LOG(...) do { printf("[heap-log] " __VA_ARGS__); printf("\n"); fflush(stdout); } while (0)

/* corpo real do runtime/heap.c (com o include de rt.h removido) */
#include "heap_impl.inc"

static int failures;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: " __VA_ARGS__); printf("\n"); failures++; } \
                              else { printf("ok: " __VA_ARGS__); printf("\n"); } } while (0)

static void mem_init(void) {
    void *m = mmap(NULL, 1ull << 32, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (m == MAP_FAILED) { perror("mmap"); exit(2); }
    mprotect(m, 0x10000, PROT_NONE); /* null guard, igual ao runtime */
    MEM = (uint8_t *)m;
}

static void test_smoke(void) {
    uint32_t p = gmalloc(100);
    CHECK(p && (p & 15) == 0, "smoke alloc %08x 16-aligned", p);
    CHECK(gsize(p) >= 100, "gsize(%08x)=%u >= 100", p, gsize(p));
    memset(MEM + p, 0x5A, 100);
    CHECK(M8(p + 42) == 0x5A, "write/read pattern");
    gfree(p);
    uint32_t q = gmalloc(1);
    CHECK(q == p, "freed block reused (%08x == %08x)", q, p);
    gfree(q);
    gfree(0); /* no-op */
}

static void test_bins(void) {
    uint32_t a = gmalloc(0x100), b = gmalloc(0x100), c = gmalloc(0x100);
    CHECK(a && b && c, "bump allocs a=%08x b=%08x c=%08x", a, b, c);
    gfree(b);
    uint32_t d = gmalloc(0x100);
    CHECK(d == b, "exact-fit reuse from bin (d=%08x b=%08x)", d, b);
    uint32_t e = gmalloc(0x40);
    CHECK(e && (e & 15) == 0, "alloc after split e=%08x", e);
    gfree(a); gfree(c); gfree(d); gfree(e);
}

static void test_link_corruption(void) {
    uint32_t a = gmalloc(0x100), b = gmalloc(0x100), c = gmalloc(0x100);
    gfree(b); /* b entra numa bin com links no payload */
    M32(b + 8) = 0x11; /* NEXTF invalido (nao ≡ 8 mod 16) */
    uint32_t p = 0;
    for (int i = 0; i < 4 && !p; i++) p = gmalloc(0x100);
    CHECK(p != 0, "allocation survives free-list corruption (p=%08x)", p);
    gfree(a); gfree(c); gfree(p);
}

static void test_bad_frees(void) {
    gfree(0x12345678u);          /* fora do heap */
    gfree(0x20000004u);          /* dentro do heap, nao e bloco */
    gfree(GUEST_HEAP_BASE + 9);  /* desalinhado */
    uint32_t p = gmalloc(0x80);
    gfree(p);
    gfree(p); /* double free: detectado e descartado */
    CHECK(gsize(0xDEADBEEFu) == 0, "gsize of garbage returns 0");
    uint32_t q = gmalloc(0x80);
    CHECK(q != 0, "heap still consistent after bad frees (q=%08x)", q);
    gfree(q);
}

static void test_header_corruption_on_free(void) {
    uint32_t a = gmalloc(0x100), b = gmalloc(0x100);
    M32(b + 4) = 0xFFFFFFF1u; /* tamanho absurdo no header de b */
    gfree(b);                 /* deve ser descartado com log, sem crash */
    uint32_t c = gmalloc(0x100);
    CHECK(c != 0, "alloc after corrupted-header free c=%08x", c);
    gfree(a); gfree(c);
}

static void test_chain_quarantine(void) {
    uint32_t a = gmalloc(0x200), b = gmalloc(0x200), c = gmalloc(0x200), d = gmalloc(0x200);
    gfree(b); gfree(d);
    /* estraga o header de c (bloco USADO): simula overrun grande de vizinho */
    M32(c + 4) = 0x00000017u;
    gfree(a);
    M32(a + 8) = 0x21; /* NEXTF insano -> dispara rebuild */
    uint32_t p = gmalloc(0x200);
    CHECK(p != 0, "gmalloc alive after chain break (p=%08x)", p);
    /* blocos livres ANTES do ponto quebrado (a, b) podem ser reusados; o que
     * nao pode e alocar DENTRO da regiao em quarentena */
    uint32_t q = gmalloc(0x200);
    CHECK(q != 0, "alloc still works after quarantine (q=%08x)", q);
    gfree(p); gfree(q);
}

enum { MT_N = 20000, MT_SLOTS = 16 };
static uint32_t mt_seed[2];

static void *mt_worker(void *arg) {
    int id = (int)(intptr_t)arg;
    uint32_t s = mt_seed[id];
    uint32_t slots[MT_SLOTS] = {0};
    for (int i = 0; i < MT_N; i++) {
        s = s * 1103515245u + 12345u;
        int k = (s >> 16) % MT_SLOTS;
        if (slots[k]) { gfree(slots[k]); slots[k] = 0; }
        else slots[k] = gmalloc(16 + ((s >> 8) % 512));
    }
    for (int k = 0; k < MT_SLOTS; k++) if (slots[k]) gfree(slots[k]);
    return NULL;
}

static void test_stress_mt(void) {
    pthread_t th[2];
    mt_seed[0] = 12345; mt_seed[1] = 67890;
    uint32_t baseline = g_corrupt_events; /* eventos intencionais dos testes 3-6 */
    for (long i = 0; i < 2; i++) pthread_create(&th[i], NULL, mt_worker, (void *)i);
    for (int i = 0; i < 2; i++) pthread_join(th[i], NULL);
    CHECK(g_corrupt_events == baseline, "stress sem corrupcao (events=%u -> %u)",
          baseline, g_corrupt_events);
}

int main(void) {
    mem_init();
    printf("== hardened guest heap test ==\n");
    test_smoke();
    test_bins();
    test_link_corruption();
    test_bad_frees();
    test_header_corruption_on_free();
    test_chain_quarantine();
    test_stress_mt();
    printf("== %s (%d failures, %u corruption events logged) ==\n",
           failures ? "FAILED" : "ALL PASS", failures, g_corrupt_events);
    return failures ? 1 : 0;
}
