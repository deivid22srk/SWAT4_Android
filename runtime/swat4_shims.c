/* SWAT 4 host shims for what the shared runtime cannot cover.
 *
 * Most OS services live in kernel32.c / user32.c / msvcrt.c / misc.c /
 * d3d9_bridge.cpp. This file adds, from the real boot log on a phone and on
 * the desktop build:
 *   - MSVCR71 wide-character CRT (Windows wchar_t is 16-bit; the host's is not)
 *   - MSVCP71 std::string (__thiscall, MSVC 7.1 layout)
 *   - libm entry points returning in ST0
 *   - narrow CRT helpers missing from msvcrt.c
 *   - kernel32 file enumeration/attributes/directories and small services
 *   - OLE32/OLEAUT32/VERSION/ADVAPI32 basics
 *   - GDI/User32 stubs that never draw (D3DDrv renders through DXVK)
 *
 * Anything not here still aborts with "unimplemented import dll!name".
 */
#define _GNU_SOURCE
#include "rt.h"
#include "game.h"
/* Window registry living in user32.c (shared with the A-shims). */
extern uint32_t user32_next_hwnd(void);
extern void user32_win_register(uint32_t hwnd, uint32_t wndproc, uint32_t parent, uint32_t style, uint32_t exstyle);
extern void user32_win_unregister(uint32_t hwnd);
extern uint32_t user32_win_classproc(uint32_t hwnd);
extern uint32_t user32_main_hwnd(void);
extern int32_t user32_win_getlong(uint32_t hwnd, int32_t idx);
extern int32_t user32_win_setlong(uint32_t hwnd, int32_t idx, int32_t val);
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <time.h>
#include <math.h>
#include <dirent.h>
#include <fnmatch.h>
#include <sys/stat.h>
#include <unistd.h>
#include <errno.h>

size_t guest_vformat(char *out, size_t cap, const char *fmt, uint32_t argp);
extern uint32_t k32_create_event(int manual, int initial);
extern uint32_t k32_create_mutex(int initial);
extern uint32_t k32_wait_object(uint32_t h, uint32_t ms);

#define INVALID_HANDLE 0xFFFFFFFFu

/* ------------------------------------------------------------ conversions */
static void gw2a(uint32_t w, char *out, size_t n) {
    size_t i = 0;
    if (!n) return;
    if (!w) { out[0] = 0; return; }
    for (; i + 1 < n && M16(w + 2 * i); i++) out[i] = (char)M16(w + 2 * i);
    out[i] = 0;
}
static void ga2w(const char *s, uint32_t w, size_t n) {
    size_t i = 0;
    if (!w || !n) return;
    for (; s && s[i] && i + 1 < n; i++) M16(w + 2 * i) = (uint8_t)s[i];
    M16(w + 2 * i) = 0;
}
static uint32_t w_len(uint32_t p) {
    uint32_t n = 0;
    if (p) while (M16(p + 2 * n)) n++;
    return n;
}

/* --------------------------------------------------------- MSVCR71 wide */
SHIM(wcslen) {
    uint32_t p = ARG(0), n = w_len(p);
    if (n > 1000) LOG("wcslen(%08x) = %u (suspicious)", p, n);
    RETC(n);
}
SHIM(wcschr) {
    uint32_t p = ARG(0); uint16_t ch = (uint16_t)ARG(1);
    if (!p) RETC(0);
    for (;; p += 2) { if (M16(p) == ch) RETC(p); if (!M16(p)) RETC(0); }
}
SHIM(wcsrchr) {
    uint32_t p = ARG(0), r = 0; uint16_t ch = (uint16_t)ARG(1);
    if (!p) RETC(0);
    for (;; p += 2) { if (M16(p) == ch) r = p; if (!M16(p)) break; }
    RETC(r);
}
SHIM(wcsstr) {
    uint32_t h = ARG(0), n = ARG(1);
    if (!h || !n) RETC(0);
    uint32_t nl = w_len(n);
    if (!nl) RETC(h);
    for (uint32_t p = h; M16(p); p += 2) {
        uint32_t k = 0;
        while (k < nl && M16(p + 2 * k) == M16(n + 2 * k)) k++;
        if (k == nl) RETC(p);
    }
    RETC(0);
}
SHIM(wcspbrk) {
    uint32_t s = ARG(0), a = ARG(1);
    for (uint32_t p = s; p && M16(p); p += 2)
        for (uint32_t q = a; q && M16(q); q += 2)
            if (M16(p) == M16(q)) RETC(p);
    RETC(0);
}
SHIM(wcscpy) {
    uint32_t d = ARG(0), s = ARG(1), i = 0;
    if (!d || !s) RETC(d);
    do { M16(d + 2 * i) = M16(s + 2 * i); } while (M16(s + 2 * i++));
    RETC(d);
}
SHIM(wcsncpy) {
    uint32_t d = ARG(0), s = ARG(1), n = ARG(2), i = 0;
    if (!d) RETC(d);
    for (; i < n && s && M16(s + 2 * i); i++) M16(d + 2 * i) = M16(s + 2 * i);
    for (; i < n; i++) M16(d + 2 * i) = 0;
    RETC(d);
}
SHIM(wcscat) {
    uint32_t d = ARG(0), s = ARG(1), i = w_len(d), j = 0;
    if (!d || !s) RETC(d);
    while ((M16(d + 2 * (i + j)) = M16(s + 2 * j))) j++;
    RETC(d);
}
static int w_cmp(uint32_t a, uint32_t b, uint32_t n, int icase) {
    for (uint32_t i = 0; i < n; i++) {
        uint16_t x = a ? M16(a + 2 * i) : 0, y = b ? M16(b + 2 * i) : 0;
        if (icase) { if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; }
        if (x != y) return x < y ? -1 : 1;
        if (!x) break;
    }
    return 0;
}
SHIM(wcscmp) { RETC((uint32_t)w_cmp(ARG(0), ARG(1), 0x7FFFFFFFu, 0)); }
SHIM(wcsncmp) { RETC((uint32_t)w_cmp(ARG(0), ARG(1), ARG(2), 0)); }
SHIM(_wcsicmp) { RETC((uint32_t)w_cmp(ARG(0), ARG(1), 0x7FFFFFFFu, 1)); }
SHIM(_wcsnicmp) { RETC((uint32_t)w_cmp(ARG(0), ARG(1), ARG(2), 1)); }
SHIM(_wcsupr) {
    uint32_t p = ARG(0);
    for (; p && M16(p); p += 2) if (M16(p) >= 'a' && M16(p) <= 'z') M16(p) -= 32;
    RETC(ARG(0));
}
SHIM(_wcslwr) {
    uint32_t p = ARG(0);
    for (; p && M16(p); p += 2) if (M16(p) >= 'A' && M16(p) <= 'Z') M16(p) += 32;
    RETC(ARG(0));
}
SHIM(_wtoi) { char b[64]; gw2a(ARG(0), b, sizeof b); RETC((uint32_t)atoi(b)); }
SHIM(wcstoul) {
    char b[256], *e; gw2a(ARG(0), b, sizeof b);
    unsigned long v = strtoul(b, &e, (int)ARG(2));
    if (ARG(1)) M32(ARG(1)) = ARG(0) + 2 * (uint32_t)(e - b);
    RETC((uint32_t)v);
}
SHIM(_itow) {
    char b[32];
    int base = (int)ARG(2);
    if (base == 10) snprintf(b, sizeof b, "%d", (int)ARG(0));
    else snprintf(b, sizeof b, "%x", (unsigned)ARG(0));
    ga2w(b, ARG(1), 34);
    RETC(ARG(1));
}
SHIM(_wstrdate) {
    char b[32]; time_t t = time(NULL); struct tm tmv; localtime_r(&t, &tmv);
    strftime(b, sizeof b, "%m/%d/%y", &tmv); ga2w(b, ARG(0), 9); RETC(ARG(0));
}
SHIM(_wstrtime) {
    char b[32]; time_t t = time(NULL); struct tm tmv; localtime_r(&t, &tmv);
    strftime(b, sizeof b, "%H:%M:%S", &tmv); ga2w(b, ARG(0), 9); RETC(ARG(0));
}
SHIM(swprintf) {
    /* swprintf(buf, wide fmt, ...): format with WIDE semantics — %s takes a
     * wide string — then widen into the guest buffer. */
    char buf[4096];
    size_t n = guest_vwfmt(buf, sizeof buf - 1, ARG(1), c->r[4] + 12);
    if (n > sizeof buf - 1) n = sizeof buf - 1;
    buf[n] = 0;
    RETC((uint32_t)widen_to_guest(ARG(0), 4096, buf, n));
}
SHIM(_snwprintf) {
    /* _snwprintf(buf, cap, wide fmt, ...) — same as swprintf with a cap. */
    uint32_t cap = ARG(1);
    if (cap > 65536) cap = 65536;
    if (!cap) RETC((uint32_t)-1);
    char buf[4096];
    size_t n = guest_vwfmt(buf, sizeof buf - 1, ARG(2), c->r[4] + 16);
    if (n > sizeof buf - 1) n = sizeof buf - 1;
    buf[n] = 0;
    if (n >= cap) { widen_to_guest(ARG(0), cap, buf, cap - 1); RETC((uint32_t)-1); }
    RETC((uint32_t)widen_to_guest(ARG(0), cap, buf, n));
}
SHIM(swscanf) { LOG("swscanf: not implemented"); RETC(0); }

/* --------------------------------------------------- MSVCP71 std::string */
/* MSVC 7.1 basic_string<char>: _Bx (16-byte union), _Mysize at +16,
 * _Myres at +20; small-string when _Myres < 16. These are __thiscall:
 * this is in ECX, explicit args start at [esp+4]. */
#define STR_SIZE(o) M32((o) + 16)
#define STR_RES(o)  M32((o) + 20)
static void str_assign(uint32_t self, const char *s, uint32_t n) {
    STR_SIZE(self) = n;
    if (n < 16) {
        STR_RES(self) = 15;
        if (n) memcpy(MEM + self, s, n);
        M8(self + n) = 0;
    } else {
        uint32_t q = gmalloc(n + 1);
        if (n) memcpy(MEM + q, s, n);
        M8(q + n) = 0;
        M32(self) = q;
        STR_RES(self) = n;
    }
}
SHIM(cpp_str_ctor_cstr) {
    uint32_t self = c->r[1];
    const char *s = GSTR(ARG(0));
    if (self) str_assign(self, s ? s : "", s ? (uint32_t)strlen(s) : 0);
    RET(self, 1);
}
SHIM(cpp_str_ctor_copy) {
    uint32_t self = c->r[1], o = ARG(0);
    if (self) {
        uint32_t n = o ? STR_SIZE(o) : 0;
        uint32_t p = o ? (STR_RES(o) < 16 ? o : M32(o)) : 0;
        str_assign(self, p ? (const char *)(MEM + p) : "", n);
    }
    RET(self, 1);
}
SHIM(cpp_str_dtor) {
    uint32_t self = c->r[1];
    if (self) {
        if (STR_RES(self) >= 16 && M32(self)) gfree(M32(self));
        STR_SIZE(self) = 0; STR_RES(self) = 0; M32(self) = 0;
    }
    RET(0, 0);
}
SHIM(cpp_str_c_str) {
    uint32_t self = c->r[1];
    RET(self ? (STR_RES(self) < 16 ? self : M32(self)) : 0, 0);
}

/* --------------------------------------------------------------- libm */
/* MSVC 7.1 returns double in ST0; __cdecl means the lifted caller pops the
 * argument (it contains the add esp). */
