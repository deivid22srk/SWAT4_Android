/* Host entry: maps guest memory, loads the SWAT 4 modules from the user's game
 * folder and runs the recompiled entry point. Nothing from the game is embedded
 * in the build; the files are read at runtime. See docs/RECON.md for the module
 * map and the supported build. */
#define _GNU_SOURCE
#include "game.h"
#include "rt.h"
#include "rt_table.h"
#include <stdarg.h>
#include <sys/mman.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <sys/stat.h>
#ifdef __ANDROID__
#include <SDL2/SDL.h>
#endif

uint8_t *MEM;
int g_log_level = 1;
char g_game_dir[1024] = ".";
char g_save_dir[1024] = "./save";
uint32_t g_image_size;
uint32_t g_entry;

void rt_fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "FATAL: ");
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    fflush(stderr);
#ifdef __ANDROID__
    {   /* show it on screen too: the log is easy to miss on a phone */
        char msg[2048];
        va_start(ap, fmt);
        vsnprintf(msg, sizeof msg, fmt, ap);
        va_end(ap);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, GAME_TITLE, msg, NULL);
    }
#endif
    abort();
}

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* Loads a PE image at `want_base` (applying relocations if it differs from
 * the preferred base), resolves its imports and registers its exports.
 * Returns the entry point. */
static uint32_t load_pe(const char *path, uint32_t want_base, const char *modname) {
    FILE *f = fopen(path, "rb");
    if (!f) {  /* DirectX DLLs may sit in redist/, any case */
        static const char *const k_dirs[] = {"redist/", "Redist/", "REDIST/", NULL};
        char alt[512];
        for (int i = 0; !f && k_dirs[i]; i++) {
            snprintf(alt, sizeof alt, "%s%s", k_dirs[i], path);
            f = fopen(alt, "rb");
            if (!f && !strcasecmp(path, "d3dx9_43.dll")) {
                snprintf(alt, sizeof alt, "%sD3DX9_43.dll", k_dirs[i]);
                f = fopen(alt, "rb");
            }
        }
    }
    if (!f) rt_fatal("cannot open %s", path);
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = malloc(n);
    if (fread(buf, 1, n, f) != (size_t)n) rt_fatal("short read %s", path);
    fclose(f);
    uint32_t pe = rd32(buf + 0x3C);
    if (rd32(buf + pe) != 0x4550) rt_fatal("not a PE file");
    uint16_t nsec = rd16(buf + pe + 6);
    uint16_t optsz = rd16(buf + pe + 20);
    const uint8_t *opt = buf + pe + 24;
    uint32_t pref = rd32(opt + 28);
    uint32_t base = want_base ? want_base : pref;
    uint32_t entry = rd32(opt + 16) ? base + rd32(opt + 16) : 0;
    uint32_t image_size = rd32(opt + 56);
    uint32_t hdrsz = rd32(opt + 60);
    memcpy(MEM + base, buf, hdrsz);
    const uint8_t *sec = opt + optsz;
    for (int i = 0; i < nsec; i++, sec += 40) {
        uint32_t vsz = rd32(sec + 8), va = rd32(sec + 12), rawsz = rd32(sec + 16), raw = rd32(sec + 20);
        uint32_t cp = rawsz < vsz ? rawsz : vsz;
        if (!vsz) cp = rawsz;
        memcpy(MEM + base + va, buf + raw, cp);
    }
    /* relocations */
    if (base != pref) {
        uint32_t delta = base - pref;
        uint32_t rva = rd32(opt + 136), size = rd32(opt + 140);
        for (uint32_t p = base + rva; p < base + rva + size;) {
            uint32_t page = M32(p), bsz = M32(p + 4);
            if (!bsz) break;
            for (uint32_t e = p + 8; e < p + bsz; e += 2) {
                uint16_t ent = M16(e);
                if ((ent >> 12) == 3) M32(base + page + (ent & 0xFFF)) += delta;
            }
            p += bsz;
        }
    }
    /* exports */
    uint32_t exp_rva = rd32(opt + 96);
    if (exp_rva) {
        uint32_t ed = base + exp_rva;
        uint32_t nnames = M32(ed + 24), funcs = base + M32(ed + 28), names = base + M32(ed + 32),
                 ords = base + M32(ed + 36), ordbase = M32(ed + 16), nfuncs = M32(ed + 20);
        for (uint32_t i = 0; i < nnames; i++) {
            const char *nm = (const char *)(MEM + base + M32(names + 4 * i));
            uint16_t o = M16(ords + 2 * i);
            rt_register_data(modname, nm, base + M32(funcs + 4 * o));
        }
        for (uint32_t i = 0; i < nfuncs; i++) {
            char nm[16];
            snprintf(nm, sizeof nm, "#%u", ordbase + i);
            if (M32(funcs + 4 * i)) rt_register_data(modname, nm, base + M32(funcs + 4 * i));
        }
    }
    /* imports */
    uint32_t imp_rva = rd32(opt + 104);
    for (uint32_t d = base + imp_rva; imp_rva && M32(d + 12); d += 20) {
        const char *dll = (const char *)(MEM + base + M32(d + 12));
        uint32_t ilt = M32(d) ? base + M32(d) : base + M32(d + 16);
        uint32_t iat = base + M32(d + 16);
        for (; M32(ilt); ilt += 4, iat += 4) {
            uint32_t e = M32(ilt);
            char name[128];
            if (e & 0x80000000u) snprintf(name, sizeof name, "#%u", e & 0xFFFF);
            else snprintf(name, sizeof name, "%s", (const char *)(MEM + base + e + 2));
            M32(iat) = rt_import(dll, name);
        }
    }
    uint32_t tls_rva = rd32(opt + 168), tls_size = rd32(opt + 172);
    free(buf);
    extern void rt_module_add(const char *, uint32_t);
    rt_module_add(modname, base);
    LOG("loaded %s at %08x: entry %08x image %08x", path, base, entry, image_size);
    if (!strcmp(modname, GAME_EXE)) {
        g_entry = entry;
        g_image_size = image_size;
        if (tls_rva && tls_size) {
            extern void rt_set_static_tls(uint32_t, uint32_t, uint32_t, uint32_t);
            uint32_t d = base + tls_rva;
            rt_set_static_tls(M32(d), M32(d + 4), M32(d + 8), M32(d + 16));
            if (M32(d + 12) && M32(M32(d + 12))) LOG("warning: TLS callbacks not run");
        }
    }
    return entry;
}

