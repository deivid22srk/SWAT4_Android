// renderer_host: despachante entre a bridge D3D9 e o backend grafico.
//
// O jogo chama Direct3DCreate9; a bridge (runtime/d3d9_bridge.cpp) chama esta
// funcao, que decide ENTRE os dois backends compilados no processo:
//   - "dxvk"  (default e fallback): o dxvk-native vendored (libdxvk_d3d9).
//   - "native": o modulo novo (android/app/jni/native-render, libnative_render),
//     entry point nvr_Direct3DCreate9 (simbolo exportado propositadamente com
//     outro nome para os dois coexistirem no mesmo binario).
//
// Selecao (nesta ordem):
//   1. variavel de ambiente LB_RENDERER=dxvk|native
//   2. primeira linha de <g_game_dir>/port/renderer.txt ("dxvk" ou "native")
//   3. default: dxvk (regra 3 do projeto: dxvk permanece o padrao ate o
//      native-render ser validado em campo)
//
// A troca e por PROCESSO (feita antes do primeiro CreateDevice); trocar o
// renderer exige reiniciar o jogo — igual ao comportamento de um driver real.
#include "renderer_host.h"
#include "rt.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>

extern "C" {
// do dxvk-native (linkado sempre; o backend escolhido decide quem responde)
IDirect3D9 *Direct3DCreate9(UINT SDKVersion);

// do native-render (libnative_render)
void *nvr_Direct3DCreate9(uint32_t sdk_version);
void nvr_set_cache_dir(const char *dir);
const char *nvr_version_string(void);
}

static bool renderer_host_logged = false;

static bool read_renderer_file(char *out, size_t cap) {
    char path[1100];
    snprintf(path, sizeof path, "%s/port/renderer.txt", g_game_dir);
    FILE *f = fopen(path, "r");
    if (!f) return false;
    char line[64] = {0};
    bool ok = false;
    while (fgets(line, sizeof line, f)) {
        // primeira linha nao vazia; trim simples
        char *s = line;
        while (*s == ' ' || *s == '\t') s++;
        char *e = s + strlen(s);
        while (e > s && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '\t')) *--e = 0;
        if (*s) {
            snprintf(out, cap, "%s", s);
            ok = true;
            break;
        }
    }
    fclose(f);
    return ok;
}

IDirect3D9 *renderer_host_direct3d9(UINT sdk_version) {
    char choice[64] = "dxvk";
    const char *env = getenv("LB_RENDERER");
    if (env && *env) {
        snprintf(choice, sizeof choice, "%s", env);
    } else if (read_renderer_file(choice, sizeof choice)) {
        // arquivo vence o default; env vence arquivo
        if (env && *env) snprintf(choice, sizeof choice, "%s", env);
    }

    if (!renderer_host_logged) {
        renderer_host_logged = true;
        LOG("renderer_host: backend escolhido = %s (LB_RENDERER=%s)", choice,
        env && *env ? env : "(nao definido)");
    }

    if (!strcmp(choice, "native")) {
        // cache do native-render em <jogo>/port/native-render
        char cache[1100];
        snprintf(cache, sizeof cache, "%s/port/native-render", g_game_dir);
        mkdir(cache, 0777);   // best effort (o port/ ja existe)
        nvr_set_cache_dir(cache);
        LOG("renderer_host: %s", nvr_version_string());
        return (IDirect3D9 *)nvr_Direct3DCreate9(sdk_version);
    }

    // dxvk: caminho original da bridge (fallback e default)
    return Direct3DCreate9(sdk_version);
}