#define M1(name, expr) SHIM(name) { double x = MF64(c->r[4] + 4); fpush(c, (expr)); RETC(0); }
M1(m_acos, acos(x))
M1(m_asin, asin(x))
M1(m_atan, atan(x))
M1(m_fabs, fabs(x))
M1(m_log, log(x))
M1(m_log10, log10(x))
M1(m_sin, sin(x))
M1(m_cos, cos(x))
M1(m_tan, tan(x))
M1(m_sqrt, sqrt(x))
M1(m_exp, exp(x))
M1(m_ceil, ceil(x))
M1(m_floor, floor(x))
SHIM(m_pow) { double a = MF64(c->r[4] + 4), b = MF64(c->r[4] + 12); fpush(c, pow(a, b)); RETC(0); }
SHIM(m_fmod) { double a = MF64(c->r[4] + 4), b = MF64(c->r[4] + 12); fpush(c, fmod(a, b)); RETC(0); }

/* Host replacement for Swat4.exe 0x10903FA0 (see tools/lift.py). The original
 * is an MMX memcpy whose tail is a Duff's-device `jmp ecx` into a run of
 * one-byte `movsd`; the lifted form cannot jump to those mid-instruction
 * targets. cdecl: the caller pops the three arguments. */
void f_10903FA0(CPU *c) {
    uint32_t d = ARG(0), s = ARG(1), n = ARG(2);
    if (n) memmove(MEM + d, MEM + s, n);
    RETC(d);
}
/* Core.dll exported CRT wrappers (see tools/lift.py). The originals tail-jump
 * into the middle of an MMX memcpy; these are the plain operations. */
void f_100270C(CPU *c) {  /* wchar_t *appStrcpy(wchar_t *d, const wchar_t *s) */
    uint32_t d = ARG(0), s = ARG(1), i = 0;
    if (d && s) {
        do { M16(d + 2 * i) = M16(s + 2 * i); } while (M16(s + 2 * i++));
    }
    RETC(d);
}
void f_10018E8(CPU *c) {  /* void *appMemmove(void *d, const void *s, int n) */
    uint32_t d = ARG(0), s = ARG(1), n = ARG(2);
    if (n) memmove(MEM + d, MEM + s, n);
    RETC(d);
}
void f_10015F5(CPU *c) {  /* void appMemset(void *d, int v, int n) */
    uint32_t d = ARG(0);
    int v = (int)ARG(1);
    uint32_t n = ARG(2);
    if (n) memset(MEM + d, v, n);
    RETC(0);
}
void f_100276B(CPU *c) {  /* int appMemcmp(const void *a, const void *b, int n) */
    uint32_t a = ARG(0), b = ARG(1), n = ARG(2);
    RETC(n ? (uint32_t)memcmp(MEM + a, MEM + b, n) : 0);
}

/* ------------------------------------------------------ narrow CRT bits */
SHIM(memchr) {
    void *p = memchr(GPTR(ARG(0)), ARG(1), ARG(2));
    RETC(p ? (uint32_t)((uint8_t *)p - MEM) : 0);
}
SHIM(islower) { RETC(islower((int)ARG(0)) ? 1 : 0); }
SHIM(isupper) { RETC(isupper((int)ARG(0)) ? 1 : 0); }
SHIM(strpbrk) {
    char *p = strpbrk(GSTR(ARG(0)), GSTR(ARG(1)));
    RETC(p ? (uint32_t)((uint8_t *)p - MEM) : 0);
}
SHIM(strtol) {
    char *s = GSTR(ARG(0)), *e = NULL;
    long v = strtol(s ? s : "", &e, (int)ARG(2));
    if (ARG(1)) M32(ARG(1)) = (uint32_t)((uint8_t *)e - MEM);
    RETC((uint32_t)v);
}
SHIM(strtoul) {
    char *s = GSTR(ARG(0)), *e = NULL;
    unsigned long v = strtoul(s ? s : "", &e, (int)ARG(2));
    if (ARG(1)) M32(ARG(1)) = (uint32_t)((uint8_t *)e - MEM);
    RETC((uint32_t)v);
}
SHIM(strncat) {
    char *d = GSTR(ARG(0)), *s = GSTR(ARG(1));
    if (!d || !s) RETC(ARG(0));
    strncat(d, s, ARG(2));
    RETC(ARG(0));
}
SHIM(strncpy) {
    char *d = GSTR(ARG(0)), *s = GSTR(ARG(1));
    if (!d || !s) RETC(ARG(0));
    strncpy(d, s, ARG(2));
    RETC(ARG(0));
}
SHIM(strcspn) { RETC(strcspn(GSTR(ARG(0)) ? GSTR(ARG(0)) : "", GSTR(ARG(1)) ? GSTR(ARG(1)) : "")); }
SHIM(_strlwr) {
    char *s = GSTR(ARG(0));
    if (s) for (; *s; s++) if (*s >= 'A' && *s <= 'Z') *s += 32;
    RETC(ARG(0));
}
SHIM(_strupr) {
    char *s = GSTR(ARG(0));
    if (s) for (; *s; s++) if (*s >= 'a' && *s <= 'z') *s -= 32;
    RETC(ARG(0));
}
SHIM(_atoi64) {
    int64_t v = strtoll(GSTR(ARG(0)) ? GSTR(ARG(0)) : "", NULL, 10);
    c->r[0] = (uint32_t)v;
    c->r[2] = (uint32_t)((uint64_t)v >> 32);
    c->r[4] += 4;
}
SHIM(sprintf) {
    char *out = GSTR(ARG(0));
    size_t n = out ? guest_vformat(out, 4096, GSTR(ARG(1)), c->r[4] + 12) : 0;
    RETC(n);
}
SHIM(vsprintf) {
    char *out = GSTR(ARG(0));
    size_t n = out ? guest_vformat(out, 4096, GSTR(ARG(1)), ARG(2)) : 0;
    RETC(n);
}
SHIM(_aligned_malloc) {
    uint32_t a = ARG(0), n = ARG(1);
    if (!a) a = 8;
    uint32_t p = gmalloc(n + a + 8);
    uint32_t q = (p + 8 + a - 1) & ~(a - 1);
    M32(q - 4) = p;
    RETC(q);
}
SHIM(_aligned_free) {
    uint32_t q = ARG(0);
    if (q) gfree(M32(q - 4));
    RETC(0);
}
/* MSVCR71 CRT start-up plumbing (the exe's static CRT calls these). */
SHIM(crt_set_app_type) { RETC(0); }
SHIM(crt_p_commode) { static uint32_t p; if (!p) { p = gmalloc(4); M32(p) = 0; } RETC(p); }
SHIM(crt_p_fmode) { static uint32_t p; if (!p) { p = gmalloc(4); M32(p) = 0; } RETC(p); }
SHIM(crt_setusermatherr) { RETC(0); }
SHIM(crt_getmainargs) {
    static uint32_t av, str;
    if (!av) { str = gstrdup(GAME_EXE); av = gmalloc(8); M32(av) = str; M32(av + 4) = 0; }
    if (ARG(0)) M32(ARG(0)) = 1;
    if (ARG(1)) M32(ARG(1)) = av;
    if (ARG(2)) M32(ARG(2)) = 0;
    RETC(0);
}
SHIM(crt_cexit) { RETC(0); }
SHIM(crt_c_exit) { RETC(0); }
SHIM(crt_exit) { exit((int)ARG(0)); }
SHIM(crt_ismbblead) { RETC(0); }
SHIM(crt_adjust_fdiv) { RETC(0); }
SHIM(crt_except_handler3) { RETC(1); }
SHIM(crt_cpp_xcpt_filter) { RETC(0); }

/* --------------------------------------------------------------- time */
static uint32_t tm_buf(void) {
    static uint32_t p;
    if (!p) { p = gmalloc(36); memset(MEM + p, 0, 36); }
    return p;
}
SHIM(crt_time) {
    time_t t = time(NULL);
    if (ARG(0)) M32(ARG(0)) = (uint32_t)t;
    RETC((uint32_t)t);
}
SHIM(crt_gmtime) {
    time_t t = (time_t)(ARG(0) ? M32(ARG(0)) : 0);
    struct tm tmv; gmtime_r(&t, &tmv);
    uint32_t p = tm_buf();
    M32(p + 0) = tmv.tm_sec; M32(p + 4) = tmv.tm_min; M32(p + 8) = tmv.tm_hour;
    M32(p + 12) = tmv.tm_mday; M32(p + 16) = tmv.tm_mon; M32(p + 20) = tmv.tm_year;
    M32(p + 24) = tmv.tm_wday; M32(p + 28) = tmv.tm_yday; M32(p + 32) = tmv.tm_isdst;
    RETC(p);
}
SHIM(crt_localtime) {
    time_t t = (time_t)(ARG(0) ? M32(ARG(0)) : 0);
    struct tm tmv; localtime_r(&t, &tmv);
    uint32_t p = tm_buf();
    M32(p + 0) = tmv.tm_sec; M32(p + 4) = tmv.tm_min; M32(p + 8) = tmv.tm_hour;
    M32(p + 12) = tmv.tm_mday; M32(p + 16) = tmv.tm_mon; M32(p + 20) = tmv.tm_year;
    M32(p + 24) = tmv.tm_wday; M32(p + 28) = tmv.tm_yday; M32(p + 32) = tmv.tm_isdst;
    RETC(p);
}
SHIM(crt_mktime) {
    uint32_t p = ARG(0);
    struct tm tmv; memset(&tmv, 0, sizeof tmv);
    tmv.tm_sec = (int)M32(p + 0); tmv.tm_min = (int)M32(p + 4); tmv.tm_hour = (int)M32(p + 8);
    tmv.tm_mday = (int)M32(p + 12); tmv.tm_mon = (int)M32(p + 16); tmv.tm_year = (int)M32(p + 20);
    tmv.tm_isdst = (int)M32(p + 32);
    RETC((uint32_t)mktime(&tmv));
}
SHIM(crt_difftime) {
    double d = difftime((time_t)M32(c->r[4] + 4), (time_t)M32(c->r[4] + 8));
    fpush(c, d);
    RETC(0);
}
SHIM(crt_clock) { RETC((uint32_t)clock()); }

/* ------------------------------------------- kernel32: files and small */
static void unix_to_ft(time_t t, uint32_t a) {
    uint64_t v = ((uint64_t)t + 11644473600ULL) * 10000000ULL;
    M32(a) = (uint32_t)v;
    M32(a + 4) = (uint32_t)(v >> 32);
}
static uint32_t attrs_of_mode(mode_t m) {
    uint32_t a = S_ISDIR(m) ? 0x10 : 0x80;
    if (!(m & 0200)) a |= 0x01;
    return a;
}
static uint32_t map_attrs(struct stat *st) { return attrs_of_mode(st->st_mode); }