/* Decodes a movie with the recompiled binkw32 only and writes a few frames
 * as PPM files: isolates Bink/MMX lifting problems from the game. */
static int bink_test(CPU *c, const char *movie, const char *outdir) {
    uint32_t e = load_pe("binkw32.dll", 0x02900000u, "binkw32.dll");
    uint32_t a3[3] = {0x02900000u, 1, 0};
    rt_invoke(c, e, 3, a3);
    uint32_t name = gstrdup(movie);
    uint32_t a2[2] = {name, 0};
    uint32_t bink = rt_invoke(c, rt_import("binkw32.dll", "_BinkOpen@8"), 2, a2);
    if (!bink) { LOG("BinkOpen failed"); return 1; }
    uint32_t w = M32(bink), h = M32(bink + 4), frames = M32(bink + 8);
    LOG("bink: %ux%u, %u frames", w, h, frames);
    uint32_t buf = gmalloc(w * h * 4);
    for (uint32_t f = 1; f <= frames && f <= 90; f++) {
        uint32_t a1[1] = {bink};
        rt_invoke(c, rt_import("binkw32.dll", "_BinkDoFrame@4"), 1, a1);
        uint32_t a7[7] = {bink, buf, w * 4, h, 0, 0, 3 /* BINKSURFACE32 */};
        rt_invoke(c, rt_import("binkw32.dll", "_BinkCopyToBuffer@28"), 7, a7);
        if (f == 1 || f == 30 || f == 90) {
            char path[1024];
            snprintf(path, sizeof path, "%s/frame%03u.ppm", outdir, f);
            FILE *o = fopen(path, "wb");
            fprintf(o, "P6 %u %u 255\n", w, h);
            for (uint32_t i = 0; i < w * h; i++) {
                uint8_t *px = MEM + buf + 4 * i;
                uint8_t rgb[3] = {px[2], px[1], px[0]};
                fwrite(rgb, 1, 3, o);
            }
            fclose(o);
            LOG("wrote %s", path);
        }
        rt_invoke(c, rt_import("binkw32.dll", "_BinkNextFrame@4"), 1, a1);
    }
    return 0;
}

