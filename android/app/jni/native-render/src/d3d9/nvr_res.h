// Classes COM completas (todas as vtables D3D9). Metodos triviais inline;
// os complexos (locks, present blit, uploads) em nvr_res.cpp.
#pragma once
#include "nvr_internal.h"

namespace nvr {

class NrDevice;
class NrVolume;

// ---------------------------------------------------------------- base
struct ResRefs {
    std::atomic<uint32_t> refs{1};
    void *owner = nullptr;   // NrDevice*
    DWORD priority = 0;
};

// ------------------------------------------------------------- NrSurface
class NrSurface final : public IDirect3DSurface9, public ResRefs {
public:
    NrDevice *dev() { return (NrDevice *)owner; }
    SurfCore core;
    std::function<void()> destroy_cb;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs;
        if (!r) { if (destroy_cb) destroy_cb(); delete this; }
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **out) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *size) override;
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override;
    DWORD STDMETHODCALLTYPE SetPriority(DWORD p) override { priority = p; return p; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return priority; }
    void STDMETHODCALLTYPE PreLoad() override {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_SURFACE; }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DSURFACE_DESC *out) override;
    HRESULT STDMETHODCALLTYPE LockRect(D3DLOCKED_RECT *out, const RECT *rect, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect() override;
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID riid, void **out) override;
    HRESULT STDMETHODCALLTYPE GetDC(HDC *) override;
    HRESULT STDMETHODCALLTYPE ReleaseDC(HDC) override;
    uint8_t *sysmem_data() { return core.sysmem.empty() ? nullptr : core.sysmem.data(); }
    uint8_t *sysmem_data_resize(size_t n) {
        core.sysmem.resize(n);
        return core.sysmem.data();
    }
};

// ------------------------------------------------------------ NrTexture2D
class NrTexture2D final : public IDirect3DTexture9, public ResRefs {
public:
    NrDevice *dev() { return (NrDevice *)owner; }
    TexCore core;
    std::vector<NrSurface *> levels;
    std::function<void()> destroy_cb;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs;
        if (!r) { if (destroy_cb) destroy_cb(); delete this; }
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **out) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *size) override;
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override;
    DWORD STDMETHODCALLTYPE SetPriority(DWORD p) override { priority = p; return p; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return priority; }
    void STDMETHODCALLTYPE PreLoad() override {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_TEXTURE; }
    DWORD STDMETHODCALLTYPE SetLOD(DWORD lod) override { core.autogen_lod = lod; return lod; }
    DWORD STDMETHODCALLTYPE GetLOD() override { return core.autogen_lod; }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return core.levels; }
    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE f) override { core.autogen_filter = f; return D3D_OK; }
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override { return core.autogen_filter; }
    void STDMETHODCALLTYPE GenerateMipSubLevels() override;
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC *out) override;
    HRESULT STDMETHODCALLTYPE GetSurfaceLevel(UINT level, IDirect3DSurface9 **out) override;
    HRESULT STDMETHODCALLTYPE LockRect(UINT level, D3DLOCKED_RECT *out, const RECT *rect, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect(UINT level) override;
    HRESULT STDMETHODCALLTYPE AddDirtyRect(const RECT *rect) override;
};

// ---------------------------------------------------------- NrTextureCube
class NrTextureCube final : public IDirect3DCubeTexture9, public ResRefs {
public:
    NrDevice *dev() { return (NrDevice *)owner; }
    TexCore core;
    std::vector<NrSurface *> faces;   // face * levels + level
    std::function<void()> destroy_cb;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs;
        if (!r) { if (destroy_cb) destroy_cb(); delete this; }
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **out) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *size) override;
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override;
    DWORD STDMETHODCALLTYPE SetPriority(DWORD p) override { priority = p; return p; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return priority; }
    void STDMETHODCALLTYPE PreLoad() override {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_CUBETEXTURE; }
    DWORD STDMETHODCALLTYPE SetLOD(DWORD lod) override { core.autogen_lod = lod; return lod; }
    DWORD STDMETHODCALLTYPE GetLOD() override { return core.autogen_lod; }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return core.levels; }
    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE f) override { core.autogen_filter = f; return D3D_OK; }
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override { return core.autogen_filter; }
    void STDMETHODCALLTYPE GenerateMipSubLevels() override;
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DSURFACE_DESC *out) override;
    HRESULT GetLevelDescImpl(UINT level, D3DSURFACE_DESC *out);
    HRESULT STDMETHODCALLTYPE GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface9 **out) override;
    HRESULT STDMETHODCALLTYPE LockRect(D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT *out,
                                       const RECT *rect, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE UnlockRect(D3DCUBEMAP_FACES face, UINT level) override;
    HRESULT STDMETHODCALLTYPE AddDirtyRect(D3DCUBEMAP_FACES face, const RECT *rect) override;
};