SHIM(GetFileAttributesW) {
    char path[1024], host[1024];
    gw2a(ARG(0), path, sizeof path);
    {   /* diagnostic: unique probed paths + one guest chain dump once the
         * config-file polling loop is clearly spinning */
        static char last[1024];
        static int shown, calls, dumped;
        if (!last[0] || strcmp(last, path)) {
            snprintf(last, sizeof last, "%s", path);
            if (shown++ < 400) LOG("GetFileAttributesW '%s'", path);
        }
        if (!dumped && ++calls > 3000) {
            dumped = 1;
            fprintf(stderr, "GFAW-WATCHDOG after %d calls: guest stack scan\n", calls);
            fprintf(stderr, "  chain:");
            uint32_t sp = c->r[4];
            for (int i = 0; i < 600; i++) {
                uint32_t v = M32(sp + 4 * i);
                if (v < 0x10000 || v >= 0x40000000u) continue;
                const char *mod = NULL; uint32_t base = 0;
                if (v >= 0x10900000 && v < 0x10950000) { mod = "Swat4.exe"; base = 0x10900000; }
                else {
                    extern const char *const g_dll_modules[];
                    extern const uint32_t g_dll_bases[];
                    for (int m = 0; g_dll_modules[m]; m++) {
                        uint32_t b = g_dll_bases[m];
                        if (b && v >= b && v - b < 0x1000000u && b > base) { base = b; mod = g_dll_modules[m]; }
                    }
                }
                if (mod) fprintf(stderr, " %s+%x", mod, v - base);
            }
            fputc('\n', stderr);
            fflush(stderr);
        }
    }
    struct stat st;
    if (rt_host_path(path, host, sizeof host) < 0 || stat(host, &st)) { rt_set_last_error(c, 2); RET(INVALID_HANDLE, 1); }
    RET(attrs_of_mode(st.st_mode), 1);
}
SHIM(GetFileAttributesExW) {
    char path[1024], host[1024];
    gw2a(ARG(0), path, sizeof path);
    if (rt_host_path(path, host, sizeof host) < 0) { rt_set_last_error(c, 2); RET(0, 3); }
    struct stat st;
    if (stat(host, &st)) { rt_set_last_error(c, 2); RET(0, 3); }
    uint32_t o = ARG(2);
    if (o) {
        M32(o) = attrs_of_mode(st.st_mode);
        unix_to_ft(st.st_mtime, o + 4); unix_to_ft(st.st_mtime, o + 12); unix_to_ft(st.st_mtime, o + 20);
        M32(o + 28) = (uint32_t)((uint64_t)st.st_size >> 32);
        M32(o + 32) = (uint32_t)st.st_size;
    }
    RET(1, 3);
}
typedef struct { DIR *d; char dir[1024]; char pat[260]; } GFindW;
static int find_next_w(GFindW *g, uint32_t out) {
    struct dirent *de;
    while ((de = readdir(g->d))) {
        if (fnmatch(g->pat, de->d_name, FNM_CASEFOLD)) continue;
        char full[1400]; struct stat st;
        snprintf(full, sizeof full, "%s/%s", g->dir, de->d_name);
        if (stat(full, &st)) continue;
        memset(MEM + out, 0, 592);
        M32(out) = attrs_of_mode(st.st_mode);
        unix_to_ft(st.st_mtime, out + 4); unix_to_ft(st.st_mtime, out + 12); unix_to_ft(st.st_mtime, out + 20);
        M32(out + 28) = (uint32_t)((uint64_t)st.st_size >> 32);
        M32(out + 32) = (uint32_t)st.st_size;
        ga2w(de->d_name, out + 44, 260);
        return 1;
    }
    return 0;
}
SHIM(FindFirstFileW) {
    char narrow[1024], host[1024];
    gw2a(ARG(0), narrow, sizeof narrow);
    rt_host_path(narrow, host, sizeof host);
    GFindW *g = calloc(1, sizeof *g);
    char *slash = strrchr(host, '/');
    if (slash) { *slash = 0; snprintf(g->dir, sizeof g->dir, "%s", host); snprintf(g->pat, sizeof g->pat, "%s", slash + 1); }
    else { strcpy(g->dir, "."); snprintf(g->pat, sizeof g->pat, "%s", host); }
    if (!strcmp(g->pat, "*.*")) strcpy(g->pat, "*");
    g->d = opendir(g->dir);
    TRACE("  FFFW spec=%s host=%s", narrow, host);
    if (!g->d || !find_next_w(g, ARG(1))) {
        if (g->d) closedir(g->d);
        free(g);
        TRACE("  FFFW MISS (lasterr=2)");
        rt_set_last_error(c, 2);
        RET(INVALID_HANDLE, 2);
    }
    RET(h_new(H_FIND, g), 2);
}
SHIM(FindNextFileW) {
    GFindW *g = h_get(ARG(0), H_FIND);
    if (!g || !find_next_w(g, ARG(1))) { rt_set_last_error(c, 18); RET(0, 2); }
    RET(1, 2);
}
SHIM(GetCurrentDirectoryW) {
    uint32_t n = ARG(0);
    const char *d = GAME_WIN_DIR;
    uint32_t l = (uint32_t)strlen(d);
    if (n > l) ga2w(d, ARG(1), n);
    RET(l, 2);
}
SHIM(GetCurrentDirectoryA) {
    uint32_t n = ARG(0);
    const char *d = GAME_WIN_DIR;
    uint32_t l = (uint32_t)strlen(d);
    if (n > l) snprintf(GSTR(ARG(1)), n, "%s", d);
    RET(l, 2);
}
SHIM(GetSystemDirectoryW) { ga2w("C:\\Windows\\System32", ARG(0), ARG(1) ? ARG(1) : 1); RET(19, 2); }
SHIM(GetWindowsDirectoryW) { ga2w("C:\\Windows", ARG(0), ARG(1) ? ARG(1) : 1); RET(10, 2); }
SHIM(CreateDirectoryW) {
    char path[1024], host[1024];
    gw2a(ARG(0), path, sizeof path);
    rt_host_path(path, host, sizeof host);
    int ok = mkdir(host, 0755) == 0;
    if (!ok && errno == EEXIST) ok = 1; /* Windows reports success for ALREADY_EXISTS users of UE2's MakeDirectory */
    if (!ok)
        rt_set_last_error(c, (errno == EACCES || errno == EPERM || errno == EROFS) ? 5 /*ACCESS_DENIED*/
                                                                                    : 3 /*PATH_NOT_FOUND*/);
    LOG("CreateDirectoryW(%s) -> %d lasterr=%u", path, ok, ok ? 0 : (errno == EACCES ? 5 : 3));
    RET(ok, 2);
}
SHIM(RemoveDirectoryW) {
    char path[1024], host[1024];
    gw2a(ARG(0), path, sizeof path);
    rt_host_path(path, host, sizeof host);
    RET(rmdir(host) == 0, 1);
}
SHIM(SetFileAttributesW) { RET(1, 2); }
SHIM(GetDriveTypeW) { (void)ARG(0); RET(3, 1); }
SHIM(GetDriveTypeA) { (void)ARG(0); RET(3, 1); }
SHIM(GetDiskFreeSpaceExW) {
    if (ARG(1)) M64(ARG(1)) = 16ull << 30;
    if (ARG(2)) M64(ARG(2)) = 64ull << 30;
    if (ARG(3)) M64(ARG(3)) = 16ull << 30;
    RET(1, 4);
}
SHIM(GetVolumeInformationA) {
    if (ARG(1) && ARG(2)) snprintf(GSTR(ARG(1)), ARG(2), "SWAT4");
    if (ARG(3)) M32(ARG(3)) = 0x12345678;
    if (ARG(4)) M32(ARG(4)) = 255;
    if (ARG(5)) M32(ARG(5)) = 0;
    if (ARG(6) && ARG(7)) snprintf(GSTR(ARG(6)), ARG(7), "NTFS");
    RET(1, 8);
}
SHIM(GetLocalTime) {
    time_t t = time(NULL); struct tm tmv; localtime_r(&t, &tmv);
    uint32_t o = ARG(0);
    M16(o + 0) = (uint16_t)(tmv.tm_year + 1900);
    M16(o + 2) = (uint16_t)(tmv.tm_mon + 1);
    M16(o + 4) = (uint16_t)tmv.tm_wday;
    M16(o + 6) = (uint16_t)tmv.tm_mday;
    M16(o + 8) = (uint16_t)tmv.tm_hour;
    M16(o + 10) = (uint16_t)tmv.tm_min;
    M16(o + 12) = (uint16_t)tmv.tm_sec;
    M16(o + 14) = 0;
    RET(0, 1);
}
SHIM(VirtualQuery) {
    uint32_t a = ARG(0), o = ARG(1);
    if (!o) RET(0, 3);
    memset(MEM + o, 0, 28);
    M32(o + 0) = a & ~0xFFFFu;
    M32(o + 4) = a & ~0xFFFFu;
    M32(o + 12) = 0x10000;
    M32(o + 16) = 0x1000;  /* MEM_COMMIT */
    M32(o + 20) = 0x04;    /* PAGE_READWRITE */
    M32(o + 24) = 0x20000; /* MEM_PRIVATE */
    RET(28, 3);
}
SHIM(VirtualProtect) { if (ARG(3)) M32(ARG(3)) = 0x04; RET(1, 4); }
SHIM(GlobalAlloc) { RET(gmalloc(ARG(1) ? ARG(1) : 1), 2); }
SHIM(GlobalLock) { RET(ARG(0), 1); }
SHIM(GlobalUnlock) { RET(1, 1); }
SHIM(GetProcessWorkingSetSize) {
    if (ARG(1)) M32(ARG(1)) = 32u << 20;
    if (ARG(2)) M32(ARG(2)) = 64u << 20;
    RET(1, 3);
}
SHIM(GetVersionExW) {
    uint32_t o = ARG(0);
    if (!o) RET(0, 1);
    M32(o + 0) = 276; M32(o + 4) = 5; M32(o + 8) = 1; M32(o + 12) = 2600;
    M32(o + 16) = 2; ga2w("Service Pack 3", o + 20, 128);
    RET(1, 1);
}
SHIM(GetComputerNameA) {
    const char *n = "SWAT4"; uint32_t l = (uint32_t)strlen(n);
    if (ARG(1) && M32(ARG(1)) > l) { snprintf(GSTR(ARG(0)), M32(ARG(1)), "%s", n); M32(ARG(1)) = l; }
    RET(1, 2);
}
SHIM(GetComputerNameW) {
    const char *n = "SWAT4"; uint32_t l = (uint32_t)strlen(n);
    if (ARG(1) && M32(ARG(1)) > l) { ga2w(n, ARG(0), M32(ARG(1))); M32(ARG(1)) = l; }
    RET(1, 2);
}
SHIM(GetUserNameA) {
    const char *n = "Player"; uint32_t l = (uint32_t)strlen(n);
    if (ARG(1)) { if (M32(ARG(1)) > l) { snprintf(GSTR(ARG(0)), M32(ARG(1)), "%s", n); M32(ARG(1)) = l + 1; } }
    RET(1, 2);
}
SHIM(GetUserNameW) {
    const char *n = "Player"; uint32_t l = (uint32_t)strlen(n);
    if (ARG(1)) { if (M32(ARG(1)) > l) { ga2w(n, ARG(0), M32(ARG(1))); M32(ARG(1)) = l + 1; } }
    RET(1, 2);
}
SHIM(MulDiv) {
    int64_t v = (int64_t)(int32_t)ARG(0) * (int32_t)ARG(1);
    int32_t d = (int32_t)ARG(2);
    RET((uint32_t)(d ? v / d : -1), 3);
}
SHIM(lstrlenA) { RET((uint32_t)strlen(GSTR(ARG(0)) ? GSTR(ARG(0)) : ""), 1); }
SHIM(lstrcmpiW) { RET((uint32_t)w_cmp(ARG(0), ARG(1), 0x7FFFFFFFu, 1), 2); }
SHIM(GetCommandLineW) {
    static uint32_t p;
    if (!p) { char b[512]; snprintf(b, sizeof b, "\"%s\\%s\"", GAME_WIN_DIR, GAME_EXE); p = gmalloc(2 * (strlen(b) + 1)); ga2w(b, p, strlen(b) + 1); }
    RET(p, 0);
}
SHIM(GetEnvironmentVariableA) { (void)ARG(0); (void)ARG(1); (void)ARG(2); RET(0, 3); }
SHIM(CopyFileW) {
    char a[1024], b[1024], ha[1024], hb[1024];
    gw2a(ARG(0), a, sizeof a); gw2a(ARG(1), b, sizeof b);
    rt_host_path(a, ha, sizeof ha); rt_host_path(b, hb, sizeof hb);
    FILE *in = fopen(ha, "rb"); if (!in) RET(0, 3);
    FILE *out = fopen(hb, "wb"); if (!out) { fclose(in); RET(0, 3); }
    char buf[8192]; size_t n;
    while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
    fclose(in); fclose(out);
    RET(1, 3);
}
SHIM(CreateEventW) { RET(k32_create_event(ARG(1) != 0, ARG(2) != 0), 4); }
SHIM(CreateMutexW) { RET(k32_create_mutex(ARG(1) != 0), 3); }
SHIM(OpenEventW) { LOG("OpenEventW: named events unsupported, returning a new event"); RET(k32_create_event(1, 1), 3); }
SHIM(WaitForSingleObjectEx) { RET(k32_wait_object(ARG(0), ARG(1)), 3); }
SHIM(SetHandleInformation) { RET(1, 3); }
SHIM(SetConsoleMode) { RET(1, 2); }
SHIM(LockFileEx) { RET(1, 6); }
SHIM(UnlockFileEx) { RET(1, 5); }
SHIM(GetThreadContext) { RET(0, 2); }
SHIM(FormatMessageW) { RET(0, 7); }
SHIM(TerminateThread) { LOG("TerminateThread(%08x) ignored", ARG(0)); RET(1, 2); }


