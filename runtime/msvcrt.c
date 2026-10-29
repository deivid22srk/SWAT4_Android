/* msvcrt.dll shims for recompiled DLLs (d3dx9_35). All cdecl unless noted.
 * The _CI* math helpers use the x87 register calling convention. */
#define _GNU_SOURCE
#include <unistd.h>
#include <sys/stat.h>
#include "rt.h"
#include <ctype.h>
#include <setjmp.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <wctype.h>

static uint32_t g_errno_addr, g_iob, g_pioinfo, g_badioinfo, g_locale_c;

/* ---------------- guest printf/scanf ---------------- */
/* Formats with arguments read from guest stack memory at *argp. */
size_t guest_vformat(char *out, size_t cap, const char *fmt, uint32_t argp) {
    size_t n = 0;
#define PUT(ch) do { if (n + 1 < cap) out[n] = (ch); n++; } while (0)
    for (const char *p = fmt; *p; p++) {
        if (*p != '%') { PUT(*p); continue; }
        char spec[32];
        int k = 0;
        spec[k++] = '%';
        p++;
        while (*p && strchr("-+ #0", *p) && k < 20) spec[k++] = *p++;
        if (*p == '*') { k += snprintf(spec + k, sizeof spec - k, "%d", (int32_t)M32(argp)); argp += 4; p++; }
        else while (isdigit((unsigned char)*p) && k < 24) spec[k++] = *p++;
        if (*p == '.') {
            spec[k++] = *p++;
            if (*p == '*') { k += snprintf(spec + k, sizeof spec - k, "%d", (int32_t)M32(argp)); argp += 4; p++; }
            else while (isdigit((unsigned char)*p) && k < 28) spec[k++] = *p++;
        }
        int is64 = 0, isl = 0, ish = 0;
        for (;;) {
            if (*p == 'l') { isl = 1; p++; }
            else if (*p == 'h') { ish = 1; p++; }
            else if (p[0] == 'I' && p[1] == '6' && p[2] == '4') { is64 = 1; p += 3; }
            else if (*p == 'I') { p++; }
            else if (*p == 'L') { p++; }
            else break;
        }
        char conv = *p;
        if (!conv) break;
        char tmp[512];
        int len = 0;
        switch (conv) {
        case 'd': case 'i': case 'u': case 'x': case 'X': case 'o':
            if (is64) {
                uint64_t v = M64(argp); argp += 8;
                spec[k++] = 'l'; spec[k++] = 'l'; spec[k++] = conv; spec[k] = 0;
                len = snprintf(tmp, sizeof tmp, spec, v);
            } else {
                uint32_t v = M32(argp); argp += 4;
                if (ish) v = (conv == 'd' || conv == 'i') ? (uint32_t)(int32_t)(int16_t)v : (uint16_t)v;
                spec[k++] = conv; spec[k] = 0;
                len = snprintf(tmp, sizeof tmp, spec, v);
            }
            break;
        case 'c':
            spec[k++] = 'c'; spec[k] = 0;
            len = snprintf(tmp, sizeof tmp, spec, (int)(uint8_t)M32(argp)); argp += 4;
            break;
        case 's': case 'S': {
            uint32_t s = M32(argp); argp += 4;
            spec[k++] = 's'; spec[k] = 0;
            if ((conv == 'S') != (isl != 0) && s) {
                /* wide string */
                char nar[512];
                int i = 0;
                for (; i < 511 && M16(s + 2 * i); i++) nar[i] = (char)M16(s + 2 * i);
                nar[i] = 0;
                len = snprintf(tmp, sizeof tmp, spec, nar);
            } else {
                len = snprintf(tmp, sizeof tmp, spec, s ? (char *)(MEM + s) : "(null)");
            }
            break;
        }
        case 'e': case 'E': case 'f': case 'g': case 'G': case 'a': case 'A': {
            double v = MF64(argp); argp += 8;
            spec[k++] = conv; spec[k] = 0;
            len = snprintf(tmp, sizeof tmp, spec, v);
            break;
        }
        case 'p':
            len = snprintf(tmp, sizeof tmp, "%08X", M32(argp)); argp += 4;
            break;
        case 'n':
            M32(M32(argp)) = (uint32_t)n; argp += 4;
            break;
        case '%':
            tmp[0] = '%'; len = 1;
            break;
        default:
            tmp[0] = conv; len = 1;
            break;
        }
        if (len > (int)sizeof tmp - 1) len = sizeof tmp - 1;
        for (int i = 0; i < len; i++) PUT(tmp[i]);
    }
    if (cap) out[n < cap ? n : cap - 1] = 0;
#undef PUT
    return n;
}