// ---------------------------------------------------------- NrTextureVol
class NrTextureVol final : public IDirect3DVolumeTexture9, public ResRefs {
public:
    NrDevice *dev() { return (NrDevice *)owner; }
    TexCore core;
    std::vector<NrVolume *> volumes;
    std::function<void()> destroy_cb;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs;
        if (!r) { if (destroy_cb) destroy_cb(); delete this; }
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **out) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *size) override;
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override;
    DWORD STDMETHODCALLTYPE SetPriority(DWORD p) override { priority = p; return p; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return priority; }
    void STDMETHODCALLTYPE PreLoad() override {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_VOLUMETEXTURE; }
    DWORD STDMETHODCALLTYPE SetLOD(DWORD lod) override { core.autogen_lod = lod; return lod; }
    DWORD STDMETHODCALLTYPE GetLOD() override { return core.autogen_lod; }
    DWORD STDMETHODCALLTYPE GetLevelCount() override { return core.levels; }
    HRESULT STDMETHODCALLTYPE SetAutoGenFilterType(D3DTEXTUREFILTERTYPE f) override { core.autogen_filter = f; return D3D_OK; }
    D3DTEXTUREFILTERTYPE STDMETHODCALLTYPE GetAutoGenFilterType() override { return core.autogen_filter; }
    void STDMETHODCALLTYPE GenerateMipSubLevels() override;
    HRESULT STDMETHODCALLTYPE GetLevelDesc(UINT level, D3DVOLUME_DESC *out) override;
    HRESULT STDMETHODCALLTYPE GetVolumeLevel(UINT level, IDirect3DVolume9 **out) override;
    HRESULT STDMETHODCALLTYPE LockBox(UINT level, D3DLOCKED_BOX *out, const D3DBOX *box, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE UnlockBox(UINT level) override;
    HRESULT STDMETHODCALLTYPE AddDirtyBox(const D3DBOX *box) override;
};

// --------------------------------------------------------------- NrVolume
class NrVolume final : public IDirect3DVolume9, public ResRefs {
public:
    NrDevice *dev() { return (NrDevice *)owner; }
    TexCore *tex = nullptr;
    uint32_t level = 0;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs;
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **out) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *size) override;
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override;
    HRESULT STDMETHODCALLTYPE GetDesc(D3DVOLUME_DESC *out) override;
    HRESULT STDMETHODCALLTYPE LockBox(D3DLOCKED_BOX *out, const D3DBOX *box, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE UnlockBox() override;
    HRESULT STDMETHODCALLTYPE GetContainer(REFIID riid, void **out) override;
};