/* --------------------------------------------------------- OLE/version */
SHIM(CoCreateGuid) {
    uint32_t o = ARG(0);
    for (int i = 0; i < 16; i += 4) M32(o + i) = (uint32_t)rand();
    RET(0, 1);
}
SHIM(CoTaskMemFree) { if (ARG(0)) gfree(ARG(0)); RET(0, 1); }
SHIM(CLSIDFromString) { if (ARG(1)) memset(MEM + ARG(1), 0, 16); RET(0, 2); }
SHIM(StringFromCLSID) {
    static const char *z = "{00000000-0000-0000-0000-000000000000}";
    uint32_t p = gmalloc(2 * 40);
    ga2w(z, p, 40);
    if (ARG(1)) M32(ARG(1)) = p;
    RET(0, 2);
}
SHIM(IsEqualGUID) { RET(memcmp(MEM + ARG(0), MEM + ARG(1), 16) == 0, 2); }
SHIM(VariantInit) { memset(MEM + ARG(0), 0, 16); RET(0, 1); }
SHIM(VariantClear) { memset(MEM + ARG(0), 0, 16); RET(0, 1); }
SHIM(GetFileVersionInfoSizeA) { if (ARG(1)) M32(ARG(1)) = 0; RET(0, 2); }
SHIM(GetFileVersionInfoA) { RET(0, 4); }
SHIM(VerQueryValueA) { if (ARG(3)) M32(ARG(3)) = 0; RET(0, 4); }
SHIM(CryptAcquireContextA) {
    if (ARG(0)) M32(ARG(0)) = 0x43525950u;
    RET(1, 5);
}
SHIM(CryptGenRandom) {
    uint8_t *p = (uint8_t *)GPTR(ARG(2));
    for (uint32_t i = 0; p && i < ARG(1); i++) p[i] = (uint8_t)rand();
    RET(1, 3);
}
SHIM(CryptReleaseContext) { RET(1, 2); }
SHIM(RegCreateKeyExA) { if (ARG(7)) M32(ARG(7)) = 0; RET(0, 9); }
SHIM(RegDeleteKeyA) { RET(0, 2); }
SHIM(RegDeleteValueA) { RET(0, 2); }

/* ------------------------------------------------ User32 odds and ends */
SHIM(wsprintfA) {
    char *out = GSTR(ARG(0));
    size_t n = out ? guest_vformat(out, 4096, GSTR(ARG(1)), c->r[4] + 12) : 0;
    RETC(n);
}
SHIM(SetTimer) { RET(0x1001, 4); }
SHIM(KillTimer) { RET(1, 2); }
SHIM(OpenClipboard) { RET(1, 1); }
SHIM(EmptyClipboard) { RET(1, 0); }
SHIM(SetClipboardData) { RET(ARG(1), 2); }
SHIM(GetClipboardData) { RET(0, 1); }
SHIM(CloseClipboard) { RET(1, 0); }
SHIM(GetKeyState) { RET(0, 1); }
SHIM(GetAsyncKeyState) { RET(0, 1); }
static uint32_t g_winmsg = 0xC000;
SHIM(RegisterWindowMessageA) { RET(g_winmsg++, 1); }
SHIM(RegisterWindowMessageW) { RET(g_winmsg++, 1); }
/* Window class registry: Window.dll registers WinBaseClass with
 * RegisterClassExW and then asserts unless GetClassInfoExW returns it. */
typedef struct { char name[128]; uint8_t wc[48]; } WinClass;
static WinClass g_classes[64];
static int g_nclasses;
static int class_find(const char *n) {
    for (int i = 0; i < g_nclasses; i++)
        if (!strcasecmp(g_classes[i].name, n)) return i;
    return -1;
}
SHIM(CreateWindowExW) {
    uint32_t clsarg = ARG(1), wndproc = 0;
    if (clsarg >= 0x10000) {
        char cls[128]; gw2a(clsarg, cls, sizeof cls);
        int ci = class_find(cls);
        if (ci >= 0) wndproc = *(uint32_t *)(g_classes[ci].wc + 8);
    } else {
        int ci = (int)clsarg - 0xC000;
        if (ci >= 0 && ci < g_nclasses) wndproc = *(uint32_t *)(g_classes[ci].wc + 8);
    }
    uint32_t hwnd = user32_next_hwnd();
    static int create_depth;
    if (wndproc && create_depth < 2) {
        create_depth++;
        uint32_t cs = gmalloc(48);
        memset(MEM + cs, 0, 48);
        M32(cs + 0) = ARG(11); M32(cs + 4) = ARG(10); M32(cs + 8) = ARG(9);
        M32(cs + 12) = ARG(8); M32(cs + 16) = ARG(7); M32(cs + 20) = ARG(6);
        M32(cs + 24) = ARG(5); M32(cs + 28) = ARG(4); M32(cs + 32) = ARG(3);
        M32(cs + 36) = ARG(2); M32(cs + 40) = ARG(1); M32(cs + 44) = ARG(0);
        uint32_t a[4] = {hwnd, 0x81 /* WM_NCCREATE */, 0, cs};
        uint32_t r = rt_invoke(c, wndproc, 4, a);
        {   /* diagnostic: which object/virtual answered WM_NCCREATE */
            uint32_t obj = ARG(11), vt = obj ? M32(obj) : 0;
            LOG("CreateWindowExW hwnd=%08x wndproc=%08x WM_NCCREATE -> %u (obj=%08x vtbl=%08x vt+1C=%08x exstyle=%08x)",
                hwnd, wndproc, r, obj, vt, vt ? M32(vt + 0x1C) : 0, ARG(0));
        }
        if (!r) { gfree(cs); create_depth--; RET(0, 12); }
        a[1] = 0x1; /* WM_CREATE */
        rt_invoke(c, wndproc, 4, a);
        gfree(cs);
        create_depth--;
    }
    user32_win_register(hwnd, wndproc, ARG(8), ARG(3), ARG(0));
    RET(hwnd, 12);
}
SHIM(DestroyWindow) { user32_win_unregister(ARG(0)); RET(1, 1); }
SHIM(ShowWindow) { RET(0, 2); }
SHIM(ReleaseCapture) { RET(1, 0); }
SHIM(SetCapture) { RET(0, 1); }
SHIM(ClipCursor) { RET(1, 1); }
SHIM(MessageBeep) { RET(1, 1); }
SHIM(SetFocus) { RET(0, 1); }
SHIM(GetFocus) { RET(0, 0); }
SHIM(PeekMessageW) { RET(0, 5); }
SHIM(DispatchMessageW) {
    uint32_t m = ARG(0);
    uint32_t r = 0;
    uint32_t proc = user32_win_classproc(M32(m));
    if (proc && M32(m + 4) != 0x12) {
        uint32_t args[4] = {M32(m), M32(m + 4), M32(m + 8), M32(m + 12)};
        r = rt_invoke(c, proc, 4, args);
    }
    RET(r, 1);
}
SHIM(PostMessageW) { RET(1, 4); }
SHIM(PostMessageA) { RET(1, 4); }
SHIM(PostThreadMessageA) { RET(1, 4); }
SHIM(SendMessageW) { RET(0, 4); }
SHIM(GetMessageA) { RET(0, 4); }
SHIM(GetMessageTime) { RET(0, 0); }
SHIM(keybd_event) { RET(0, 4); }
SHIM(SystemParametersInfoW) { RET(1, 4); }

/* ------------------------------------------------------ GDI / Massive */
SHIM(gdi_GetStockObject) { static uint32_t next = 0x7000; RET(next++, 1); }
SHIM(gdi_CreateCompatibleDC) { static uint32_t next = 0x7100; RET(next++, 1); }
SHIM(gdi_DeleteDC) { RET(1, 1); }
SHIM(gdi_DeleteObject) { RET(1, 1); }
SHIM(gdi_SelectObject) { RET(ARG(1), 2); }
SHIM(gdi_CreateCompatibleBitmap) { static uint32_t next = 0x7200; RET(next++, 3); }
SHIM(gdi_CreateDIBSection) {
    uint32_t bmi = ARG(1), bits_out = ARG(3);
    uint32_t w = bmi ? M32(bmi + 4) : 1, h = bmi ? (uint32_t)abs((int)M32(bmi + 8)) : 1;
    uint32_t bpp = bmi ? M16(bmi + 14) : 32;
    uint32_t size = w * h * (bpp ? bpp : 32) / 8;
    uint32_t buf = gmalloc(size ? size : 16);
    memset(MEM + buf, 0, size ? size : 16);
    if (bits_out) M32(bits_out) = buf;
    static uint32_t next = 0x7300;
    RET(next++, 6);
}
SHIM(gdi_StretchBlt) { RET(1, 11); }
SHIM(gdi_GetDIBits) { RET(1, 7); }
SHIM(gdi_GetDeviceCaps) { RET(32, 2); }
SHIM(gdi_GetPixel) { RET(0, 3); }
SHIM(gdi_SetPixel) { RET(0, 4); }
SHIM(gdi_Polygon) { RET(1, 3); }
SHIM(gdi_Rectangle) { RET(1, 5); }
SHIM(gdi_SetROP2) { RET(0, 2); }
SHIM(gdi_LineTo) { RET(1, 3); }
SHIM(gdi_CreatePen) { static uint32_t next = 0x7400; RET(next++, 3); }
SHIM(gdi_BitBlt) { RET(1, 9); }
SHIM(gdi_CreateSolidBrush) { static uint32_t next = 0x7500; RET(next++, 1); }
SHIM(gdi_TextOutW) { RET(1, 5); }
/* The exe calls winGetSizeANSI("MPTEST") right after appInit and exits if the
 * result is 0. A zeroed SIZE made it take the "no display" path. Report a
 * plausible 8x16 cell per character. */
static void text_extent(uint32_t n, uint32_t out) {
    if (!out) return;
    M32(out) = (n ? n : 1) * 8;
    M32(out + 4) = 16;
}
SHIM(gdi_GetTextExtentPoint32A) { text_extent(ARG(2), ARG(3)); RET(1, 4); }
SHIM(gdi_GetTextExtentPoint32W) { text_extent(ARG(2), ARG(3)); RET(1, 4); }
SHIM(gdi_CreateDIBitmap) { static uint32_t next = 0x7600; RET(next++, 6); }
SHIM(gdi_PatBlt) { RET(1, 4); }
SHIM(gdi_CreateFontW) { static uint32_t next = 0x7700; RET(next++, 14); }
SHIM(gdi_CreateFontA) { static uint32_t next = 0x7800; RET(next++, 14); }
SHIM(gdi_CreateBitmap) { static uint32_t next = 0x7900; RET(next++, 5); }
SHIM(gdi_SetDeviceGammaRamp) { RET(1, 2); }
/* Dialogs: the first one is the bitmap splash. Returning 0 makes the game take
 * its own "no splash" path and continue; the menus/dialog helpers below are
 * harmless no-ops (the game renders through D3DDrv, not GDI). */
