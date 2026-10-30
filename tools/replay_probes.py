#!/usr/bin/env python3
"""Replay validator for the Win32 file layer (runtime/kernel32.c).

Extracts the real mapping block (dircache / dir_lookup_ci / resolve_ci /
path_canonicalize / rt_host_path) from kernel32.c and compiles it side by
side with a pre-fix variant (no canonicalization, 16 cache slots), then
replays a list of Windows paths captured from a device log and checks that
both variants resolve to the same inodes, while reporting how many distinct
directory keys each variant feeds the cache.

Usage:
  replay_probes.py extract <kslog.txt> <probes.txt>
      pull the distinct CreateFileA paths out of a runner log
  replay_probes.py test <probes.txt> <game-dir> [workdir]
      generate canon_test.c, build and run it against <game-dir>

The exit code is 0 only when there are no resolution mismatches.
"""
import re, subprocess, sys, os

DEF = 'typedef struct { char dir[1024]; time_t mtime;'
END = '\nstatic void mkdirs_for('


def kernel_block(kernel32_path):
    src = open(kernel32_path).read()
    i = src.index(DEF)
    j = src.index(END, i)
    return src[i:j]


def extract(log_path, out_path):
    paths = set()
    call = re.compile(r'^CreateFileA\((C:.*)\)\s*$')
    res = re.compile(r'^CreateFileA (C:.*) -> .* fd=')
    with open(log_path, encoding='utf-8', errors='replace') as f:
        for line in f:
            m = call.match(line) or res.match(line)
            if m:
                paths.add(m.group(1))
    with open(out_path, 'w') as f:
        f.write('\n'.join(sorted(paths)) + '\n')
    print(f'{len(paths)} distinct CreateFileA paths -> {out_path}')


def gen_sources(kernel32_path):
    b = kernel_block(kernel32_path)
    prefix_re = re.compile(r'static const char \*k_win_(?:game|user)_prefix[^;]*;\n', re.M)
    gamedir_re = re.compile(r'char g_(?:game|save)_dir[^;]*;\n', re.M)

    new = b
    new = prefix_re.sub('', new)
    new = gamedir_re.sub('', new)
    new = new.replace('rt_host_path', 'rt_host_path_n')
    new = new.replace('dir_lookup_ci(', 'dir_lookup_ci_n(')
    new = new.replace('static int dir_lookup_ci_n(', 'static int real_dir_lookup_ci_n(')

    old = b.replace('    path_canonicalize(out);\n', '')
    old = re.sub(r'/\* Lexically collapse.*?\nstatic void path_canonicalize\(char \*path\) \{.*?\n\}\n\n',
                 '', old, flags=re.S)
    old = old.replace('DirCache', 'DirCacheO')
    old = old.replace('DIRCACHE_SLOTS', 'OLD_DC_SLOTS')
    old = old.replace('#define OLD_DC_SLOTS 256', '#define OLD_DC_SLOTS 16')
    for name in ['g_dircache', 'g_dircache_next', 'g_dircache_mu',
                 'dircache_forget', 'dir_lookup_ci', 'resolve_ci', 'rt_host_path',
                 'k_win_game_prefix', 'k_win_user_prefix', 'g_game_dir', 'g_save_dir']:
        old = re.sub(r'\b%s\b' % name, name + '_o', old)
    old = prefix_re.sub('', old)
    old = gamedir_re.sub('', old)
    return new, old