// -------------------------------------------------------------- NrVB/NrIB
class NrVB final : public IDirect3DVertexBuffer9, public ResRefs {
public:
    NrDevice *dev() { return (NrDevice *)owner; }
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    void *mapped = nullptr;
    uint32_t size = 0;
    DWORD usage = 0, fvf = 0;
    D3DPOOL pool = D3DPOOL_DEFAULT;
    bool needs_upload = false;
    std::vector<uint8_t> shadow;
    StagingRing::Alloc ring_alloc{};
    uint32_t lock_off = 0, lock_size = 0;
    std::function<void()> destroy_cb;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs;
        if (!r) { if (destroy_cb) destroy_cb(); delete this; }
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **out) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *size) override;
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override;
    DWORD STDMETHODCALLTYPE SetPriority(DWORD p) override { priority = p; return p; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return priority; }
    void STDMETHODCALLTYPE PreLoad() override {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_VERTEXBUFFER; }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DVERTEXBUFFER_DESC *out) override;
    HRESULT STDMETHODCALLTYPE Lock(UINT off, UINT size, void **data, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE Unlock() override;
};
class NrIB final : public IDirect3DIndexBuffer9, public ResRefs {
public:
    NrDevice *dev() { return (NrDevice *)owner; }
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    void *mapped = nullptr;
    uint32_t size = 0;
    D3DFORMAT fmt = D3DFMT_INDEX16;
    DWORD usage = 0;
    D3DPOOL pool = D3DPOOL_DEFAULT;
    bool needs_upload = false;
    std::vector<uint8_t> shadow;
    StagingRing::Alloc ring_alloc{};
    uint32_t lock_off = 0, lock_size = 0;
    std::function<void()> destroy_cb;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs;
        if (!r) { if (destroy_cb) destroy_cb(); delete this; }
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **out) override;
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID, const void *, DWORD, DWORD) override;
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID, void *, DWORD *size) override;
    HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID) override;
    DWORD STDMETHODCALLTYPE SetPriority(DWORD p) override { priority = p; return p; }
    DWORD STDMETHODCALLTYPE GetPriority() override { return priority; }
    void STDMETHODCALLTYPE PreLoad() override {}
    D3DRESOURCETYPE STDMETHODCALLTYPE GetType() override { return D3DRTYPE_INDEXBUFFER; }
    HRESULT STDMETHODCALLTYPE GetDesc(D3DINDEXBUFFER_DESC *out) override;
    HRESULT STDMETHODCALLTYPE Lock(UINT off, UINT size, void **data, DWORD flags) override;
    HRESULT STDMETHODCALLTYPE Unlock() override;
};

// -------------------------------------------------------- Decl/VS/PS/etc
class NrDecl final : public IDirect3DVertexDeclaration9, public ResRefs {
public:
    std::vector<D3DVERTEXELEMENT9> elems;
    DWORD fvf = 0;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs;
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **out) override;
    HRESULT STDMETHODCALLTYPE GetDeclaration(D3DVERTEXELEMENT9 *elems, UINT *count) override;
};
class NrVS final : public IDirect3DVertexShader9, public ResRefs {
public:
    std::vector<uint8_t> bytecode;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs;
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **out) override;
    HRESULT STDMETHODCALLTYPE GetFunction(void *data, UINT *size) override;
};
class NrPS final : public IDirect3DPixelShader9, public ResRefs {
public:
    std::vector<uint8_t> bytecode;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs;
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **out) override;
    HRESULT STDMETHODCALLTYPE GetFunction(void *data, UINT *size) override;
};
class NrStateBlock final : public IDirect3DStateBlock9, public RefBase {
public:
    std::vector<uint8_t> data;
    ~NrStateBlock();
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs;
        if (!r) delete this;
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **out) override;
    HRESULT STDMETHODCALLTYPE Capture() override;
    HRESULT STDMETHODCALLTYPE Apply() override;
};
class NrQuery final : public IDirect3DQuery9, public RefBase {
public:
    D3DQUERYTYPE type = D3DQUERYTYPE_OCCLUSION;
    uint32_t pool_slot = 0xFFFFFFFFu;
    uint32_t query_idx = 0;
    bool issued = false;
    bool m_begun = false;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG r = --refs;
        if (!r) { if (release_cb) release_cb(); delete this; }
        return r;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
    HRESULT STDMETHODCALLTYPE GetDevice(IDirect3DDevice9 **out) override;
    HRESULT STDMETHODCALLTYPE GetData(void *data, DWORD size, DWORD flags) override;
    DWORD STDMETHODCALLTYPE GetDataSize() override;
    HRESULT STDMETHODCALLTYPE Issue(DWORD flags) override;
    D3DQUERYTYPE STDMETHODCALLTYPE GetType() override { return type; }
    std::function<void()> release_cb;
};

} // namespace nvr