SHIM(CreateDialogParamA) { RET(0, 5); }
SHIM(CreateDialogParamW) { RET(0, 5); }
SHIM(CreateDialogIndirectParamA) { RET(0, 5); }
SHIM(CreateDialogIndirectParamW) { RET(0, 5); }
SHIM(DialogBoxParamA) { RET(0xFFFFFFFFu, 5); }
SHIM(DialogBoxParamW) { RET(0xFFFFFFFFu, 5); }
SHIM(EndDialog) { RET(1, 2); }
SHIM(GetDlgItem) { RET(0, 2); }
SHIM(GetDlgCtrlID) { RET(0, 1); }
SHIM(SetDlgItemTextA) { RET(1, 3); }
SHIM(SetDlgItemTextW) { RET(1, 3); }
SHIM(GetDlgItemTextA) { RET(0, 3); }
SHIM(GetDlgItemTextW) { RET(0, 3); }
SHIM(CheckDlgButton) { RET(1, 3); }
SHIM(CheckRadioButton) { RET(1, 4); }
SHIM(IsDlgButtonChecked) { RET(0, 2); }
SHIM(LoadImageA) { RET(0, 6); }
SHIM(LoadImageW) { RET(0, 6); }
SHIM(LoadIconW) { RET(0, 2); }
SHIM(LoadCursorW) { RET(0, 2); }
SHIM(LoadMenuA) { RET(0, 2); }
SHIM(LoadMenuW) { RET(0, 2); }
SHIM(GetSubMenu) { RET(0, 2); }
SHIM(AppendMenuW) { RET(1, 5); }
SHIM(SetMenuItemInfoA) { RET(1, 4); }
SHIM(SetMenuItemInfoW) { RET(1, 4); }
SHIM(GetMenuItemInfoA) { RET(0, 4); }
SHIM(GetMenuItemInfoW) { RET(0, 4); }
SHIM(InsertMenuItemA) { RET(1, 4); }
SHIM(GetMenuItemCount) { RET(0, 1); }
SHIM(GetMenu) { RET(0, 1); }
SHIM(GetMenuState) { RET(0xFFFFFFFFu, 3); }
SHIM(TrackPopupMenu) { RET(0, 7); }
SHIM(CreatePopupMenu) { RET(0x00020001u, 0); }
SHIM(DestroyMenu) { RET(1, 1); }
SHIM(SetMenu) { RET(1, 2); }
SHIM(DrawMenuBar) { RET(1, 1); }
SHIM(EnableMenuItem) { RET(0, 3); }
SHIM(GetSysColor) { RET(0xC0C0C0u, 1); }
SHIM(GetSysColorBrush) { RET(0x1001, 1); }
SHIM(FillRect) { RET(1, 3); }
SHIM(FrameRect) { RET(1, 3); }
SHIM(DrawEdge) { RET(1, 4); }
SHIM(DrawFocusRect) { RET(1, 2); }
SHIM(DrawTextA) { RET(1, 7); }
SHIM(DrawTextW) { RET(1, 7); }
SHIM(DrawTextExA) { RET(1, 8); }
SHIM(DrawTextExW) { RET(1, 8); }
SHIM(IsZoomed) { RET(0, 1); }
SHIM(IsWindowEnabled) { RET(1, 1); }
/* Win32: a window does not exist yet while WM_NCCREATE/WM_CREATE run —
 * IsWindow only turns TRUE after CreateWindowEx returns. The registry
 * mirrors that (user32_win_register runs after the create messages). */
SHIM(IsWindow) { RET(user32_win_classproc(ARG(0)) ? 1u : 0u, 1); }
/* IMM32: the engine disables the Input Method Editor on its windows
 * (WinDrv.dll!ImmAssociateContext). No IME on this runtime: the context
 * handle is a fake non-null so "prev == NULL" checks behave sanely. */
static uint32_t g_fake_himc = 0x0C000001u;
SHIM(ImmAssociateContext) { RET(g_fake_himc, 3); }
SHIM(ImmGetContext) { RET(g_fake_himc, 1); }
SHIM(ImmReleaseContext) { RET(1, 2); }
SHIM(ImmSetOpenStatus) { RET(1, 2); }
SHIM(ImmGetOpenStatus) { RET(0, 1); }
SHIM(ImmSetCompositionWindow) { RET(1, 2); }
SHIM(ImmSetCompositionFontA) { RET(1, 2); }
SHIM(ImmSetCompositionFontW) { RET(1, 2); }
SHIM(ImmNotifyIME) { RET(1, 4); }
SHIM(ImmSimulateHotKey) { RET(1, 2); }
SHIM(GetParent) { RET(0, 1); }
SHIM(GetWindowTextA) { RET(0, 3); }
SHIM(InvalidateRect) { RET(1, 3); }
SHIM(ValidateRect) { RET(1, 2); }
SHIM(BringWindowToTop) { RET(1, 1); }
SHIM(SetWindowLongW) { RET((uint32_t)user32_win_setlong(ARG(0), (int32_t)ARG(1), (int32_t)ARG(2)), 3); }
SHIM(GetWindowLongW) { RET((uint32_t)user32_win_getlong(ARG(0), (int32_t)ARG(1)), 2); }
/* Subclassing path used by Window.dll's controls: CallWindowProc forwards to
 * the previous wndproc, which on this runtime is a guest function. A NULL
 * previous proc means the engine never subclassed this window: answer like
 * DefWindowProc so the caller's Win32 contract still holds. */
SHIM(CallWindowProcW) {
    if (!ARG(0)) { uint32_t msg = ARG(2); RET(msg == 0x81 ? 1u : 0u, 5); }
    uint32_t args[4] = {ARG(1), ARG(2), ARG(3), ARG(4)};
    uint32_t r = rt_invoke(c, ARG(0), 4, args);
    RET(r, 5);
}
SHIM(CallWindowProcA) {
    if (!ARG(0)) { uint32_t msg = ARG(2); RET(msg == 0x81 ? 1u : 0u, 5); }
    uint32_t args[4] = {ARG(1), ARG(2), ARG(3), ARG(4)};
    uint32_t r = rt_invoke(c, ARG(0), 4, args);
    RET(r, 5);
}
SHIM(EnableWindow) { RET(1, 2); }
SHIM(WindowFromPoint) { RET(user32_main_hwnd(), 2); }
SHIM(SetWindowPos) { RET(1, 7); }
SHIM(GetUpdateRect) { RET(0, 3); }
SHIM(AdjustWindowRectEx) { RET(1, 4); }
SHIM(BeginDeferWindowPos) { RET(0x3001, 4); }
SHIM(EndDeferWindowPos) { RET(1, 1); }
SHIM(DeferWindowPos) { RET(0x3001, 7); }
SHIM(MapWindowPoints) { RET(0, 4); }
SHIM(AttachThreadInput) { RET(1, 3); }
SHIM(SetActiveWindow) { RET(0, 1); }
SHIM(SetKeyboardState) { RET(1, 1); }
SHIM(UnregisterHotKey) { RET(1, 2); }
SHIM(GetKeyNameTextW) { RET(0, 3); }
SHIM(MapVirtualKeyW) { RET(0, 2); }
SHIM(GetClassInfoExA) { RET(0, 2); }
SHIM(EnumChildWindows) { RET(1, 3); }
SHIM(RegisterClassExW) {
    uint32_t p = ARG(0);
    char name[128]; gw2a(M32(p + 40), name, sizeof name);
    int i = class_find(name);
    LOG("RegisterClassExW '%s'", name);
    if (i < 0) { if (g_nclasses >= 64) RET(0, 1); i = g_nclasses++; }
    memset(g_classes[i].wc, 0, 48);
    memcpy(g_classes[i].wc, MEM + p, 48);
    snprintf(g_classes[i].name, sizeof g_classes[i].name, "%s", name);
    RET(0xC000 + (uint32_t)i, 1);
}
SHIM(RegisterClassW) {
    uint32_t p = ARG(0);
    char name[128]; gw2a(M32(p + 36), name, sizeof name);
    int i = class_find(name);
    if (i < 0) { if (g_nclasses >= 64) RET(0, 1); i = g_nclasses++; }
    memset(g_classes[i].wc, 0, 48);
    *(uint32_t *)(g_classes[i].wc + 0) = 48;      /* cbSize */
    memcpy(g_classes[i].wc + 4, MEM + p, 40);     /* WNDCLASS -> WNDCLASSEX */
    snprintf(g_classes[i].name, sizeof g_classes[i].name, "%s", name);
    RET(0xC000 + (uint32_t)i, 1);
}
static const char *const k_sys_classes[] = {
    "SCROLLBAR", "BUTTON", "EDIT", "STATIC", "COMBOBOX", "LISTBOX", "MDICLIENT",
    "SysTreeView32", "SysListView32", "SysHeader32", "SysTabControl32",
    "msctls_trackbar32", "msctls_progress32", "msctls_statusbar32",
    "msctls_updown32", "msctls_hotkey32", "tooltips_class32", "#32770",
    "SysDateTimePick32", "SysMonthCal32",
    /* comctl32 common controls the engine queries for its UI */
    "RICHEDIT", "RICHEDIT20A", "RICHEDIT20W", "RICHEDIT50W",
    "ToolbarWindow32", "ReBarWindow32", "SysAnimate32", "SysPager",
    "SysIPAddress32", "ComboBoxEx32", "SysLink", "SysDragList32",
    "SysTrackbar32", "SysProgress32", NULL};
static int sys_class_p(const char *n) {
    for (int i = 0; k_sys_classes[i]; i++)
        if (!strcasecmp(n, k_sys_classes[i])) return 1;
    return 0;
}
SHIM(GetClassInfoExW) {
    char name[128]; gw2a(ARG(1), name, sizeof name);
    int i = class_find(name);
    LOG("GetClassInfoExW '%s' reg=%d sys=%d", name, i, sys_class_p(name));
    if (i >= 0) {
        memcpy(MEM + ARG(2), g_classes[i].wc, 48);
        RET(1, 3);
    }
    if (sys_class_p(name)) {
        /* Standard controls exist even though we do not register them. */
        uint32_t out = ARG(2);
        memset(MEM + out, 0, 48);
        M32(out + 0) = 48;                                  /* cbSize */
        M32(out + 8) = rt_import("USER32.dll", "DefWindowProcA"); /* lpfnWndProc */
        RET(1, 3);
    }
    RET(0, 3);
}
SHIM(GetClassInfoW) {
    char name[128]; gw2a(ARG(1), name, sizeof name);
    int i = class_find(name);
    if (i < 0) RET(0, 3);
    memcpy(MEM + ARG(2), g_classes[i].wc + 4, 40);
    RET(1, 3);
}
SHIM(UnregisterClassW) {
    char name[128]; gw2a(ARG(0), name, sizeof name);
    int i = class_find(name);
    if (i >= 0) { g_classes[i] = g_classes[--g_nclasses]; }
    RET(1, 2);
}
SHIM(DefWindowProcW) {
    uint32_t msg = ARG(1);
    if (msg == 0x81 /* WM_NCCREATE */) RET(1, 4);
    RET(0, 4);
}
SHIM(comctl_ord17) { RET(1, 1); }        /* InitCommonControlsEx / _TrackMouseEvent */
SHIM(comctl_trackmouse) { RET(1, 1); }
SHIM(comctl_init) { RET(0, 0); }
SHIM(FindWindowA) { RET(0, 2); }
SHIM(FindWindowW) { RET(0, 2); }
SHIM(FindWindowExA) { RET(0, 4); }
SHIM(FindWindowExW) { RET(0, 4); }
SHIM(GetPropA) { RET(0, 2); }
SHIM(GetPropW) { RET(0, 2); }
SHIM(SetPropA) { RET(1, 3); }
SHIM(SetPropW) { RET(1, 3); }
SHIM(RemovePropA) { RET(0, 2); }
SHIM(RemovePropW) { RET(0, 2); }
SHIM(SetParent) { RET(0, 2); }
SHIM(LocalFileTimeToFileTime) {
    uint32_t a = ARG(0), b = ARG(1);
    if (a && b) { M32(b) = M32(a); M32(b + 4) = M32(a + 4); }
    RET(1, 2);
}
SHIM(DirectDrawCreate) {
    if (ARG(2)) M32(ARG(2)) = 0;
    LOG("DirectDrawCreate -> DDERR_GENERIC (renderer is D3D9)");
    RET(0x80004005u, 3);
}
SHIM(GetSaveFileNameA) { RET(0, 1); }
SHIM(GetOpenFileNameA) { RET(0, 1); }
SHIM(ChooseColorA) { RET(0, 1); }
SHIM(PrintDlgA) { RET(0, 1); }
SHIM(FindTextA) { RET(0, 1); }
SHIM(ad_Initialize) { LOG("massive: Initialize -> NULL (service is dead)"); RETC(0); }
SHIM(ad_Shutdown) { RETC(0); }
SHIM(ad_EnterZone) { RETC(0); }
SHIM(ad_Update) { RETC(0); }
SHIM(ad_sub_ctor) { RETC(ARG(0)); }
SHIM(ad_sub_dtor) { RETC(0); }
SHIM(ad_sub_SetImpression) { RETC(0); }