static int guest_vscan(const char *str, const char *fmt, uint32_t argp) {
    const char *s = str;
    int assigned = 0;
    for (const char *p = fmt; *p; p++) {
        if (isspace((unsigned char)*p)) { while (isspace((unsigned char)*s)) s++; continue; }
        if (*p != '%') { if (*s != *p) break; s++; continue; }
        p++;
        int suppress = 0;
        if (*p == '*') { suppress = 1; p++; }
        int width = 0;
        while (isdigit((unsigned char)*p)) width = width * 10 + (*p++ - '0');
        int isl = 0;
        while (*p == 'l' || *p == 'h' || *p == 'L') { if (*p == 'l') isl++; p++; }
        char conv = *p;
        if (conv != 'c' && conv != '[' && conv != 'n') while (isspace((unsigned char)*s)) s++;
        if (!*s && conv != 'n') break;
        char *end;
        switch (conv) {
        case 'd': case 'i': case 'u': case 'x': case 'X': {
            long v = strtol(s, &end, conv == 'x' || conv == 'X' ? 16 : conv == 'i' ? 0 : 10);
            if (end == s) return assigned;
            s = end;
            if (!suppress) { M32(M32(argp)) = (uint32_t)v; argp += 4; assigned++; }
            break;
        }
        case 'f': case 'g': case 'e': {
            double v = strtod(s, &end);
            if (end == s) return assigned;
            s = end;
            if (!suppress) {
                if (isl) MF64(M32(argp)) = v; else MF32(M32(argp)) = (float)v;
                argp += 4; assigned++;
            }
            break;
        }
        case 's': {
            uint32_t d = suppress ? 0 : M32(argp);
            int i = 0;
            while (*s && !isspace((unsigned char)*s) && (!width || i < width)) { if (d) M8(d + i) = (uint8_t)*s; i++; s++; }
            if (d) { M8(d + i) = 0; argp += 4; assigned++; }
            break;
        }
        case 'c': {
            int w = width ? width : 1;
            uint32_t d = suppress ? 0 : M32(argp);
            for (int i = 0; i < w && *s; i++, s++) if (d) M8(d + i) = (uint8_t)*s;
            if (d) { argp += 4; assigned++; }
            break;
        }
        case 'n':
            if (!suppress) { M32(M32(argp)) = (uint32_t)(s - str); argp += 4; }
            break;
        default:
            return assigned;
        }
    }
    return assigned;
}

#define CARG(n) ARG(n)
SHIM(_snprintf) {
    char *buf = malloc(ARG(1) + 1);
    size_t n = guest_vformat(buf, ARG(1) + 1, GSTR(ARG(2)), c->r[4] + 16);
    uint32_t cap = ARG(1);
    memcpy(MEM + ARG(0), buf, n < cap ? n + 1 : cap);
    free(buf);
    RETC(n < cap ? (int32_t)n : -1);
}
SHIM(_vsnprintf) {
    char *buf = malloc(ARG(1) + 1);
    size_t n = guest_vformat(buf, ARG(1) + 1, GSTR(ARG(2)), ARG(3));
    uint32_t cap = ARG(1);
    memcpy(MEM + ARG(0), buf, n < cap ? n + 1 : cap);
    free(buf);
    RETC(n < cap ? (int32_t)n : -1);
}
SHIM(sscanf) { RETC(guest_vscan(GSTR(ARG(0)), GSTR(ARG(1)), c->r[4] + 12)); }