#if defined(__GLIBC__)
#include <execinfo.h>
#include <signal.h>
#include <ucontext.h>
/* Crash report: host backtrace plus the faulting address. */
static void on_crash(int sig, siginfo_t *si, void *uc) {
    fprintf(stderr, "CRASH: signal %d at address %p\n", sig, si->si_addr);
#if defined(__x86_64__)
    ucontext_t *u = uc;
    greg_t *g = u->uc_mcontext.gregs;
    fprintf(stderr, "  rip=%llx rsp=%llx rax=%llx rcx=%llx rdx=%llx rbx=%llx rsi=%llx rdi=%llx\n",
            (unsigned long long)g[REG_RIP], (unsigned long long)g[REG_RSP],
            (unsigned long long)g[REG_RAX], (unsigned long long)g[REG_RCX],
            (unsigned long long)g[REG_RDX], (unsigned long long)g[REG_RBX],
            (unsigned long long)g[REG_RSI], (unsigned long long)g[REG_RDI]);
    fprintf(stderr, "  fault = MEM %+td\n", (uint8_t *)si->si_addr - MEM);
    {   /* guest call chain: scan the guest stack for code-range words */
        CPU *c = rt_current_cpu();
        if (c && MEM) {
            uint32_t sp = c->r[4];
            fprintf(stderr, "  guest esp=%08x ecx=%08x esi=%08x edi=%08x\n", sp, c->r[1], c->r[6], c->r[7]);
            fprintf(stderr, "  guest stack:");
            for (int i = 0; i < 220; i++) {
                uint32_t v = M32(sp + 4 * i);
                int core = v >= 0x1000000 && v < 0x12b3000;
                int exe = v >= 0x10900000 && v < 0x10950000;
                int eng = v >= 0x1400000 && v < 0x1c29000;
                int sga = v >= 0x3200000 && v < 0x32c3000;
                if (core || exe || eng || sga) fprintf(stderr, " %08x", v);
            }
            fputc('\n', stderr);
            /* dump the this/struct objects the crashing frame was touching */
            if (c->r[6] > 0x10000 && c->r[6] < 0x40000000u) {
                uint32_t a = M32(c->r[6]);
                fprintf(stderr, "  [esi]=%08x [esi+4]=%08x\n", a, M32(c->r[6] + 4));
                if (a > 0x10000 && a < 0x40000000u) {
                    fprintf(stderr, "  obj at %08x:", a);
                    for (int i = 0; i < 12; i++) fprintf(stderr, " %08x", M32(a + 4 * i));
                    fputc('\n', stderr);
                }
            }
        }
    }
#endif
    void *bt[64];
    int n = backtrace(bt, 64);
    backtrace_symbols_fd(bt, n, 2);
    _exit(128 + sig);
}
static void install_crash_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO;
    sigaction(SIGSEGV, &sa, NULL);
    sigaction(SIGBUS, &sa, NULL);
    sigaction(SIGFPE, &sa, NULL);
    sigaction(SIGABRT, &sa, NULL);
}
#elif defined(__ANDROID__)
#include <signal.h>
#include <dlfcn.h>
#include <unwind.h>
#include <ucontext.h>
/* Crash report for log.txt: fault address, registers and a backtrace as
 * library+offset (symbolise with the unstripped libmain.so from the build).
 * The previous handler (debuggerd) runs afterwards so a tombstone is kept. */