/* ------------------------------------------------------------ tables */
#define S2(dll, name, fn) {dll, name, fn},
static const ShimDef crt_defs[] = {
    {"msvcr71.dll", "wcslen", shim_wcslen},
    {"msvcr71.dll", "wcschr", shim_wcschr},
    {"msvcr71.dll", "wcsrchr", shim_wcsrchr},
    {"msvcr71.dll", "wcsstr", shim_wcsstr},
    {"msvcr71.dll", "wcspbrk", shim_wcspbrk},
    {"msvcr71.dll", "wcscpy", shim_wcscpy},
    {"msvcr71.dll", "wcsncpy", shim_wcsncpy},
    {"msvcr71.dll", "wcscat", shim_wcscat},
    {"msvcr71.dll", "wcscmp", shim_wcscmp},
    {"msvcr71.dll", "wcsncmp", shim_wcsncmp},
    {"msvcr71.dll", "_wcsicmp", shim__wcsicmp},
    {"msvcr71.dll", "_wcsnicmp", shim__wcsnicmp},
    {"msvcr71.dll", "_wcsupr", shim__wcsupr},
    {"msvcr71.dll", "_wcslwr", shim__wcslwr},
    {"msvcr71.dll", "_wtoi", shim__wtoi},
    {"msvcr71.dll", "wcstoul", shim_wcstoul},
    {"msvcr71.dll", "_itow", shim__itow},
    {"msvcr71.dll", "_wstrdate", shim__wstrdate},
    {"msvcr71.dll", "_wstrtime", shim__wstrtime},
    {"msvcr71.dll", "swprintf", shim_swprintf},
    {"msvcr71.dll", "_snwprintf", shim__snwprintf},
    {"msvcr71.dll", "swscanf", shim_swscanf},
    {"msvcr71.dll", "memchr", shim_memchr},
    {"msvcr71.dll", "islower", shim_islower},
    {"msvcr71.dll", "isupper", shim_isupper},
    {"msvcr71.dll", "strpbrk", shim_strpbrk},
    {"msvcr71.dll", "strtol", shim_strtol},
    {"msvcr71.dll", "strtoul", shim_strtoul},
    {"msvcr71.dll", "strncat", shim_strncat},
    {"msvcr71.dll", "strncpy", shim_strncpy},
    {"msvcr71.dll", "strcspn", shim_strcspn},
    {"msvcr71.dll", "_strlwr", shim__strlwr},
    {"msvcr71.dll", "_strupr", shim__strupr},
    {"msvcr71.dll", "_atoi64", shim__atoi64},
    {"msvcr71.dll", "sprintf", shim_sprintf},
    {"msvcr71.dll", "vsprintf", shim_vsprintf},
    {"msvcr71.dll", "_aligned_malloc", shim__aligned_malloc},
    {"msvcr71.dll", "_aligned_free", shim__aligned_free},
    {"msvcr71.dll", "__set_app_type", shim_crt_set_app_type},
    {"msvcr71.dll", "__p__commode", shim_crt_p_commode},
    {"msvcr71.dll", "__p__fmode", shim_crt_p_fmode},
    {"msvcr71.dll", "__setusermatherr", shim_crt_setusermatherr},
    {"msvcr71.dll", "__getmainargs", shim_crt_getmainargs},
    {"msvcr71.dll", "_cexit", shim_crt_cexit},
    {"msvcr71.dll", "_c_exit", shim_crt_c_exit},
    {"msvcr71.dll", "_exit", shim_crt_exit},
    {"msvcr71.dll", "_ismbblead", shim_crt_ismbblead},
    {"msvcr71.dll", "_adjust_fdiv", shim_crt_adjust_fdiv},
    {"msvcr71.dll", "_except_handler3", shim_crt_except_handler3},
    {"msvcr71.dll", "__CppXcptFilter", shim_crt_cpp_xcpt_filter},
    {"msvcr71.dll", "time", shim_crt_time},
    {"msvcr71.dll", "gmtime", shim_crt_gmtime},
    {"msvcr71.dll", "localtime", shim_crt_localtime},
    {"msvcr71.dll", "mktime", shim_crt_mktime},
    {"msvcr71.dll", "difftime", shim_crt_difftime},
    {"msvcr71.dll", "clock", shim_crt_clock},
    {"msvcr71.dll", "acos", shim_m_acos},
    {"msvcr71.dll", "asin", shim_m_asin},
    {"msvcr71.dll", "atan", shim_m_atan},
    {"msvcr71.dll", "fabs", shim_m_fabs},
    {"msvcr71.dll", "log", shim_m_log},
    {"msvcr71.dll", "log10", shim_m_log10},
    {"msvcr71.dll", "sin", shim_m_sin},
    {"msvcr71.dll", "cos", shim_m_cos},
    {"msvcr71.dll", "tan", shim_m_tan},
    {"msvcr71.dll", "sqrt", shim_m_sqrt},
    {"msvcr71.dll", "exp", shim_m_exp},
    {"msvcr71.dll", "ceil", shim_m_ceil},
    {"msvcr71.dll", "floor", shim_m_floor},
    {"msvcr71.dll", "pow", shim_m_pow},
    {"msvcr71.dll", "fmod", shim_m_fmod},
    {NULL, NULL, NULL}
};
static const ShimDef cpp_defs[] = {
    {"MSVCP71.dll", "??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@PBD@Z", shim_cpp_str_ctor_cstr},
    {"MSVCP71.dll", "??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@ABV01@@Z", shim_cpp_str_ctor_copy},
    {"MSVCP71.dll", "??1?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@XZ", shim_cpp_str_dtor},
    {"MSVCP71.dll", "?c_str@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEPBDXZ", shim_cpp_str_c_str},
    {NULL, NULL, NULL}
};
static const ShimDef defs[] = {
    {"KERNEL32.dll", "GetFileAttributesW", shim_GetFileAttributesW},
    {"KERNEL32.dll", "GetFileAttributesExW", shim_GetFileAttributesExW},
    {"KERNEL32.dll", "FindFirstFileW", shim_FindFirstFileW},
    {"KERNEL32.dll", "FindNextFileW", shim_FindNextFileW},
    {"KERNEL32.dll", "GetCurrentDirectoryW", shim_GetCurrentDirectoryW},
    {"KERNEL32.dll", "GetCurrentDirectoryA", shim_GetCurrentDirectoryA},
    {"KERNEL32.dll", "GetSystemDirectoryW", shim_GetSystemDirectoryW},
    {"KERNEL32.dll", "GetWindowsDirectoryW", shim_GetWindowsDirectoryW},
    {"KERNEL32.dll", "CreateDirectoryW", shim_CreateDirectoryW},
    {"KERNEL32.dll", "RemoveDirectoryW", shim_RemoveDirectoryW},
    {"KERNEL32.dll", "SetFileAttributesW", shim_SetFileAttributesW},
    {"KERNEL32.dll", "GetDriveTypeW", shim_GetDriveTypeW},
    {"KERNEL32.dll", "GetDriveTypeA", shim_GetDriveTypeA},
    {"KERNEL32.dll", "GetDiskFreeSpaceExW", shim_GetDiskFreeSpaceExW},
    {"KERNEL32.dll", "GetVolumeInformationA", shim_GetVolumeInformationA},
    {"KERNEL32.dll", "GetLocalTime", shim_GetLocalTime},
    {"KERNEL32.dll", "VirtualQuery", shim_VirtualQuery},
    {"KERNEL32.dll", "VirtualProtect", shim_VirtualProtect},
    {"KERNEL32.dll", "GlobalAlloc", shim_GlobalAlloc},
    {"KERNEL32.dll", "GlobalLock", shim_GlobalLock},
    {"KERNEL32.dll", "GlobalUnlock", shim_GlobalUnlock},
    {"KERNEL32.dll", "GetProcessWorkingSetSize", shim_GetProcessWorkingSetSize},
    {"KERNEL32.dll", "GetVersionExW", shim_GetVersionExW},
    {"KERNEL32.dll", "GetComputerNameA", shim_GetComputerNameA},
    {"KERNEL32.dll", "GetComputerNameW", shim_GetComputerNameW},
    {"ADVAPI32.dll", "GetUserNameA", shim_GetUserNameA},
    {"ADVAPI32.dll", "GetUserNameW", shim_GetUserNameW},
    {"KERNEL32.dll", "GetUserNameA", shim_GetUserNameA},
    {"KERNEL32.dll", "GetUserNameW", shim_GetUserNameW},
    {"KERNEL32.dll", "MulDiv", shim_MulDiv},
    {"KERNEL32.dll", "lstrlenA", shim_lstrlenA},
    {"KERNEL32.dll", "lstrcmpiW", shim_lstrcmpiW},
    {"KERNEL32.dll", "GetCommandLineW", shim_GetCommandLineW},
    {"KERNEL32.dll", "GetEnvironmentVariableA", shim_GetEnvironmentVariableA},
    {"KERNEL32.dll", "CopyFileW", shim_CopyFileW},
    {"KERNEL32.dll", "CreateEventW", shim_CreateEventW},
    {"KERNEL32.dll", "CreateMutexW", shim_CreateMutexW},
    {"KERNEL32.dll", "OpenEventW", shim_OpenEventW},
    {"KERNEL32.dll", "WaitForSingleObjectEx", shim_WaitForSingleObjectEx},
    {"KERNEL32.dll", "SetHandleInformation", shim_SetHandleInformation},
    {"KERNEL32.dll", "SetConsoleMode", shim_SetConsoleMode},
    {"KERNEL32.dll", "LockFileEx", shim_LockFileEx},
    {"KERNEL32.dll", "UnlockFileEx", shim_UnlockFileEx},
    {"KERNEL32.dll", "GetThreadContext", shim_GetThreadContext},
    {"KERNEL32.dll", "FormatMessageW", shim_FormatMessageW},
    {"KERNEL32.dll", "TerminateThread", shim_TerminateThread},
    
    {"ole32.dll", "CoCreateGuid", shim_CoCreateGuid},
    {"ole32.dll", "CoTaskMemFree", shim_CoTaskMemFree},
    {"ole32.dll", "CLSIDFromString", shim_CLSIDFromString},
    {"ole32.dll", "StringFromCLSID", shim_StringFromCLSID},
    {"ole32.dll", "IsEqualGUID", shim_IsEqualGUID},
    {"OLEAUT32.dll", "#8", shim_VariantInit},
    {"OLEAUT32.dll", "#9", shim_VariantClear},
    {"OLEAUT32.dll", "VariantInit", shim_VariantInit},
    {"OLEAUT32.dll", "VariantClear", shim_VariantClear},
    {"VERSION.dll", "GetFileVersionInfoSizeA", shim_GetFileVersionInfoSizeA},
    {"VERSION.dll", "GetFileVersionInfoA", shim_GetFileVersionInfoA},
    {"VERSION.dll", "VerQueryValueA", shim_VerQueryValueA},
    {"ADVAPI32.dll", "CryptAcquireContextA", shim_CryptAcquireContextA},
    {"ADVAPI32.dll", "CryptGenRandom", shim_CryptGenRandom},
    {"ADVAPI32.dll", "CryptReleaseContext", shim_CryptReleaseContext},
    {"ADVAPI32.dll", "RegCreateKeyExA", shim_RegCreateKeyExA},
    {"ADVAPI32.dll", "RegDeleteKeyA", shim_RegDeleteKeyA},
    {"ADVAPI32.dll", "RegDeleteValueA", shim_RegDeleteValueA},
    {"USER32.dll", "wsprintfA", shim_wsprintfA},
    {"USER32.dll", "SetTimer", shim_SetTimer},
    {"USER32.dll", "KillTimer", shim_KillTimer},
    {"USER32.dll", "OpenClipboard", shim_OpenClipboard},
    {"USER32.dll", "EmptyClipboard", shim_EmptyClipboard},
    {"USER32.dll", "SetClipboardData", shim_SetClipboardData},
    {"USER32.dll", "GetClipboardData", shim_GetClipboardData},
    {"USER32.dll", "CloseClipboard", shim_CloseClipboard},
    {"USER32.dll", "GetKeyState", shim_GetKeyState},
    {"USER32.dll", "GetAsyncKeyState", shim_GetAsyncKeyState},
    {"USER32.dll", "RegisterWindowMessageA", shim_RegisterWindowMessageA},
    {"USER32.dll", "RegisterWindowMessageW", shim_RegisterWindowMessageW},
    {"USER32.dll", "DefWindowProcW", shim_DefWindowProcW},
    {"USER32.dll", "RegisterClassExW", shim_RegisterClassExW},
    {"USER32.dll", "CreateWindowExW", shim_CreateWindowExW},
    {"USER32.dll", "DestroyWindow", shim_DestroyWindow},
    {"USER32.dll", "ShowWindow", shim_ShowWindow},
    {"USER32.dll", "ReleaseCapture", shim_ReleaseCapture},
    {"USER32.dll", "SetCapture", shim_SetCapture},
    {"USER32.dll", "ClipCursor", shim_ClipCursor},
    {"USER32.dll", "MessageBeep", shim_MessageBeep},
    {"USER32.dll", "SetFocus", shim_SetFocus},
    {"USER32.dll", "GetFocus", shim_GetFocus},
    {"USER32.dll", "PeekMessageW", shim_PeekMessageW},
    {"USER32.dll", "DispatchMessageW", shim_DispatchMessageW},
    {"USER32.dll", "PostMessageW", shim_PostMessageW},
    {"USER32.dll", "PostMessageA", shim_PostMessageA},
    {"USER32.dll", "PostThreadMessageA", shim_PostThreadMessageA},
    {"USER32.dll", "SendMessageW", shim_SendMessageW},
    {"USER32.dll", "GetMessageA", shim_GetMessageA},
    {"USER32.dll", "GetMessageTime", shim_GetMessageTime},
    {"USER32.dll", "keybd_event", shim_keybd_event},
    {"USER32.dll", "SystemParametersInfoW", shim_SystemParametersInfoW},
    {"USER32.dll", "FindWindowA", shim_FindWindowA},
    {"USER32.dll", "FindWindowW", shim_FindWindowW},
    {"USER32.dll", "FindWindowExA", shim_FindWindowExA},
    {"USER32.dll", "FindWindowExW", shim_FindWindowExW},
    {"USER32.dll", "GetPropA", shim_GetPropA},
    {"USER32.dll", "GetPropW", shim_GetPropW},
    {"USER32.dll", "SetPropA", shim_SetPropA},
    {"USER32.dll", "SetPropW", shim_SetPropW},
    {"USER32.dll", "RemovePropA", shim_RemovePropA},
    {"USER32.dll", "RemovePropW", shim_RemovePropW},
    {"USER32.dll", "SetParent", shim_SetParent},
    {"USER32.dll", "RegisterClassExW", shim_RegisterClassExW},
    {"USER32.dll", "RegisterClassW", shim_RegisterClassW},
    {"USER32.dll", "GetClassInfoExW", shim_GetClassInfoExW},
    {"USER32.dll", "GetClassInfoW", shim_GetClassInfoW},
    {"USER32.dll", "UnregisterClassW", shim_UnregisterClassW},
    {"COMCTL32.dll", "#17", shim_comctl_ord17},
    {"COMCTL32.dll", "_TrackMouseEvent", shim_comctl_trackmouse},
    {"COMCTL32.dll", "InitCommonControls", shim_comctl_init},
    {"USER32.dll", "CreateDialogParamA", shim_CreateDialogParamA},
    {"USER32.dll", "CreateDialogParamW", shim_CreateDialogParamW},
    {"USER32.dll", "CreateDialogIndirectParamA", shim_CreateDialogIndirectParamA},
    {"USER32.dll", "CreateDialogIndirectParamW", shim_CreateDialogIndirectParamW},
    {"USER32.dll", "DialogBoxParamA", shim_DialogBoxParamA},
    {"USER32.dll", "DialogBoxParamW", shim_DialogBoxParamW},
    {"USER32.dll", "EndDialog", shim_EndDialog},
    {"USER32.dll", "GetDlgItem", shim_GetDlgItem},
    {"USER32.dll", "GetDlgCtrlID", shim_GetDlgCtrlID},
    {"USER32.dll", "SetDlgItemTextA", shim_SetDlgItemTextA},
    {"USER32.dll", "SetDlgItemTextW", shim_SetDlgItemTextW},
    {"USER32.dll", "GetDlgItemTextA", shim_GetDlgItemTextA},
    {"USER32.dll", "GetDlgItemTextW", shim_GetDlgItemTextW},
    {"USER32.dll", "CheckDlgButton", shim_CheckDlgButton},
    {"USER32.dll", "CheckRadioButton", shim_CheckRadioButton},
    {"USER32.dll", "IsDlgButtonChecked", shim_IsDlgButtonChecked},
    {"USER32.dll", "LoadImageA", shim_LoadImageA},
    {"USER32.dll", "LoadImageW", shim_LoadImageW},
    {"USER32.dll", "LoadIconW", shim_LoadIconW},
    {"USER32.dll", "LoadCursorW", shim_LoadCursorW},
    {"USER32.dll", "LoadMenuA", shim_LoadMenuA},
    {"USER32.dll", "LoadMenuW", shim_LoadMenuW},
    {"USER32.dll", "GetSubMenu", shim_GetSubMenu},
    {"USER32.dll", "AppendMenuW", shim_AppendMenuW},
    {"USER32.dll", "SetMenuItemInfoA", shim_SetMenuItemInfoA},
    {"USER32.dll", "SetMenuItemInfoW", shim_SetMenuItemInfoW},
    {"USER32.dll", "GetMenuItemInfoA", shim_GetMenuItemInfoA},
    {"USER32.dll", "GetMenuItemInfoW", shim_GetMenuItemInfoW},
    {"USER32.dll", "InsertMenuItemA", shim_InsertMenuItemA},
    {"USER32.dll", "GetMenuItemCount", shim_GetMenuItemCount},
    {"USER32.dll", "GetMenu", shim_GetMenu},
    {"USER32.dll", "GetMenuState", shim_GetMenuState},
    {"USER32.dll", "TrackPopupMenu", shim_TrackPopupMenu},
    {"USER32.dll", "CreatePopupMenu", shim_CreatePopupMenu},
    {"USER32.dll", "DestroyMenu", shim_DestroyMenu},
    {"USER32.dll", "SetMenu", shim_SetMenu},
    {"USER32.dll", "DrawMenuBar", shim_DrawMenuBar},
    {"USER32.dll", "EnableMenuItem", shim_EnableMenuItem},
    {"USER32.dll", "GetSysColor", shim_GetSysColor},
    {"USER32.dll", "GetSysColorBrush", shim_GetSysColorBrush},
    {"USER32.dll", "FillRect", shim_FillRect},
    {"USER32.dll", "FrameRect", shim_FrameRect},
    {"USER32.dll", "DrawEdge", shim_DrawEdge},
    {"USER32.dll", "DrawFocusRect", shim_DrawFocusRect},
    {"USER32.dll", "DrawTextA", shim_DrawTextA},
    {"USER32.dll", "DrawTextW", shim_DrawTextW},
    {"USER32.dll", "DrawTextExA", shim_DrawTextExA},
    {"USER32.dll", "DrawTextExW", shim_DrawTextExW},
    {"USER32.dll", "IsZoomed", shim_IsZoomed},
    {"USER32.dll", "IsWindowEnabled", shim_IsWindowEnabled},
    {"USER32.dll", "IsWindow", shim_IsWindow},
    {"IMM32.dll", "ImmAssociateContext", shim_ImmAssociateContext},
    {"IMM32.dll", "ImmGetContext", shim_ImmGetContext},
    {"IMM32.dll", "ImmReleaseContext", shim_ImmReleaseContext},
    {"IMM32.dll", "ImmSetOpenStatus", shim_ImmSetOpenStatus},
    {"IMM32.dll", "ImmGetOpenStatus", shim_ImmGetOpenStatus},
    {"IMM32.dll", "ImmSetCompositionWindow", shim_ImmSetCompositionWindow},
    {"IMM32.dll", "ImmSetCompositionFontA", shim_ImmSetCompositionFontA},
    {"IMM32.dll", "ImmSetCompositionFontW", shim_ImmSetCompositionFontW},
    {"IMM32.dll", "ImmNotifyIME", shim_ImmNotifyIME},
    {"IMM32.dll", "ImmSimulateHotKey", shim_ImmSimulateHotKey},
    {"USER32.dll", "GetParent", shim_GetParent},
    {"USER32.dll", "GetWindowTextA", shim_GetWindowTextA},
    {"USER32.dll", "InvalidateRect", shim_InvalidateRect},
    {"USER32.dll", "ValidateRect", shim_ValidateRect},
    {"USER32.dll", "BringWindowToTop", shim_BringWindowToTop},
    {"USER32.dll", "SetWindowLongW", shim_SetWindowLongW},
    {"USER32.dll", "GetWindowLongW", shim_GetWindowLongW},
    {"USER32.dll", "CallWindowProcW", shim_CallWindowProcW},
    {"USER32.dll", "CallWindowProcA", shim_CallWindowProcA},
    {"USER32.dll", "EnableWindow", shim_EnableWindow},
    {"USER32.dll", "WindowFromPoint", shim_WindowFromPoint},
    {"USER32.dll", "SetWindowPos", shim_SetWindowPos},
    {"USER32.dll", "GetUpdateRect", shim_GetUpdateRect},
    {"USER32.dll", "AdjustWindowRectEx", shim_AdjustWindowRectEx},
    {"USER32.dll", "BeginDeferWindowPos", shim_BeginDeferWindowPos},
    {"USER32.dll", "EndDeferWindowPos", shim_EndDeferWindowPos},
    {"USER32.dll", "DeferWindowPos", shim_DeferWindowPos},
    {"USER32.dll", "MapWindowPoints", shim_MapWindowPoints},
    {"USER32.dll", "AttachThreadInput", shim_AttachThreadInput},
    {"USER32.dll", "SetActiveWindow", shim_SetActiveWindow},
    {"USER32.dll", "SetKeyboardState", shim_SetKeyboardState},
    {"USER32.dll", "UnregisterHotKey", shim_UnregisterHotKey},
    {"USER32.dll", "GetKeyNameTextW", shim_GetKeyNameTextW},
    {"USER32.dll", "MapVirtualKeyW", shim_MapVirtualKeyW},
    {"USER32.dll", "GetClassInfoExA", shim_GetClassInfoExA},
    {"USER32.dll", "GetClassInfoExW", shim_GetClassInfoExW},
    {"USER32.dll", "EnumChildWindows", shim_EnumChildWindows},
    {"KERNEL32.dll", "LocalFileTimeToFileTime", shim_LocalFileTimeToFileTime},
    {"DDRAW.dll", "DirectDrawCreate", shim_DirectDrawCreate},
    {"comdlg32.dll", "GetSaveFileNameA", shim_GetSaveFileNameA},
    {"comdlg32.dll", "GetOpenFileNameA", shim_GetOpenFileNameA},
    {"comdlg32.dll", "ChooseColorA", shim_ChooseColorA},
    {"comdlg32.dll", "PrintDlgA", shim_PrintDlgA},
    {"comdlg32.dll", "FindTextA", shim_FindTextA},
    {"gdi32.dll", "GetStockObject", shim_gdi_GetStockObject},
    {"gdi32.dll", "CreateCompatibleDC", shim_gdi_CreateCompatibleDC},
    {"gdi32.dll", "DeleteDC", shim_gdi_DeleteDC},
    {"gdi32.dll", "DeleteObject", shim_gdi_DeleteObject},
    {"gdi32.dll", "SelectObject", shim_gdi_SelectObject},
    {"gdi32.dll", "CreateCompatibleBitmap", shim_gdi_CreateCompatibleBitmap},
    {"gdi32.dll", "CreateDIBSection", shim_gdi_CreateDIBSection},
    {"gdi32.dll", "StretchBlt", shim_gdi_StretchBlt},
    {"gdi32.dll", "GetDIBits", shim_gdi_GetDIBits},
    {"gdi32.dll", "GetDeviceCaps", shim_gdi_GetDeviceCaps},
    {"gdi32.dll", "GetPixel", shim_gdi_GetPixel},
    {"gdi32.dll", "SetPixel", shim_gdi_SetPixel},
    {"gdi32.dll", "Polygon", shim_gdi_Polygon},
    {"gdi32.dll", "Rectangle", shim_gdi_Rectangle},
    {"gdi32.dll", "SetROP2", shim_gdi_SetROP2},
    {"gdi32.dll", "LineTo", shim_gdi_LineTo},
    {"gdi32.dll", "CreatePen", shim_gdi_CreatePen},
    {"gdi32.dll", "BitBlt", shim_gdi_BitBlt},
    {"gdi32.dll", "CreateSolidBrush", shim_gdi_CreateSolidBrush},
    {"gdi32.dll", "TextOutW", shim_gdi_TextOutW},
    {"gdi32.dll", "GetTextExtentPoint32A", shim_gdi_GetTextExtentPoint32A},
    {"gdi32.dll", "GetTextExtentPoint32W", shim_gdi_GetTextExtentPoint32W},
    {"gdi32.dll", "CreateDIBitmap", shim_gdi_CreateDIBitmap},
    {"gdi32.dll", "PatBlt", shim_gdi_PatBlt},
    {"gdi32.dll", "CreateFontW", shim_gdi_CreateFontW},
    {"gdi32.dll", "CreateFontA", shim_gdi_CreateFontA},
    {"gdi32.dll", "CreateBitmap", shim_gdi_CreateBitmap},
    {"gdi32.dll", "SetDeviceGammaRamp", shim_gdi_SetDeviceGammaRamp},
    {"xmassiveadclientdyn.dll", "?Initialize@XMassiveAdClient@MassiveAdClient3@@SAPAV12@PBU__MAD_MASSIVE_INIT_STRUCT@2@@Z", shim_ad_Initialize},
    {"xmassiveadclientdyn.dll", "?Shutdown@XMassiveAdClient@MassiveAdClient3@@SAKHI@Z", shim_ad_Shutdown},
    {"xmassiveadclientdyn.dll", "?EnterZone@XMassiveAdClient@MassiveAdClient3@@SAKPBD@Z", shim_ad_EnterZone},
    {"xmassiveadclientdyn.dll", "?Update@XMassiveAdClient@MassiveAdClient3@@SAKH@Z", shim_ad_Update},
    {"xmassiveadclientdyn.dll", "??0XMassiveAdClientSubscriber@MassiveAdClient3@@QAE@PBD@Z", shim_ad_sub_ctor},
    {"xmassiveadclientdyn.dll", "??1XMassiveAdClientSubscriber@MassiveAdClient3@@QAE@XZ", shim_ad_sub_dtor},
    {"xmassiveadclientdyn.dll", "?SetImpression@XMassiveAdClientSubscriber@MassiveAdClient3@@QAEXPAU_MAD_Impression@2@@Z", shim_ad_sub_SetImpression},
    {NULL, NULL, NULL}
};