MAIN = r'''
int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <probes.txt> <gamedir>\n", argv[0]); return 2; }
    snprintf(g_game_dir,   sizeof g_game_dir,   "%s", argv[2]);
    snprintf(g_game_dir_o, sizeof g_game_dir_o, "%s", argv[2]);
    snprintf(g_save_dir,   sizeof g_save_dir,   "/tmp/save_n");
    snprintf(g_save_dir_o, sizeof g_save_dir_o, "/tmp/save_o");

    /* pass 1: correctness — old vs new must agree on every probe */
    FILE *f = fopen(argv[1], "r");
    if (!f) { perror("probes"); return 2; }
    char line[2048];
    long total = 0, mism = 0, found_o = 0, found_n = 0;
    while (fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        while (l && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = 0;
        if (!l) continue;
        char a[1024], b2[1024];
        int ro = rt_host_path_o(line, a, sizeof a);
        int rn = rt_host_path_n(line, b2, sizeof b2);
        total++;
        if (ro) found_o++;
        if (rn) found_n++;
        struct stat sa, sb;
        int eo = stat(a, &sa), en = stat(b2, &sb);
        int same;
        if (eo == 0 && en == 0) same = sa.st_ino == sb.st_ino && sa.st_dev == sb.st_dev;
        else if (eo != 0 && en != 0) same = 1;
        else same = 0;
        if (!same || ro != rn) {
            if (mism < 15)
                printf("MISMATCH: %s\n  old(%d): %s\n  new(%d): %s\n", line, ro, a, rn, b2);
            mism++;
        }
    }
    fclose(f);
    long opens_new = g_opens; long keys_new = g_nkeys;

    /* pass 2: old mapper alone, to count its cache pressure */
    g_opens = 0; g_nkeys = 0;
    f = fopen(argv[1], "r");
    while (fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        while (l && (line[l-1] == '\n' || line[l-1] == '\r')) line[--l] = 0;
        if (!l) continue;
        char a[1024];
        rt_host_path_o(line, a, sizeof a);
    }
    fclose(f);
    long opens_old = g_opens; long keys_old = g_nkeys;

    printf("probes replayed:       %ld\n", total);
    printf("resolve mismatches:    %ld  (must be 0)\n", mism);
    printf("found old/new:         %ld / %ld\n", found_o, found_n);
    printf("dir keys OLD:          %ld distinct (16 slots)\n", keys_old);
    printf("dir keys NEW:          %ld distinct (256 slots)\n", keys_new);
    return mism ? 1 : 0;
}
'''

TEMPLATE = r'''
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <pthread.h>
#include <sys/stat.h>
#include <dirent.h>
#include <errno.h>
#include <time.h>
#include <unistd.h>

static const char *k_win_game_prefix     = "\\gog games\\swat 4\\";
static const char *k_win_user_prefix     = "\\users\\player\\";
static const char *k_win_game_prefix_o   = "\\gog games\\swat 4\\";
static const char *k_win_user_prefix_o   = "\\users\\player\\";
char g_game_dir[512],    g_save_dir[512];
char g_game_dir_o[512],  g_save_dir_o[512];

static long g_opens, g_nkeys;
static char g_keys[4096][1024];

static int dir_lookup_ci_n(const char *dir, char *comp, size_t compsz);

___NEW___

/* counting wrapper for the new dir_lookup_ci */
static int dir_lookup_ci_n(const char *dir, char *comp, size_t compsz) {
    int found = 0;
    for (int i = 0; i < g_nkeys; i++)
        if (!strcmp(g_keys[i], dir)) { found = 1; break; }
    if (!found && g_nkeys < 4096) { snprintf(g_keys[g_nkeys], 1024, "%s", dir); g_nkeys++; }
    g_opens++;
    return real_dir_lookup_ci_n(dir, comp, compsz);
}

___OLD___

___MAIN___
'''


def test(probes, gamedir, workdir, kernel32):
    new, old = gen_sources(kernel32)
    src = TEMPLATE.replace('___NEW___', new).replace('___OLD___', old).replace('___MAIN___', MAIN)
    os.makedirs(workdir, exist_ok=True)
    c = os.path.join(workdir, 'canon_test.c')
    exe = os.path.join(workdir, 'canon_test')
    open(c, 'w').write(src)
    subprocess.run(['gcc', '-w', '-O2', '-o', exe, c, '-lpthread'], check=True)
    r = subprocess.run([exe, probes, gamedir])
    sys.exit(r.returncode)


if __name__ == '__main__':
    if len(sys.argv) >= 4 and sys.argv[1] == 'extract':
        extract(sys.argv[2], sys.argv[3])
    elif len(sys.argv) >= 4 and sys.argv[1] == 'test':
        repo = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
        k32 = os.path.join(repo, 'runtime', 'kernel32.c')
        work = sys.argv[4] if len(sys.argv) >= 5 else '/tmp/replay_probes'
        test(sys.argv[2], sys.argv[3], work, k32)
    else:
        print(__doc__)
        sys.exit(2)