static struct sigaction g_old_sa[32];
static void print_pc(int i, uintptr_t pc) {
    Dl_info di;
    if (dladdr((void *)pc, &di) && di.dli_fname) {
        const char *b = strrchr(di.dli_fname, '/');
        fprintf(stderr, "  #%02d %s+0x%lx", i, b ? b + 1 : di.dli_fname, (unsigned long)(pc - (uintptr_t)di.dli_fbase));
        if (di.dli_sname) fprintf(stderr, " (%s+0x%lx)", di.dli_sname, (unsigned long)(pc - (uintptr_t)di.dli_saddr));
        fputc('\n', stderr);
    } else fprintf(stderr, "  #%02d 0x%lx\n", i, (unsigned long)pc);
}
typedef struct { int n; } BtState;
static _Unwind_Reason_Code bt_step(struct _Unwind_Context *ctx, void *arg) {
    BtState *st = arg;
    uintptr_t pc = _Unwind_GetIP(ctx);
    if (pc) print_pc(st->n, pc);
    return ++st->n >= 48 ? _URC_END_OF_STACK : _URC_NO_REASON;
}
static void on_crash(int sig, siginfo_t *si, void *ucv) {
    ucontext_t *uc = ucv;
    fprintf(stderr, "CRASH: signal %d code %d at address %p (thread %d)\n", sig, si->si_code, si->si_addr, gettid());
#if defined(__aarch64__)
    uintptr_t pc = uc->uc_mcontext.pc, lr = uc->uc_mcontext.regs[30];
    fprintf(stderr, "  sp %llx\n", (unsigned long long)uc->uc_mcontext.sp);
    for (int r = 0; r < 31; r += 4)
        fprintf(stderr, "  x%-2d %016llx %016llx %016llx %016llx\n", r,
                (unsigned long long)uc->uc_mcontext.regs[r], (unsigned long long)uc->uc_mcontext.regs[r + 1 < 31 ? r + 1 : 30],
                (unsigned long long)uc->uc_mcontext.regs[r + 2 < 31 ? r + 2 : 30], (unsigned long long)uc->uc_mcontext.regs[r + 3 < 31 ? r + 3 : 30]);
    fprintf(stderr, "  guest mem base %p\n", (void *)MEM);
    extern void rt_dump_guest(void);
    rt_dump_guest();
    fprintf(stderr, "backtrace:\n");
    print_pc(-1, pc);
    print_pc(-2, lr);
#endif
    BtState st = {0};
    _Unwind_Backtrace(bt_step, &st);
    fflush(stderr);
    sigaction(sig, &g_old_sa[sig], NULL);  /* let debuggerd write its tombstone */
}
void rt_altstack(void) {
    stack_t ss;
    ss.ss_size = 64 * 1024;
    ss.ss_sp = malloc(ss.ss_size);
    ss.ss_flags = 0;
    if (ss.ss_sp) sigaltstack(&ss, NULL);
}
static void install_crash_handler(void) {
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_crash;
    sa.sa_flags = SA_SIGINFO | SA_ONSTACK;
    rt_altstack();
    int sigs[] = {SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
    for (unsigned i = 0; i < sizeof sigs / sizeof *sigs; i++) sigaction(sigs[i], &sa, &g_old_sa[sigs[i]]);
}
#else
static void install_crash_handler(void) {}
#endif
#if !defined(__ANDROID__)
void rt_altstack(void) {}
#endif

#ifdef __ANDROID__
#include <SDL2/SDL_main.h>
#include <SDL2/SDL_system.h>
#include <sys/stat.h>
#endif

/* Fails with a message naming exactly what is missing from the game folder. */
static int file_present(const char *name) {
    static const char *const k_dirs[] = {"", "redist/", "Redist/", "REDIST/", NULL};
    char lower[256];
    snprintf(lower, sizeof lower, "%s", name);
    for (char *q = lower; *q; q++) *q = (char)tolower((unsigned char)*q);
    for (int i = 0; k_dirs[i]; i++) {
        char p[512];
        snprintf(p, sizeof p, "%s%s", k_dirs[i], name);
        if (!access(p, R_OK)) return 1;
        snprintf(p, sizeof p, "%s%s", k_dirs[i], lower);
        if (!access(p, R_OK)) return 1;
        /* any case: scan the folder */
        DIR *d = opendir(k_dirs[i][0] ? k_dirs[i] : ".");
        if (!d) continue;
        struct dirent *de;
        int hit = 0;
        while ((de = readdir(d)) && !hit) hit = !strcasecmp(de->d_name, name);
        closedir(d);
        if (hit) return 1;
    }
    return 0;
}

static void check_game_dir(void) {
    /* Runs after main() chdir()ed to <root>/Content/System. */
    static const char *const k_need[] = {
        GAME_EXE, "Core.dll", "Engine.dll", "Window.dll", "WinDrv.dll",
        "D3DDrv.dll", "ALAudio.dll", "DefOpenAL32.dll", "GUI.dll",
        "binkw32.dll", "SwatGame.dll", "SwatGui.dll", NULL};
    char missing[1024] = "";
    for (int i = 0; k_need[i]; i++)
        if (!file_present(k_need[i])) {
            if (missing[0]) strncat(missing, ", ", sizeof missing - strlen(missing) - 1);
            strncat(missing, k_need[i], sizeof missing - strlen(missing) - 1);
        }
    if (missing[0])
        rt_fatal("missing from %s: %s. Point the launcher at the folder that "
                 "contains Content/ and ContentExpansion/ (the GOG install root).",
                 g_game_dir, missing);
}

/* Reads an integer key from <root>/port/port.cfg (written by the launcher). */
static int cfg_int(const char *path, const char *key, int def) {
    FILE *f = fopen(path, "r");
    if (!f) return def;
    char line[256];
    size_t kl = strlen(key);
    int v = def;
    while (fgets(line, sizeof line, f))
        if (!strncmp(line, key, kl) && line[kl] == '=') v = atoi(line + kl + 1);
    fclose(f);
    return v;
}

/* DLLs whose DllMain has run. On Windows only the exe's static import
 * closure exists before WinMain; the engine loads the renderer, audio driver,
 * GUI and game modules with LoadLibrary after appInit has created GMalloc.
 * Running their static initializers earlier hits "Called appMalloc before
 * memory init" in AllocateNameEntry. */
static uint32_t g_dll_entry[64];
static uint8_t g_dll_inited[64];
extern const char *const g_dll_modules[];
extern const uint32_t g_dll_bases[];

int rt_dll_index(const char *name) {
    /* UE2's ALAudio asks for OpenAL32.dll; the GOG install ships the same
     * library as DefOpenAL32.dll (the "Def" driver copy). */
    if (!strcasecmp(name, "OpenAL32.dll") || !strcasecmp(name, "OpenAL32"))
        name = "DefOpenAL32.dll";
    /* The game LoadLibrary()s "SwatGame", "Engine", ... without the .dll
     * extension. Without this normalization the map lookup fails, DllMain is
     * never run and the DLL's class-registration slots (autoclass*) stay
     * zero, which crashes UClass::Bind with a NULL class. */
    static _Thread_local char norm[64];
    if (!strchr(name, '.')) {
        snprintf(norm, sizeof norm, "%s.dll", name);
        name = norm;
    }
    for (int i = 0; g_dll_modules[i]; i++)
        if (!strcasecmp(g_dll_modules[i], name)) return i;
    return -1;
}
uint32_t rt_ensure_dll_init(CPU *c, const char *name);

/* Walks the import table of the mapped PE at `base` and runs cb() for each
 * imported DLL name, so DllMain can be run in Windows loader order (static
 * imports first). RWOSupport.dll, GUI.dll & co. are only imported (never
 * LoadLibrary'd by the game) but their static initializers must still run:
 * SwatGame's class registration crashed on a never-constructed global of
 * theirs (vtable still zero). */
static void for_each_import_dll(uint32_t base, void (*cb)(const char *, void *), void *ud) {
    uint32_t pe = M32(base + 0x3C);
    if (M32(base + pe) != 0x4550) return;
    uint32_t opt = base + pe + 24;
    uint32_t imp_rva = M32(opt + 104);
    if (!imp_rva) return;
    for (uint32_t d = base + imp_rva; M32(d + 12); d += 20) {
        uint32_t nm_rva = M32(d + 12);
        char name[64];
        uint32_t i = 0;
        for (; i < sizeof name - 1 && M8(base + nm_rva + i); i++) name[i] = (char)M8(base + nm_rva + i);
        name[i] = 0;
        cb(name, ud);
    }
}

static void init_dep_cb(const char *dll, void *ud) { rt_ensure_dll_init((CPU *)ud, dll); }
static int g_dll_deps_enabled = 1; /* off while the boot preload list runs */

uint32_t rt_ensure_dll_init(CPU *c, const char *name) {
    int i = rt_dll_index(name);
    if (i < 0) return 0;
    if (!g_dll_inited[i]) {
        g_dll_inited[i] = 1; /* marked first: import graphs can cycle */
        if (g_dll_deps_enabled) for_each_import_dll(g_dll_bases[i], init_dep_cb, c);
        if (g_dll_entry[i]) {
            LOG("DllMain(%s)", rt_dll_index(name) >= 0 ? g_dll_modules[rt_dll_index(name)] : name);
            uint32_t args[3] = {g_dll_bases[i], 1, 0};
            rt_invoke(c, g_dll_entry[i], 3, args);
        }
    }
    return g_dll_bases[i];
}

/* One-time compat fixups over the user's game files, run before the engine
 * boots. Every fix here papers over a real bug in the shipped data that UE2
 * tolerates on Windows only because its C++ exception machinery can unwind —
 * the lifted code cannot catch, so for us the throw would be fatal. */
static char *read_whole_file(const char *path, long *out_n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > (8 << 20)) { fclose(f); return NULL; }
    char *buf = malloc(n + 1);
    if (!buf || fread(buf, 1, n, f) != (size_t)n) { free(buf); fclose(f); return NULL; }
    buf[n] = 0;
    fclose(f);
    *out_n = n;
    return buf;
}
static int write_whole_file(const char *path, const char *buf, long n) {
    FILE *o = fopen(path, "wb");
    if (!o) return 0;
    size_t w = fwrite(buf, 1, n, o);
    fclose(o);
    return w == (size_t)n;
}
static int str_count(const char *hay, const char *needle) {
    int n = 0;
    for (const char *p = hay; (p = strstr(p, needle)) != NULL; p++) n++;
    return n;
}
static char *str_replace_all(const char *src, const char *from, const char *to, long *out_n) {
    int hits = str_count(src, from);
    if (!hits) return NULL;
    char *out = malloc(strlen(src) + (size_t)hits * (strlen(to) - strlen(from)) + 1);
    char *w = out;
    for (const char *p = src; *p;) {
        const char *hit = strstr(p, from);
        if (!hit) { strcpy(w, p); break; }
        memcpy(w, p, hit - p); w += hit - p;
        memcpy(w, to, strlen(to)); w += strlen(to);
        p = hit + strlen(from);
    }
    *out_n = strlen(out);
    return out;
}

