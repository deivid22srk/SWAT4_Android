// Definicoes COM completas das classes de recurso (todas as vtables D3D9).
// Implementacoes triviais inline; as complexas ficam em nvr_res.cpp.
#pragma once
#include "nvr_internal.h"

namespace nvr {

#define NVR_QI_SELF(I) \
    if (riid == __uuidof(I) || riid == __uuidof(IUnknown)) { \
        *out = (I *)this; \
        AddRef(); \
        return S_OK; \
    } \
    *out = nullptr; \
    return E_NOINTERFACE;

// ------------------------------------------------------------- NrSurface
class NrSurface;
// (classe definida em nvr_internal.h; metodos complexos em nvr_res.cpp)

#define NVR_RES_COMMON(I) \
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **out) override { \
        if (!out) return D3DERR_INVALIDCALL; \
        *out = dev; \
        dev->AddRef(); \
        return D3D_OK; \
    } \
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) override { return D3D_OK; } \
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *size) override { \
        if (size) *size = 0; \
        return D3DERR_NOTFOUND; \
    } \
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override { return D3D_OK; } \
    DWORD STDMETHODCALLTYPE SetPriority(DWORD p) override { priority = p; return p; } \
    DWORD STDMETHODCALLTYPE GetPriority() override { return priority; } \
    void STDMETHODCALLTYPE PreLoad() override {} \
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return res_type; } \
    DWORD priority = 0; \
    D3DRESOURCETYPE res_type = D3DRTYPE_SURFACE;

// NrSurface: implementada em nvr_res.cpp (LockRect/GetDesc/GetDC...)
// NrTexture2D/Cube/Vol: idem. As classes ficam declaradas em nvr_internal.h
// e ganham aqui metodos auxiliares comuns via heranca de implementacao.

// helpers compartilhados usados pelos .cpp
uint32_t d3d9_fmt_bytes(D3DFORMAT fmt);
VkFormat tex_vk_format(D3DFORMAT fmt, bool bc_ok);
DWORD pack_raster_state(const DeviceState &st);
DWORD pack_ds_state(const DeviceState &st);
DWORD pack_blend_state(const DeviceState &st);

} // namespace nvr
