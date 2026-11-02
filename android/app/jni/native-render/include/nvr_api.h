// native-render: Vulkan backend exclusivo do port SWAT4 Android.
// Ponto de entrada publico. Todo o resto do modulo e interno.
//
// O simbolo exportado deliberadamente NAO se chama "Direct3DCreate9": o dxvk
// continua linkado no processo como fallback, e ambos coexistem. A selecao
// entre os dois acontece em runtime/renderer_host.cpp.
#pragma once
#include <stdint.h>

#if defined(_WIN32)
#define NVR_EXPORT __declspec(dllexport)
#else
#define NVR_EXPORT __attribute__((visibility("default")))
#endif

extern "C" {
// Equivalente a Direct3DCreate9(SDKVersion). Retorna um IDirect3D9* do header
// d3d9.h vendido em jni/dxvk/include/native/directx (ABI identica a que a
// bridge do runtime usa para chamar o host).
NVR_EXPORT void *nvr_Direct3DCreate9(uint32_t sdk_version);

// Diretorio de cache (pipeline cache, cache de shaders SPIR-V). Deve apontar
// para uma pasta gravavel (no aparelho: <pasta do jogo>/port/native-render).
// Chamado pelo runtime antes do primeiro CreateDevice; opcional.
NVR_EXPORT void nvr_set_cache_dir(const char *dir);

// Versao do renderizador, para o log do jogo (port/log.txt).
NVR_EXPORT const char *nvr_version_string(void);
}