/* ---------------- I/O (minimal: stderr only) ---------------- */
SHIM(_isatty) { RETC(0); }
SHIM(_write) {
    if (ARG(0) == 1 || ARG(0) == 2) fwrite(MEM + ARG(1), 1, ARG(2), stderr);
    RETC(ARG(2));
}
SHIM(_lseeki64) { c->r[2] = 0xFFFFFFFFu; RETC(0xFFFFFFFFu); }
SHIM(_fileno) { RETC(2); }
/* A guest FILE* is a 16-byte cell holding the host FILE*. MSVCR71's stdio is
 * not recompiled, so every stdio call arrives here. */
#define CRTFILE_MAGIC 0x4346544Cu
typedef struct { uint64_t h; uint32_t magic; } CFile;
static FILE *cf(uint32_t p) {
    if (!p) return NULL;
    CFile *f = (CFile *)(MEM + p);
    return f->magic == CRTFILE_MAGIC ? (FILE *)(uintptr_t)f->h : NULL;
}
static uint32_t mkcf(FILE *h) {
    if (!h) return 0;
    uint32_t p = gmalloc(sizeof(CFile));
    CFile *f = (CFile *)(MEM + p);
    f->h = (uint64_t)(uintptr_t)h;
    f->magic = CRTFILE_MAGIC;
    return p;
}
static void w2a_local(uint32_t w, char *out, size_t n) {
    size_t i = 0;
    if (!n) return;
    if (!w) { out[0] = 0; return; }
    for (; i + 1 < n && M16(w + 2 * i); i++) out[i] = (char)M16(w + 2 * i);
    out[i] = 0;
}
SHIM(fopen) {
    char host[1024];
    rt_host_path(GSTR(ARG(0)), host, sizeof host);
    RETC(mkcf(fopen(host, GSTR(ARG(1)) ? GSTR(ARG(1)) : "rb")));
}
SHIM(_wfopen) {
    char path[1024], mode[16], host[1024];
    w2a_local(ARG(0), path, sizeof path);
    w2a_local(ARG(1), mode, sizeof mode);
    rt_host_path(path, host, sizeof host);
    RETC(mkcf(fopen(host, mode[0] ? mode : "rb")));
}
SHIM(fclose) {
    FILE *h = cf(ARG(0));
    int r = h ? fclose(h) : -1;
    if (h) gfree(ARG(0));
    RETC(r);
}
SHIM(fread) {
    FILE *h = cf(ARG(3));
    RETC(h ? fread(MEM + ARG(0), ARG(1), ARG(2), h) : 0);
}
SHIM(fwrite) {
    FILE *h = cf(ARG(3));
    if (h) { RETC(fwrite(MEM + ARG(0), ARG(1), ARG(2), h)); }
    fwrite(MEM + ARG(0), ARG(1), ARG(2), stderr);
    RETC(ARG(2));
}
SHIM(fseek) { FILE *h = cf(ARG(0)); RETC(h ? fseek(h, (long)(int32_t)ARG(1), ARG(2)) : -1); }
SHIM(ftell) { FILE *h = cf(ARG(0)); RETC(h ? (uint32_t)ftell(h) : 0xFFFFFFFFu); }
SHIM(fgetc) { FILE *h = cf(ARG(0)); RETC(h ? (uint32_t)fgetc(h) : 0xFFFFFFFFu); }
SHIM(rewind) { FILE *h = cf(ARG(0)); if (h) rewind(h); RETC(0); }
SHIM(fflush) { FILE *h = cf(ARG(0)); RETC(fflush(h)); }
SHIM(remove) {
    char host[1024];
    rt_host_path(GSTR(ARG(0)), host, sizeof host);
    RETC(unlink(host));
}
static void fill_stat(uint32_t out, struct stat *st) {
    memset(MEM + out, 0, 32);
    M16(out + 8) = (uint16_t)(S_ISDIR(st->st_mode) ? 0040755 : 0100644);
    M32(out + 20) = (uint32_t)st->st_size;
    M32(out + 24) = (uint32_t)st->st_atime;
    M32(out + 28) = (uint32_t)st->st_mtime;
}
SHIM(_stat) {
    char host[1024];
    struct stat st;
    rt_host_path(GSTR(ARG(0)), host, sizeof host);
    if (stat(host, &st)) RETC(-1);
    fill_stat(ARG(1), &st);
    RETC(0);
}
SHIM(_wstat) {
    char path[1024], host[1024];
    struct stat st;
    w2a_local(ARG(0), path, sizeof path);
    rt_host_path(path, host, sizeof host);
    if (stat(host, &st)) RETC(-1);
    fill_stat(ARG(1), &st);
    RETC(0);
}
SHIM(_fstat) {
    struct stat st;
    FILE *h = cf(ARG(0));
    if (!h || fstat(fileno(h), &st)) RETC(-1);
    fill_stat(ARG(1), &st);
    RETC(0);
}
SHIM(fprintf) {
    char buf[4096];
    size_t n = guest_vformat(buf, sizeof buf - 1, GSTR(ARG(1)), c->r[4] + 12);
    FILE *h = cf(ARG(0));
    fwrite(buf, 1, n, h ? h : stderr);
    RETC(n);
}
SHIM(printf) {
    char buf[4096];
    size_t n = guest_vformat(buf, sizeof buf - 1, GSTR(ARG(0)), c->r[4] + 8);
    fwrite(buf, 1, n, stdout);
    fflush(stdout);
    RETC(n);
}
SHIM(fscanf) { LOG("fscanf(%s) not implemented", GSTR(ARG(1)) ? GSTR(ARG(1)) : "?"); RETC(0); }
SHIM(_tempnam) { RETC(0); }