/* ---- GMalloc foreign-pointer guard ----
 *
 * The game's custom allocator is a static Swat4.exe object (0x1092ABE8,
 * vtable 0x1091CAA0, wired into Core.dll's GMalloc global at 0x10F53A0).
 * It tracks its blocks per 16 MiB region: bucket[ptr>>24] at this+0x25C
 * holds a per-region descriptor table, and both Realloc (f_10903E00,
 * vtbl[1]) and Free (f_109041A0, vtbl[2]) dereference the table WITHOUT
 * checking for an empty region. Phone log polog (2026-10-31): during the
 * swat_splashscene GUI class-default serialization an FString whose data
 * aliases a STATIC Engine.dll buffer (0x18C8AB0) was reassigned/released;
 * Free(0x18C8AB0) computed region 0x01, found bucket[1]==0 and walked into
 * the protected null page -> SIGSEGV reading guest 0x1190.
 *
 * On Windows the same call pattern would crash too, so the upstream state
 * diverges (an empty-string assign reaches this path in the port). Rather
 * than chase every divergence, make the allocator itself tolerant: wrap
 * vtbl[1]/vtbl[2] and emulate the untracked-pointer cases.
 *   Free(untracked)        -> no-op (releases nothing, state stays sane)
 *   Realloc(untracked, 0)  -> 0 (the caller stores Data=0: clean empty)
 *   Realloc(untracked, n)  -> fresh block via the original Malloc path;
 *                             the known callers overwrite the full buffer
 *                             immediately after, so skipping the copy is
 *                             safe for them.
 * Tracked pointers take the original code unchanged. */
