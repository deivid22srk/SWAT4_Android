/* Harness: valida o negcache + rt_host_path fora do runtime.
   Compila o bloco real de runtime/kernel32.c (DirCache..mkdirs_for) com um
   main de teste e uma árvore de jogo falsa em /tmp/negtest.

   Uso (gera o include e compila):
     python3 - <<'EOF'
     src = open('runtime/kernel32.c').read()
     i = src.index('typedef struct { char dir[1024]; time_t mtime;')
     j = src.index('\nstatic void mkdirs_for(', i)
     open('/tmp/kernel_block.inc','w').write(src[i:j] + "\n")
     EOF
     gcc -O1 -std=gnu11 -o /tmp/negcache_test tools/negcache_test.c -lpthread \
         -include stdint.h && /tmp/negcache_test
   (compile com -DKERNEL_BLOCK='"/tmp/kernel_block.inc"' ou ajuste o include
    abaixo antes de compilar.) */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <dirent.h>
#include <sys/stat.h>
#include <pthread.h>

char g_game_dir[1024];
char g_save_dir[1024];
static const char *k_win_game_prefix = "\\gog games\\swat 4\\";
static const char *k_win_user_prefix = "\\users\\player\\";

/* bloco real de kernel32.c (dircache + negcache + canonicalize + resolve_ci +
   rt_host_path), extraído na compilação */
#ifndef KERNEL_BLOCK
#define KERNEL_BLOCK "kernel_block.inc"
#endif
#include KERNEL_BLOCK

static int fails;
static void check(int cond, const char *what) {
    printf("%s: %s\n", cond ? "ok  " : "FALHOU", what);
    if (!cond) fails++;
}

int main(void) {
    system("rm -rf /tmp/negtest && mkdir -p '/tmp/negtest/game/Content/System'");
    FILE *f = fopen("/tmp/negtest/game/Content/System/SwatGame.int", "w");
    fputs("x", f); fclose(f);
    snprintf(g_game_dir, sizeof g_game_dir, "/tmp/negtest/game");
    snprintf(g_save_dir, sizeof g_save_dir, "/tmp/negtest/save");
    char out[1024];
    const char *nope  = "\\gog games\\swat 4\\Content\\System\\Nope.int";
    const char *g_int = "\\gog games\\swat 4\\Content\\System\\SwatGame.int";
    const char *dot   = "\\gog games\\swat 4\\Content\\System\\.\\..\\..\\Content\\System\\SwatGame.int";
    const char *up    = "\\GOG GAMES\\SWAT 4\\CONTENT\\SYSTEM\\SWATGAME.INT";

    int r1 = rt_host_path(nope, out, sizeof out);
    check(r1 == 0, "sonda de arquivo ausente devolve 0");
    int r2 = rt_host_path(nope, out, sizeof out);
    check(r2 == -1, "2a sonda do mesmo ausente vem do negcache (-1)");
    int r2b = rt_host_path(nope, out, sizeof out);
    check(r2b == -1, "3a sonda tambem (-1), sem FUSE");

    f = fopen("/tmp/negtest/game/Content/System/Nope.int", "w");
    fputs("x", f); fclose(f);
    dircache_forget("/tmp/negtest/game/Content/System");
    int r3 = rt_host_path(nope, out, sizeof out);
    check(r3 == 1, "apos criar + dircache_forget a sonda encontra (1)");

    int r4 = rt_host_path(g_int, out, sizeof out);
    check(r4 == 1 && !!strstr(out, "SwatGame.int"), "arquivo existente resolve com caixa correta");
    int r4b = rt_host_path(g_int, out, sizeof out);
    check(r4b == 1 && !!strstr(out, "SwatGame.int"), "2a sonda do existente continua resolvendo");

    int r5 = rt_host_path(dot, out, sizeof out);
    check(r5 == 1 && !!strstr(out, "Content/System/SwatGame.int"), "grafia com ./ e ../ canoniza e resolve");

    int r6 = rt_host_path(up, out, sizeof out);
    check(r6 == 1 && !!strstr(out, "SwatGame.int"), "caixa diferente resolve (case-insensitive)");

    /* as 4 grafias do mesmo arquivo devem cair no mesmo caminho canonico */
    char a[1024], b[1024];
    rt_host_path("\\gog games\\swat 4\\Content\\System\\SwatGame.int", a, sizeof a);
    rt_host_path("\\gog games\\swat 4\\Content\\System\\..\\..\\Content\\System\\SwatGame.int", b, sizeof b);
    check(!strcmp(a, b), "grafia distintas canonizam para o mesmo host path");

    printf(fails ? "== %d FALHAS ==\n" : "== tudo ok ==\n", fails);
    return fails != 0;
}