/* ---------------- process / CRT plumbing ---------------- */
SHIM(_onexit) { RETC(ARG(0)); }
SHIM(__dllonexit) { RETC(ARG(0)); }
SHIM(_lock) { RETC(0); }
SHIM(_unlock) { RETC(0); }
SHIM(_amsg_exit) { rt_fatal("msvcrt _amsg_exit(%u)", ARG(0)); }
SHIM(_initterm) {
    uint32_t b = ARG(0), e = ARG(1);
    for (uint32_t p = b; p < e; p += 4)
        if (M32(p)) rt_invoke(c, M32(p), 0, NULL);
    RETC(0);
}
SHIM(_XcptFilter) { RETC(0); }
SHIM(terminate) { rt_fatal("msvcrt terminate()"); }
SHIM(abort_) { rt_fatal("msvcrt abort()"); }
SHIM(exit_) { LOG("msvcrt exit(%d)", (int)ARG(0)); fflush(NULL); _exit((int)ARG(0)); }
SHIM(_purecall) { rt_fatal("pure virtual call"); }
SHIM(_CxxThrowException) {
    rt_fatal("C++ exception thrown in recompiled DLL (object %08x, info %08x)", ARG(0), ARG(1));
}
SHIM(__CxxFrameHandler) { rt_fatal("__CxxFrameHandler invoked"); }
SHIM(type_info_dtor) { RET(0, 0); } /* thiscall, no stack args */
SHIM(_errno) { RETC(g_errno_addr); }
SHIM(setlocale) { RETC(g_locale_c); }
SHIM(_strtime) { snprintf((char *)MEM + ARG(0), 9, "12:00:00"); RETC(ARG(0)); }
SHIM(_strdate) { snprintf((char *)MEM + ARG(0), 9, "01/01/08"); RETC(ARG(0)); }
SHIM(_clearfp) { RETC(0); }
SHIM(_controlfp) { RETC(0x0009001Fu); }