static void fixup_game_data(void) {
    char path[1400];
    long n;
    /* 1) GOG SWAT 4 ships GuiBase.ini styles pointing at
     * Texture'gui_tex.resetbutton', a texture that is in NO shipped package
     * (verified against every .utx name table). UE2's StaticLoadObject throws
     * "Failed to find object" for it while loading the style defaults and the
     * caller's catch swallows the throw on Windows. Point the style at the
     * shipped 'backbox' button texture instead. */
    snprintf(path, sizeof path, "%s/%s/GuiBase.ini", g_game_dir, GAME_SYS_DIR);
    char *buf = read_whole_file(path, &n);
    if (buf && strstr(buf, "gui_tex.resetbutton")) {
        long out_n;
        char *out = str_replace_all(buf, "gui_tex.resetbutton", "gui_tex.backbox", &out_n);
        if (out && write_whole_file(path, out, out_n))
            LOG("GuiBase.ini: rewrote %d missing Texture'gui_tex.resetbutton' refs to gui_tex.backbox",
                str_count(buf, "gui_tex.resetbutton"));
        free(out);
    }
    free(buf);
    /* 2) Engine.dll's GUI controller resolves ini:Engine.Engine.DefaultPlayerMenu
     * (a UE2.5 key that the SWAT 4 ini never shipped). The retail game never
     * reaches that code path because the triggering object field stays NULL;
     * here it does not, so give the engine the same main-menu class the game's
     * own Default.ini uses for InitialMenuClass. */
    snprintf(path, sizeof path, "%s/%s/Swat4.ini", g_game_dir, GAME_SYS_DIR);
    buf = read_whole_file(path, &n);
    if (buf && !strstr(buf, "DefaultPlayerMenu=") && strstr(buf, "RepoClassName=SwatGame.SwatRepo")) {
        /* the ini may use CRLF: insert after the RepoClassName line's EOL */
        const char *repo = strstr(buf, "RepoClassName=SwatGame.SwatRepo");
        const char *eol = strchr(repo, '\n');
        if (eol) {
            char nl = *eol;
            const char *ins = nl == '\r' ? "\r\nDefaultPlayerMenu=SwatGui.SwatMainMenu"
                                         : "\nDefaultPlayerMenu=SwatGui.SwatMainMenu";
            size_t pre = (size_t)(eol - buf) + 1;
            long out_n = n + strlen(ins);
            char *out = malloc(out_n + 1);
            memcpy(out, buf, pre);
            strcpy(out + pre, ins);
            memcpy(out + pre + strlen(ins), buf + pre, n - pre);
            out[out_n] = 0;
            if (write_whole_file(path, out, out_n))
                LOG("Swat4.ini: added Engine.Engine.DefaultPlayerMenu=SwatGui.SwatMainMenu");
            free(out);
        }
    }
    free(buf);
}