#define GMALLOC_VTBL 0x1091CAA0u
#define GMALLOC_EXE_LO 0x10900000u
#define GMALLOC_EXE_HI 0x10A50000u

static uint32_t g_gm_orig_realloc, g_gm_orig_free;
static uint32_t g_gm_guard_hits;

static int gmalloc_foreign(uint32_t self, uint32_t p) {
    if (p < 0x10000u) return 0; /* null/tiny: keep original semantics */
    if ((p >= 0x01000000u && p < 0x04000000u) || /* recompiled module images */
        (p >= GMALLOC_EXE_LO && p < GMALLOC_EXE_HI) || /* exe image */
        (p >= 0x1A000000u && p < 0x1CA00000u))  /* stacks/TEB/PEB */
        return 1;
    if (p >= 0x20000000u && p < 0x40000000u) return 1; /* runtime shim heap */
    /* everything else: foreign unless the allocator owns that region */
    return M32(self + (p >> 24) * 4 + 0x25Cu) == 0u;
}

static void gmalloc_note(const char *what, uint32_t ptr, uint32_t size) {
    uint32_t hit = __sync_fetch_and_add(&g_gm_guard_hits, 1u);
    if (hit < 8 || hit % 1000 == 0)
        LOG("gmalloc guard: %s ptr=%08x size=%u (untracked region %02x, hit #%u)",
            what, ptr, size, ptr >> 24, hit + 1);
}

SHIM(gmalloc_realloc_wrap) {
    uint32_t self = c->r[1];
    uint32_t ptr = ARG(0), size = ARG(1), tag = ARG(2);
    if (gmalloc_foreign(self, ptr)) {
        gmalloc_note("foreign realloc", ptr, size);
        if (size == 0) RET(0, 3);
        uint32_t args[3] = {0, size, tag}; /* ptr=0 -> original Malloc path */
        uint32_t r = rt_invoke(c, g_gm_orig_realloc, 3, args);
        RET(r, 3);
    }
    uint32_t args[3] = {ptr, size, tag};
    uint32_t r = rt_invoke(c, g_gm_orig_realloc, 3, args);
    RET(r, 3);
}

SHIM(gmalloc_free_wrap) {
    uint32_t self = c->r[1];
    uint32_t ptr = ARG(0);
    if (gmalloc_foreign(self, ptr)) {
        gmalloc_note("foreign free", ptr, 0);
        RET(0, 1);
    }
    uint32_t args[1] = {ptr};
    rt_invoke(c, g_gm_orig_free, 1, args);
    RET(0, 1);
}

/* Called from main() right after the exe image is mapped and before any
 * guest code runs: the ctor only writes the vtable POINTER into the object
 * (M32[0x1092ABE8] = 0x1091CAA0), so patching the vtable contents early
 * survives. If the table does not look like the allocator's, skip quietly. */
void swat4_gmalloc_guard_install(void) {
    uint32_t vt = GMALLOC_VTBL;
    uint32_t m0 = M32(vt), m1 = M32(vt + 4), m2 = M32(vt + 8);
    if (m0 < GMALLOC_EXE_LO || m0 >= GMALLOC_EXE_HI ||
        m1 < GMALLOC_EXE_LO || m1 >= GMALLOC_EXE_HI ||
        m2 < GMALLOC_EXE_LO || m2 >= GMALLOC_EXE_HI) {
        LOG("gmalloc guard: vtable at %08x unexpected (%08x %08x %08x), not patched",
            vt, m0, m1, m2);
        return;
    }
    g_gm_orig_realloc = m1;
    g_gm_orig_free = m2;
    M32(vt + 4) = rt_thunk(shim_gmalloc_realloc_wrap, "GMalloc.Realloc");
    M32(vt + 8) = rt_thunk(shim_gmalloc_free_wrap, "GMalloc.Free");
    LOG("gmalloc guard: Realloc %08x / Free %08x wrapped (orig %08x / %08x)",
        M32(vt + 4), M32(vt + 8), g_gm_orig_realloc, g_gm_orig_free);
}

void swat4_shims_init(void) {
    static uint32_t cmd_cell;
    rt_register_shims(crt_defs);
    rt_register_shims_as("msvcr80.dll", crt_defs);
    rt_register_shims_as("msvcrt.dll", crt_defs);
    rt_register_shims(cpp_defs);
    rt_register_shims(defs);
    /* _acmdln is a data import: the game reads the pointer stored in the cell. */
    if (!cmd_cell) {
        char b[512];
        snprintf(b, sizeof b, "\"%s\\%s\"", GAME_WIN_DIR, GAME_EXE);
        uint32_t s = gstrdup(b);
        cmd_cell = gmalloc(4);
        M32(cmd_cell) = s;
    }
    rt_register_data("msvcr71.dll", "_acmdln", cmd_cell);
    rt_register_data("msvcr80.dll", "_acmdln", cmd_cell);
}