/* ---------------- memory ---------------- */
SHIM(malloc_) {
    uint32_t n = ARG(0) ? ARG(0) : 1, p = gmalloc(n);
    if (n > 0x10000) LOG("malloc(%u) = %08x", n, p);
    RETC(p);
}
SHIM(calloc_) {
    uint32_t n = ARG(0) * ARG(1);
    uint32_t p = gmalloc(n ? n : 1);
    if (p) memset(MEM + p, 0, n);
    RETC(p);
}
SHIM(realloc_) { RETC(grealloc(ARG(0), ARG(1))); }
SHIM(free_) { gfree(ARG(0)); RETC(0); }
SHIM(_strdup) { RETC(ARG(0) ? gstrdup(GSTR(ARG(0))) : 0); }
SHIM(memmove_) { memmove(MEM + ARG(0), MEM + ARG(1), ARG(2)); RETC(ARG(0)); }
SHIM(memcpy_) { memmove(MEM + ARG(0), MEM + ARG(1), ARG(2)); RETC(ARG(0)); }
SHIM(memset_) { memset(MEM + ARG(0), (int)ARG(1), ARG(2)); RETC(ARG(0)); }

/* ---------------- strings / ctype ---------------- */
SHIM(strchr_) {
    char *r = strchr(GSTR(ARG(0)), (int)ARG(1));
    RETC(r ? (uint32_t)((uint8_t *)r - MEM) : 0);
}
SHIM(strncmp_) { RETC(strncmp(GSTR(ARG(0)), GSTR(ARG(1)), ARG(2))); }
SHIM(_stricmp) { RETC(strcasecmp(GSTR(ARG(0)), GSTR(ARG(1)))); }
SHIM(_strnicmp) { RETC(strncasecmp(GSTR(ARG(0)), GSTR(ARG(1)), ARG(2))); }
SHIM(_itoa) {
    int32_t v = (int32_t)ARG(0);
    uint32_t radix = ARG(2);
    char tmp[40];
    if (radix == 10) snprintf(tmp, sizeof tmp, "%d", v);
    else if (radix == 16) snprintf(tmp, sizeof tmp, "%x", (uint32_t)v);
    else snprintf(tmp, sizeof tmp, "%o", (uint32_t)v);
    strcpy((char *)MEM + ARG(1), tmp);
    RETC(ARG(1));
}
SHIM(_ultoa) {
    char tmp[40];
    uint32_t radix = ARG(2);
    if (radix == 16) snprintf(tmp, sizeof tmp, "%x", ARG(0));
    else snprintf(tmp, sizeof tmp, "%u", ARG(0));
    strcpy((char *)MEM + ARG(1), tmp);
    RETC(ARG(1));
}
SHIM(atoi_) { RETC((uint32_t)atoi(GSTR(ARG(0)))); }
SHIM(atol_) { RETC((uint32_t)atol(GSTR(ARG(0)))); }
SHIM(atof_) { fpush(c, atof(GSTR(ARG(0)))); RETC(0); }
SHIM(strtod_) {
    char *end;
    const char *s = GSTR(ARG(0));
    double v = strtod(s, &end);
    if (ARG(1)) M32(ARG(1)) = ARG(0) + (uint32_t)(end - s);
    fpush(c, v);
    RETC(0);
}
SHIM(wctomb_) { if (ARG(0)) M8(ARG(0)) = (uint8_t)ARG(1); RETC(1); }
SHIM(wcstombs_) {
    uint32_t d = ARG(0), s = ARG(1), n = ARG(2);
    uint32_t i = 0;
    for (; M16(s + 2 * i) && (!d || i < n); i++) if (d) M8(d + i) = (uint8_t)M16(s + 2 * i);
    if (d && i < n) M8(d + i) = 0;
    RETC(i);
}
SHIM(isleadbyte_) { RETC(0); }
#define CTYPE(name, fn) SHIM(name) { RETC(fn((int)ARG(0)) ? 1 : 0); }
CTYPE(isxdigit_, isxdigit) CTYPE(isalnum_, isalnum) CTYPE(isalpha_, isalpha) CTYPE(isspace_, isspace)
CTYPE(isdigit_, isdigit) CTYPE(iswspace_, iswspace) CTYPE(iswalpha_, iswalpha) CTYPE(iswdigit_, iswdigit)
CTYPE(iswpunct_, iswpunct)
SHIM(toupper_) { RETC((uint32_t)toupper((int)ARG(0))); }
SHIM(tolower_) { RETC((uint32_t)tolower((int)ARG(0))); }
static uint32_t g_rand = 1;
SHIM(srand_) { g_rand = ARG(0); RETC(0); }
SHIM(rand_) { g_rand = g_rand * 214013u + 2531011u; RETC((g_rand >> 16) & 0x7FFF); }