int main(int argc, char **argv) {
    install_crash_handler();
    const char *env = getenv("LB_LOG");
    if (env) g_log_level = atoi(env);
#ifdef __ANDROID__
    /* The launcher passes the game folder as argv[1]; everything this port
     * writes lives in <game>/port (log.txt, last_exit.txt, save/), outside
     * Android/data. Until argv[1] arrives, point at the app's own folder so
     * early errors still get a log somewhere writable. */
    const char *ext = SDL_AndroidGetExternalStoragePath();
    if (ext) {
        snprintf(g_game_dir, sizeof g_game_dir, "%s/game", ext);
        snprintf(g_save_dir, sizeof g_save_dir, "%s/save", ext);
        mkdir(g_game_dir, 0777);
        mkdir(g_save_dir, 0777);
    }
    if (!env) g_log_level = 1;
#endif
    if (argc > 1) snprintf(g_game_dir, sizeof g_game_dir, "%s", argv[1]);
    if (argc > 2) snprintf(g_save_dir, sizeof g_save_dir, "%s", argv[2]);
#if defined(GAME_SWAT4)
    {   /* <root>/port: log, exit report and saves (outside Android/data) */
        char sys[1300], port[1200], logp[1300];
        snprintf(sys, sizeof sys, "%s/%s", g_game_dir, GAME_SYS_DIR);
        if (chdir(sys) != 0) {
            /* the user may have picked Content/System itself */
            if (chdir(g_game_dir) != 0)
                rt_fatal("cannot open the game folder %s", g_game_dir);
        }
        snprintf(port, sizeof port, "%s/port", g_game_dir);
        mkdir(port, 0777);
        snprintf(g_save_dir, sizeof g_save_dir, "%s/save", port);
        mkdir(g_save_dir, 0777);
        snprintf(logp, sizeof logp, "%s/log.txt", port);
        if (freopen(logp, "w", stderr)) {
            /* Full buffering: with the trace on (log=2) the boot logs millions
             * of lines; line buffering turned every line into an FUSE write
             * and the package preload crawled for dozens of minutes. Flushed
             * by the splash tick (boot), the present hook (frames) and every
             * fatal/exit path. */
            static char lbuf[1 << 18];
            setvbuf(stderr, lbuf, _IOFBF, sizeof lbuf);
        }
        if (!env) {
            char cfg[1400];
            snprintf(cfg, sizeof cfg, "%s/port/port.cfg", g_game_dir);
            g_log_level = cfg_int(cfg, "log", g_log_level);
        }
    }
#endif
    MEM = mmap(NULL, 1ull << 32, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
    if (MEM == MAP_FAILED) rt_fatal("cannot reserve guest address space");
    fixup_game_data();
    LOG("guest memory at %p (4 GiB)", (void *)MEM);
    /* keep the null page inaccessible to catch null derefs */
    mprotect(MEM, 0x10000, PROT_NONE);
    rt_init_shims();
    /* dxvk writes its pipeline state cache to the working directory; keep the game folder clean */
    if (!getenv("DXVK_STATE_CACHE_PATH")) setenv("DXVK_STATE_CACHE_PATH", g_save_dir, 1);
    /* 0.2.6: no compile status HUD (as in 0.1.5); DXVK_HUD=portstatus enables it. */
    check_game_dir();
extern void platform_init(void);
    platform_init();
    CPU *c = rt_new_cpu();
    rt_thread_init(c);
    if (argc > 4 && !strcmp(argv[3], "--bink-test")) return bink_test(c, argv[4], argc > 5 ? argv[5] : ".");
    /* DLLs first so the exe's imports bind to their exports */
    extern const char *const g_dll_modules[];
    extern const uint32_t g_dll_bases[];
    int ndll = 0;
    for (; g_dll_modules[ndll]; ndll++)
        g_dll_entry[ndll] = load_pe(g_dll_modules[ndll], g_dll_bases[ndll], g_dll_modules[ndll]);
    load_pe(GAME_EXE, 0, GAME_EXE);
    extern void touch_setup_guest(void);
    touch_setup_guest();
    /* Static import closure of Swat4.exe (verified with pefile): its own game
     * DLL imports plus their transitive game-DLL dependencies. Everything
     * else is initialised on demand by LoadLibrary. */
    static const char *const k_preload[] = {
        "ogg.dll", "vorbis.dll", "vorbisfile.dll", "binkw32.dll", "m4d.dll",
        "IFC23.dll", "IrrSupport.dll", "RenderUtilities.dll",
        "Core.dll", "Engine.dll", "Scripting.dll", "Window.dll",
        "AICommon.dll", "SwatAIAwareness.dll", "ALAudio.dll", NULL};
    g_dll_deps_enabled = 0; /* preload keeps its tested boot-time behaviour */
    for (int i = 0; k_preload[i]; i++)
        if (!rt_ensure_dll_init(c, k_preload[i]))
            rt_fatal("preload: %s is not in the module map", k_preload[i]);
    g_dll_deps_enabled = 1;
    /* push a return address that ends the process when main returns */
    c->r[4] -= 4;
    M32(c->r[4]) = 0;
    rt_call(c, g_entry);
    LOG("entry returned eax=%u", c->r[0]);
    return (int)c->r[0];
}