/* qsort with a guest comparator (simple insertion/merge on guest memory) */
static CPU *g_qs_cpu;
static uint32_t g_qs_cmp, g_qs_size;
static void qs_sort(uint32_t base, uint32_t n) {
    if (n < 2) return;
    uint32_t tmp = gmalloc(g_qs_size);
    for (uint32_t i = 1; i < n; i++) {
        memcpy(MEM + tmp, MEM + base + i * g_qs_size, g_qs_size);
        uint32_t j = i;
        while (j > 0) {
            uint32_t args[2] = {base + (j - 1) * g_qs_size, tmp};
            if ((int32_t)rt_invoke(g_qs_cpu, g_qs_cmp, 2, args) <= 0) break;
            memcpy(MEM + base + j * g_qs_size, MEM + base + (j - 1) * g_qs_size, g_qs_size);
            j--;
        }
        memcpy(MEM + base + j * g_qs_size, MEM + tmp, g_qs_size);
    }
    gfree(tmp);
}
SHIM(qsort_) {
    uint32_t base = ARG(0), n = ARG(1);
    g_qs_cpu = c;
    g_qs_size = ARG(2);
    g_qs_cmp = ARG(3);
    qs_sort(base, n);
    RETC(0);
}

/* ---------------- math ---------------- */
/* _CI*: argument(s) on the x87 stack, result replaces them in ST(0). */
#define CI1(name, fn) SHIM(name) { ST(0) = fn(ST(0)); RETC(0); }
CI1(_CIcos, cos) CI1(_CIsin, sin) CI1(_CItan, tan) CI1(_CIacos, acos) CI1(_CIasin, asin) CI1(_CIatan, atan)
CI1(_CIcosh, cosh) CI1(_CIsinh, sinh) CI1(_CItanh, tanh) CI1(_CIexp, exp) CI1(_CIlog, log) CI1(_CIsqrt, sqrt)
SHIM(_CIpow) { double y = ST(0), x = ST(1); fpop(c); ST(0) = pow(x, y); RETC(0); }
SHIM(_CIatan2) { double x = ST(0), y = ST(1); fpop(c); ST(0) = atan2(y, x); RETC(0); }
SHIM(_CIfmod) { double y = ST(0), x = ST(1); fpop(c); ST(0) = fmod(x, y); RETC(0); }
SHIM(floor_) { fpush(c, floor(MF64(c->r[4] + 4))); RETC(0); }
SHIM(ceil_) { fpush(c, ceil(MF64(c->r[4] + 4))); RETC(0); }
SHIM(ldexp_) { fpush(c, ldexp(MF64(c->r[4] + 4), (int)M32(c->r[4] + 12))); RETC(0); }
SHIM(frexp_) {
    int e;
    double m = frexp(MF64(c->r[4] + 4), &e);
    M32(M32(c->r[4] + 12)) = (uint32_t)e;
    fpush(c, m);
    RETC(0);
}
SHIM(modf_) {
    double ip;
    double f = modf(MF64(c->r[4] + 4), &ip);
    MF64(M32(c->r[4] + 12)) = ip;
    fpush(c, f);
    RETC(0);
}
SHIM(_isnan) { RETC(isnan(MF64(c->r[4] + 4)) ? 1 : 0); }
SHIM(_finite) { RETC(isfinite(MF64(c->r[4] + 4)) ? 1 : 0); }
SHIM(_fpclass) {
    double v = MF64(c->r[4] + 4);
    uint32_t r;
    switch (fpclassify(v)) {
    case FP_NAN: r = 2; break;
    case FP_INFINITE: r = signbit(v) ? 4 : 0x200; break;
    case FP_ZERO: r = signbit(v) ? 0x20 : 0x40; break;
    case FP_SUBNORMAL: r = signbit(v) ? 0x10 : 0x80; break;
    default: r = signbit(v) ? 8 : 0x100; break;
    }
    RETC(r);
}

/* ---------------- setjmp/longjmp ----------------
 * _setjmp3 is lifted inline by the recompiler (host setjmp inside the
 * calling guest function); these shims only run if that did not happen. */
SHIM(_setjmp3) { rt_fatal("_setjmp3 reached as a plain call; the lifter must inline it"); }
SHIM(longjmp_) {
    extern void rt_longjmp(CPU *c, uint32_t jb, uint32_t val) __attribute__((noreturn));
    rt_longjmp(c, ARG(0), ARG(1));
}

/* added for D3DCompiler_43 */
SHIM(strrchr_) {
    char *r = strrchr(GSTR(ARG(0)), (int)ARG(1));
    RETC(r ? (uint32_t)((uint8_t *)r - MEM) : 0);
}
SHIM(strstr_) {
    char *r = strstr(GSTR(ARG(0)), GSTR(ARG(1)));
    RETC(r ? (uint32_t)((uint8_t *)r - MEM) : 0);
}
SHIM(getenv_) { RETC(0); }
SHIM(_mbstrlen) { RETC(strlen(GSTR(ARG(0)))); }
/* _vsnwprintf(wchar_t *buf, size_t count, const wchar_t *fmt, va_list): the
 * format is narrowed; %s arguments are taken as narrow (only error text). */
SHIM(_vsnwprintf) {
    uint32_t wf = ARG(2), cap = ARG(1);
    char fmt[1024];
    int k = 0;
    for (; k < 1023 && M16(wf + 2 * k); k++) fmt[k] = (char)M16(wf + 2 * k);
    fmt[k] = 0;
    char *buf = malloc(cap + 1);
    size_t n = guest_vformat(buf, cap + 1, fmt, ARG(3));
    for (uint32_t i = 0; i < cap && i <= n; i++) M16(ARG(0) + 2 * i) = (uint8_t)buf[i];
    free(buf);
    RETC(n < cap ? (int32_t)n : -1);
}

static const ShimDef defs[] = {
#define S(n) {"msvcrt.dll", #n, shim_##n},
#define S2(n, f) {"msvcrt.dll", n, shim_##f},
    S(_snprintf) S(_vsnprintf) S(sscanf) S(_isatty) S(_write) S(_lseeki64) S(_fileno)
    S(fopen) S(_wfopen) S(fclose) S(fseek) S(ftell) S(fgetc) S(rewind) S(remove)
    S(_stat) S(_wstat) S(_fstat) S(fprintf) S(printf) S(fscanf) S(fread) S(fwrite)
    S(fflush) S(_tempnam) S(_onexit) S(__dllonexit) S(_lock) S(_unlock) S(_amsg_exit) S(_initterm)
    S(_XcptFilter) S2("?terminate@@YAXXZ", terminate) S2("abort", abort_) S2("exit", exit_) S(_purecall)
    S(_CxxThrowException) S(__CxxFrameHandler) S2("??1type_info@@UAE@XZ", type_info_dtor) S(_errno)
    S(setlocale) S(_strtime) S(_strdate) S(_clearfp) S(_controlfp)
    S2("malloc", malloc_) S2("calloc", calloc_) S2("realloc", realloc_) S2("free", free_) S(_strdup)
    S2("??2@YAPAXI@Z", malloc_) S2("??3@YAXPAX@Z", free_) S2("memmove", memmove_) S2("memcpy", memcpy_)
    S2("memset", memset_) S2("strchr", strchr_) S2("strncmp", strncmp_) S(_stricmp) S(_strnicmp) S(_itoa)
    S(_ultoa) S2("atoi", atoi_) S2("atol", atol_) S2("atof", atof_) S2("strtod", strtod_) S2("wctomb", wctomb_)
    S2("wcstombs", wcstombs_) S2("isleadbyte", isleadbyte_) S2("isxdigit", isxdigit_) S2("isalnum", isalnum_)
    S2("isalpha", isalpha_) S2("isspace", isspace_) S2("isdigit", isdigit_) S2("iswspace", iswspace_)
    S2("iswalpha", iswalpha_) S2("iswdigit", iswdigit_) S2("iswpunct", iswpunct_) S2("toupper", toupper_)
    S2("tolower", tolower_) S2("srand", srand_) S2("rand", rand_) S2("qsort", qsort_)
    S(_CIcos) S(_CIsin) S(_CItan) S(_CIacos) S(_CIasin) S(_CIatan) S(_CIcosh) S(_CIsinh) S(_CItanh) S(_CIexp)
    S(_CIlog) S(_CIsqrt) S(_CIpow) S(_CIatan2) S(_CIfmod) S2("floor", floor_) S2("ceil", ceil_)
    S2("ldexp", ldexp_) S2("frexp", frexp_) S2("modf", modf_) S(_isnan) S(_finite) S(_fpclass)
    S(_setjmp3) S2("longjmp", longjmp_)
    S2("strrchr", strrchr_) S2("strstr", strstr_) S2("getenv", getenv_) S(_mbstrlen) S(_vsnwprintf)
#undef S
#undef S2
    {NULL, NULL, NULL}
};

/* Declared at the end of the file (see below). */
static void msvcr80_register(void);

void msvcrt_init(void) {
    rt_register_shims(defs);
    msvcr80_register();
    g_errno_addr = gmalloc(4);
    M32(g_errno_addr) = 0;
    g_iob = gmalloc(32 * 20);
    memset(MEM + g_iob, 0, 32 * 20);
    g_pioinfo = gmalloc(64 * 4);
    memset(MEM + g_pioinfo, 0, 64 * 4);
    g_badioinfo = gmalloc(64);
    memset(MEM + g_badioinfo, 0, 64);
    g_locale_c = gstrdup("C");
    rt_register_data("msvcrt.dll", "_iob", g_iob);
    rt_register_data("msvcrt.dll", "__pioinfo", g_pioinfo);
    rt_register_data("msvcrt.dll", "__badioinfo", g_badioinfo);
}

/* SWAT 4 is built with VC7.1 (/MD) and imports msvcr71.dll; its entry points
 * match the msvcrt.dll ones above, so the same shims answer for both. The
 * msvcr80 alias is kept for the older scaffold targets. */
static void msvcr80_register(void) {
    rt_register_shims_as("msvcr71.dll", defs);
    rt_register_data("msvcr71.dll", "_iob", g_iob);
    rt_register_data("msvcr71.dll", "__pioinfo", g_pioinfo);
    rt_register_data("msvcr71.dll", "__badioinfo", g_badioinfo);
    rt_register_shims_as("msvcr80.dll", defs);
    rt_register_data("msvcr80.dll", "_iob", g_iob);
    rt_register_data("msvcr80.dll", "__pioinfo", g_pioinfo);
    rt_register_data("msvcr80.dll", "__badioinfo", g_badioinfo);
}
