// Recursos D3D9: criacao, locks, uploads, blits (StretchRect/UpdateSurface/
// UpdateTexture/GetRenderTargetData/GetFrontBufferData/ColorFill), present
// blit (escala+gamma+rotacao), queries e state blocks.
#include "nvr_device.h"
#include "nvr_com_impl.h"
#include <algorithm>
#include <SDL2/SDL.h>

namespace nvr {

// ---------------------------------------------------------------- helpers
VkFormat tex_vk_format(D3DFORMAT fmt, bool bc_ok) {
    const FormatInfo &fi = format_info(fmt);
    if (fi.vk == VK_FORMAT_UNDEFINED) return VK_FORMAT_UNDEFINED;
    if (!bc_ok && fi.bytes_per_block >= 8 && fi.block_width == 4) return VK_FORMAT_R8G8B8A8_UNORM;
    return fi.vk;
}

void TexCore::compute_level_layout() {
    const FormatInfo &fi = format_info(fmt);
    size_t off = 0;
    for (uint32_t l = 0; l < levels && l < 16; l++) {
        uint32_t lw = std::max(1u, w >> l), lh = std::max(1u, h >> l);
        uint32_t bw = (lw + fi.block_width - 1) / fi.block_width;
        uint32_t bh = (lh + fi.block_height - 1) / fi.block_height;
        level_pitch[l] = bw * fi.bytes_per_block;
        level_offset[l] = off;
        off += (size_t)level_pitch[l] * bh * (type == D3DRTYPE_VOLUMETEXTURE ? std::max(1u, d >> l) : 1);
    }
    sysmem.resize(off);
}

size_t TexCore::level_size(uint32_t l) const {
    if (l + 1 < 16 && l + 1 < levels) return level_offset[l + 1] - level_offset[l];
    return sysmem.size() - level_offset[l];
}

// ------------------------------------------------------------ QI (COM)
HRESULT NrSurface::QueryInterface(REFIID riid, void **out) { NVR_QI_SELF(IDirect3DSurface9) }
HRESULT NrTexture2D::QueryInterface(REFIID riid, void **out) { NVR_QI_SELF(IDirect3DTexture9) }
HRESULT NrTextureCube::QueryInterface(REFIID riid, void **out) { NVR_QI_SELF(IDirect3DCubeTexture9) }
HRESULT NrTextureVol::QueryInterface(REFIID riid, void **out) { NVR_QI_SELF(IDirect3DVolumeTexture9) }
HRESULT NrVolume::QueryInterface(REFIID riid, void **out) { NVR_QI_SELF(IDirect3DVolume9) }
HRESULT NrVB::QueryInterface(REFIID riid, void **out) { NVR_QI_SELF(IDirect3DVertexBuffer9) }
HRESULT NrIB::QueryInterface(REFIID riid, void **out) { NVR_QI_SELF(IDirect3DIndexBuffer9) }
HRESULT NrDecl::QueryInterface(REFIID riid, void **out) { NVR_QI_SELF(IDirect3DVertexDeclaration9) }
HRESULT NrVS::QueryInterface(REFIID riid, void **out) { NVR_QI_SELF(IDirect3DVertexShader9) }
HRESULT NrPS::QueryInterface(REFIID riid, void **out) { NVR_QI_SELF(IDirect3DPixelShader9) }
HRESULT NrStateBlock::QueryInterface(REFIID riid, void **out) { NVR_QI_SELF(IDirect3DStateBlock9) }
HRESULT NrQuery::QueryInterface(REFIID riid, void **out) { NVR_QI_SELF(IDirect3DQuery9) }

HRESULT NrSurface::GetDevice(IDirect3DDevice9 **out) { *out = (IDirect3DDevice9 *)owner; ((IDirect3DDevice9 *)owner)->AddRef(); return D3D_OK; }
HRESULT NrTexture2D::GetDevice(IDirect3DDevice9 **out) { *out = (IDirect3DDevice9 *)owner; ((IDirect3DDevice9 *)owner)->AddRef(); return D3D_OK; }
HRESULT NrTextureCube::GetDevice(IDirect3DDevice9 **out) { *out = (IDirect3DDevice9 *)owner; ((IDirect3DDevice9 *)owner)->AddRef(); return D3D_OK; }
HRESULT NrTextureVol::GetDevice(IDirect3DDevice9 **out) { *out = (IDirect3DDevice9 *)owner; ((IDirect3DDevice9 *)owner)->AddRef(); return D3D_OK; }
HRESULT NrVolume::GetDevice(IDirect3DDevice9 **out) { *out = (IDirect3DDevice9 *)owner; ((IDirect3DDevice9 *)owner)->AddRef(); return D3D_OK; }
HRESULT NrVB::GetDevice(IDirect3DDevice9 **out) { *out = (IDirect3DDevice9 *)owner; ((IDirect3DDevice9 *)owner)->AddRef(); return D3D_OK; }
HRESULT NrIB::GetDevice(IDirect3DDevice9 **out) { *out = (IDirect3DDevice9 *)owner; ((IDirect3DDevice9 *)owner)->AddRef(); return D3D_OK; }
HRESULT NrDecl::GetDevice(IDirect3DDevice9 **out) { *out = (IDirect3DDevice9 *)owner; ((IDirect3DDevice9 *)owner)->AddRef(); return D3D_OK; }
HRESULT NrVS::GetDevice(IDirect3DDevice9 **out) { *out = (IDirect3DDevice9 *)owner; ((IDirect3DDevice9 *)owner)->AddRef(); return D3D_OK; }
HRESULT NrPS::GetDevice(IDirect3DDevice9 **out) { *out = (IDirect3DDevice9 *)owner; ((IDirect3DDevice9 *)owner)->AddRef(); return D3D_OK; }
HRESULT NrStateBlock::GetDevice(IDirect3DDevice9 **out) { *out = (IDirect3DDevice9 *)owner; ((IDirect3DDevice9 *)owner)->AddRef(); return D3D_OK; }
HRESULT NrQuery::GetDevice(IDirect3DDevice9 **out) { *out = (IDirect3DDevice9 *)owner; ((IDirect3DDevice9 *)owner)->AddRef(); return D3D_OK; }

// trivial private data
HRESULT NrSurface::SetPrivateData(REFGUID, const void *, DWORD, DWORD) { return D3D_OK; }
HRESULT NrSurface::GetPrivateData(REFGUID, void *, DWORD *s) { if (s) *s = 0; return D3DERR_NOTFOUND; }
HRESULT NrSurface::FreePrivateData(REFGUID) { return D3D_OK; }
HRESULT NrTexture2D::SetPrivateData(REFGUID, const void *, DWORD, DWORD) { return D3D_OK; }
HRESULT NrTexture2D::GetPrivateData(REFGUID, void *, DWORD *s) { if (s) *s = 0; return D3DERR_NOTFOUND; }
HRESULT NrTexture2D::FreePrivateData(REFGUID) { return D3D_OK; }
HRESULT NrTextureCube::SetPrivateData(REFGUID, const void *, DWORD, DWORD) { return D3D_OK; }
HRESULT NrTextureCube::GetPrivateData(REFGUID, void *, DWORD *s) { if (s) *s = 0; return D3DERR_NOTFOUND; }
HRESULT NrTextureCube::FreePrivateData(REFGUID) { return D3D_OK; }
HRESULT NrTextureVol::SetPrivateData(REFGUID, const void *, DWORD, DWORD) { return D3D_OK; }
HRESULT NrTextureVol::GetPrivateData(REFGUID, void *, DWORD *s) { if (s) *s = 0; return D3DERR_NOTFOUND; }
HRESULT NrTextureVol::FreePrivateData(REFGUID) { return D3D_OK; }
HRESULT NrVolume::SetPrivateData(REFGUID, const void *, DWORD, DWORD) { return D3D_OK; }
HRESULT NrVolume::GetPrivateData(REFGUID, void *, DWORD *s) { if (s) *s = 0; return D3DERR_NOTFOUND; }
HRESULT NrVolume::FreePrivateData(REFGUID) { return D3D_OK; }
HRESULT NrVB::SetPrivateData(REFGUID, const void *, DWORD, DWORD) { return D3D_OK; }
HRESULT NrVB::GetPrivateData(REFGUID, void *, DWORD *s) { if (s) *s = 0; return D3DERR_NOTFOUND; }
HRESULT NrVB::FreePrivateData(REFGUID) { return D3D_OK; }
HRESULT NrIB::SetPrivateData(REFGUID, const void *, DWORD, DWORD) { return D3D_OK; }
HRESULT NrIB::GetPrivateData(REFGUID, void *, DWORD *s) { if (s) *s = 0; return D3DERR_NOTFOUND; }
HRESULT NrIB::FreePrivateData(REFGUID) { return D3D_OK; }

// ------------------------------------------------------------ criacao
HRESULT NrDevice::CreateTexture(UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool,
                                IDirect3DTexture9 **out, HANDLE *) {
    if (!out) return D3DERR_INVALIDCALL;
    *out = nullptr;
    if (!w || !h || pool == D3DPOOL_SCRATCH) return D3DERR_INVALIDCALL;
    bool bc_ok = m_vk->has_bc();
    VkFormat vkf = tex_vk_format(fmt, bc_ok);
    const FormatInfo &fi = format_info(fmt);
    if (vkf == VK_FORMAT_UNDEFINED) { NVR_WARN("CreateTexture: formato %d sem mapa", fmt); return D3DERR_INVALIDCALL; }
    if (!levels) levels = (uint32_t)floorf(log2f((float)std::max(w, h))) + 1;
    auto *t = new NrTexture2D();
    t->owner = (void *)this;
    TexCore &c = t->core;
    c.type = D3DRTYPE_TEXTURE;
    c.w = w; c.h = h; c.levels = levels;
    c.fmt = fmt;
    c.vkfmt = vkf;
    c.usage = usage;
    c.pool = pool;
    c.autogen = (usage & D3DUSAGE_AUTOGENMIPMAP) != 0;
    c.is_depth = false;
    if (pool != D3DPOOL_DEFAULT || (usage & D3DUSAGE_DYNAMIC) || (usage & D3DUSAGE_RENDERTARGET)) {
        c.compute_level_layout();
    }
    if (pool != D3DPOOL_SYSTEMMEM) {
        bool is_rt = (usage & D3DUSAGE_RENDERTARGET) != 0;
        VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = vkf;
        ici.extent = {w, h, 1};
        ici.mipLevels = levels;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if (is_rt) ici.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        else ici.usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        c.alloc = mem_alloc_image(m_vk, ici, &c.image);
        if (!c.alloc) { NVR_ERROR("CreateTexture %ux%u falhou", w, h); delete t; return D3DERR_OUTOFVIDEOMEMORY; }
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = c.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = vkf;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
        vkCreateImageView(m_vk->device(), &vi, nullptr, &c.view_sampled);
        if (is_rt) {
            VkImageViewCreateInfo vi2 = vi;
            vi2.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCreateImageView(m_vk->device(), &vi2, nullptr, &c.view_rt);
        }
    }
    for (uint32_t l = 0; l < levels; l++) {
        auto *s = new NrSurface();
        s->owner = (void *)this;
        s->core.tex = &t->core;
        s->core.level = l;
        s->core.layer = 0;
        s->core.fmt = fmt;
        s->core.usage = usage;
        s->core.pool = pool;
        t->levels.push_back(s);
    }
    t->destroy_cb = [t, this]() {
        for (auto s : t->levels) s->Release();
        destroy_texcore(&t->core);
    };
    *out = t;
    NVR_TRACE("CreateTexture %ux%u l=%u fmt=%d pool=%d usage=%x", w, h, levels, fmt, pool, usage);
    return D3D_OK;
}

HRESULT NrDevice::CreateCubeTexture(UINT size, UINT levels, DWORD usage, D3DFORMAT fmt, D3DPOOL pool,
                                    IDirect3DCubeTexture9 **out, HANDLE *) {
    if (!out || !size) return D3DERR_INVALIDCALL;
    bool bc_ok = m_vk->has_bc();
    VkFormat vkf = tex_vk_format(fmt, bc_ok);
    if (vkf == VK_FORMAT_UNDEFINED) return D3DERR_INVALIDCALL;
    if (!levels) levels = (uint32_t)floorf(log2f((float)size)) + 1;
    auto *t = new NrTextureCube();
    t->owner = (void *)this;
    TexCore &c = t->core;
    c.type = D3DRTYPE_CUBETEXTURE;
    c.w = size; c.h = size; c.levels = levels;
    c.fmt = fmt;
    c.vkfmt = vkf;
    c.usage = usage;
    c.pool = pool;
    c.is_depth = false;
    if (pool != D3DPOOL_SYSTEMMEM) c.compute_level_layout();
    if (pool != D3DPOOL_SYSTEMMEM) {
        VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = vkf;
        ici.extent = {size, size, 1};
        ici.mipLevels = levels;
        ici.arrayLayers = 6;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        c.alloc = mem_alloc_image(m_vk, ici, &c.image);
        if (!c.alloc) { delete t; return D3DERR_OUTOFVIDEOMEMORY; }
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = c.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
        vi.format = vkf;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 6};
        vkCreateImageView(m_vk->device(), &vi, nullptr, &c.view_sampled);
    }
    for (uint32_t f = 0; f < 6; f++)
        for (uint32_t l = 0; l < levels; l++) {
            auto *s = new NrSurface();
            s->owner = (void *)this;
            s->core.tex = &t->core;
            s->core.level = l;
            s->core.layer = f;
            s->core.fmt = fmt;
            s->core.usage = usage;
            s->core.pool = pool;
            t->faces.push_back(s);
        }
    t->destroy_cb = [t, this]() {
        for (auto s : t->faces) s->Release();
        destroy_texcore(&t->core);
    };
    *out = t;
    return D3D_OK;
}

HRESULT NrDevice::CreateVolumeTexture(UINT w, UINT h, UINT d, UINT levels, DWORD usage, D3DFORMAT fmt,
                                      D3DPOOL pool, IDirect3DVolumeTexture9 **out, HANDLE *) {
    if (!out || !w || !h || !d) return D3DERR_INVALIDCALL;
    bool bc_ok = m_vk->has_bc();
    VkFormat vkf = tex_vk_format(fmt, bc_ok);
    if (vkf == VK_FORMAT_UNDEFINED) return D3DERR_INVALIDCALL;
    if (!levels) levels = (uint32_t)floorf(log2f((float)std::max({w, h, d}))) + 1;
    auto *t = new NrTextureVol();
    t->owner = (void *)this;
    TexCore &c = t->core;
    c.type = D3DRTYPE_VOLUMETEXTURE;
    c.w = w; c.h = h; c.d = d; c.levels = levels;
    c.fmt = fmt;
    c.vkfmt = vkf;
    c.usage = usage;
    c.pool = pool;
    c.compute_level_layout();
    if (pool != D3DPOOL_SYSTEMMEM) {
        VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ici.imageType = VK_IMAGE_TYPE_3D;
        ici.format = vkf;
        ici.extent = {w, h, d};
        ici.mipLevels = levels;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        c.alloc = mem_alloc_image(m_vk, ici, &c.image);
        if (!c.alloc) { delete t; return D3DERR_OUTOFVIDEOMEMORY; }
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = c.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_3D;
        vi.format = vkf;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, levels, 0, 1};
        vkCreateImageView(m_vk->device(), &vi, nullptr, &c.view_sampled);
    }
    for (uint32_t l = 0; l < levels; l++) {
        auto *v = new NrVolume();
        v->owner = (void *)this;
        v->tex = &t->core;
        v->level = l;
        t->volumes.push_back(v);
    }
    t->destroy_cb = [t, this]() {
        for (auto v : t->volumes) v->Release();
        destroy_texcore(&t->core);
    };
    *out = t;
    return D3D_OK;
}

HRESULT NrDevice::CreateRenderTarget(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms, DWORD quality,
                                     BOOL lockable, IDirect3DSurface9 **out, HANDLE *) {
    (void)ms; (void)quality; (void)lockable;
    if (!out || !w || !h) return D3DERR_INVALIDCALL;
    const FormatInfo &fi = format_info(fmt);
    if (fi.vk == VK_FORMAT_UNDEFINED) return D3DERR_INVALIDCALL;
    SurfCore sc;
    sc.w = w; sc.h = h;
    sc.fmt = fmt;
    sc.vkfmt = fi.vk;
    sc.usage = D3DUSAGE_RENDERTARGET;
    sc.pool = D3DPOOL_DEFAULT;
    sc.is_depth = false;
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = fi.vk;
    ici.extent = {w, h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    sc.alloc = mem_alloc_image(m_vk, ici, &sc.image);
    if (!sc.alloc) return D3DERR_OUTOFVIDEOMEMORY;
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = sc.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = fi.vk;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(m_vk->device(), &vi, nullptr, &sc.view_rt);
    VkImageViewCreateInfo vi2 = vi;
    vi2.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(m_vk->device(), &vi2, nullptr, &sc.view_sampled);
    *out = wrap_standalone(std::move(sc));
    return D3D_OK;
}

HRESULT NrDevice::CreateDepthStencilSurface(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms,
                                            DWORD quality, BOOL discard_z, IDirect3DSurface9 **out,
                                            HANDLE *) {
    (void)ms; (void)quality; (void)discard_z;
    if (!out || !w || !h) return D3DERR_INVALIDCALL;
    SurfCore sc;
    sc.w = w; sc.h = h;
    sc.fmt = fmt;
    sc.is_depth = true;
    sc.pool = D3DPOOL_DEFAULT;
    bool stencil = false;
    sc.actual_fmt = choose_depth_format(m_vk->physical(), fmt, &stencil);
    sc.has_stencil = stencil;
    if (sc.actual_fmt == VK_FORMAT_UNDEFINED) return D3DERR_NOTAVAILABLE;
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = sc.actual_fmt;
    ici.extent = {w, h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    sc.alloc = mem_alloc_image(m_vk, ici, &sc.image);
    if (!sc.alloc) return D3DERR_OUTOFVIDEOMEMORY;
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = sc.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = sc.actual_fmt;
    vi.subresourceRange = {(VkImageAspectFlags)(stencil ? (VK_IMAGE_ASPECT_DEPTH_BIT |
                                                                        VK_IMAGE_ASPECT_STENCIL_BIT)
                                                                        : VK_IMAGE_ASPECT_DEPTH_BIT),
                           0, 1, 0, 1};
    vkCreateImageView(m_vk->device(), &vi, nullptr, &sc.view_ds);
    *out = wrap_standalone(std::move(sc));
    return D3D_OK;
}

HRESULT NrDevice::CreateOffscreenPlainSurface(UINT w, UINT h, D3DFORMAT fmt, D3DPOOL pool,
                                             IDirect3DSurface9 **out, HANDLE *) {
    if (!out || !w || !h) return D3DERR_INVALIDCALL;
    const FormatInfo &fi = format_info(fmt);
    if (fi.vk == VK_FORMAT_UNDEFINED) return D3DERR_INVALIDCALL;
    SurfCore sc;
    sc.w = w; sc.h = h;
    sc.fmt = fmt;
    sc.vkfmt = fi.vk;
    sc.pool = pool;
    sc.usage = 0;
    if (pool != D3DPOOL_SYSTEMMEM) {
        VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = fi.vk;
        ici.extent = {w, h, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        sc.alloc = mem_alloc_image(m_vk, ici, &sc.image);
        if (!sc.alloc) return D3DERR_OUTOFVIDEOMEMORY;
    } else {
        sc.alloc = VK_NULL_HANDLE;
        // sysmem: sem imagem; dados no shadow do wrapper
    }
    *out = wrap_standalone(std::move(sc));
    return D3D_OK;
}

NrSurface *NrDevice::wrap_standalone(SurfCore core) {
    auto *s = new NrSurface();
    s->owner = (void *)this;
    s->core = std::move(core);
    s->destroy_cb = [s2 = s, this]() { destroy_surface(const_cast<NrSurface *>(s2)); };
    return s;
}

void NrDevice::destroy_surface(NrSurface *s) {
    if (!s) return;
    SurfCore &c = s->core;
    // framebuffers referenciando as views morrem antes das views
    if (c.view_rt) m_framebuffers.forget_surface(c.view_rt);
    if (c.view_ds) m_framebuffers.forget_surface(c.view_ds);
    if (c.view_sampled) m_framebuffers.forget_surface(c.view_sampled);
    VkDevice dev = m_vk->device();
    if (c.image) {
        mem_free_image(m_vk, c.image, c.alloc);
        c.image = VK_NULL_HANDLE;
    }
    if (c.view_sampled) { vkDestroyImageView(dev, c.view_sampled, nullptr); c.view_sampled = VK_NULL_HANDLE; }
    if (c.view_rt) { vkDestroyImageView(dev, c.view_rt, nullptr); c.view_rt = VK_NULL_HANDLE; }
    if (c.view_ds) { vkDestroyImageView(dev, c.view_ds, nullptr); c.view_ds = VK_NULL_HANDLE; }
}

void NrDevice::destroy_texcore(TexCore *t) {
    VkDevice dev = m_vk->device();
    if (t->image) { mem_free_image(m_vk, t->image, t->alloc); t->image = VK_NULL_HANDLE; }
    if (t->view_sampled) { vkDestroyImageView(dev, t->view_sampled, nullptr); t->view_sampled = VK_NULL_HANDLE; }
    if (t->view_rt) { vkDestroyImageView(dev, t->view_rt, nullptr); t->view_rt = VK_NULL_HANDLE; }
}

void NrDevice::destroy_buffer(VkBuffer buf, VmaAllocation a) {
    if (buf) vkDestroyBuffer(m_vk->device(), buf, nullptr);
    if (a) mem_free(m_vk, a);
}

// -------------------------------------------------------------- descriptors
HRESULT NrSurface::GetDesc(D3DSURFACE_DESC *out) {
    if (!out) return D3DERR_INVALIDCALL;
    memset(out, 0, sizeof *out);
    out->Format = core.fmt;
    out->Type = D3DRTYPE_SURFACE;
    out->Usage = core.usage;
    out->Pool = core.pool;
    out->MultiSampleType = D3DMULTISAMPLE_NONE;
    out->Width = core.width();
    out->Height = core.height();
    return D3D_OK;
}
HRESULT NrTexture2D::GetLevelDesc(UINT level, D3DSURFACE_DESC *out) {
    if (level >= core.levels || !out) return D3DERR_INVALIDCALL;
    memset(out, 0, sizeof *out);
    out->Format = core.fmt;
    out->Type = D3DRTYPE_SURFACE;
    out->Usage = core.usage;
    out->Pool = core.pool;
    out->Width = std::max(1u, core.w >> level);
    out->Height = std::max(1u, core.h >> level);
    return D3D_OK;
}
HRESULT NrTextureCube::GetLevelDesc(UINT level, D3DSURFACE_DESC *out) { return GetLevelDescImpl(level, out); }
HRESULT NrTextureCube::AddDirtyRect(D3DCUBEMAP_FACES, const RECT *) { core.whole_dirty = true; return D3D_OK; }
HRESULT NrTexture2D::AddDirtyRect(const RECT *) { core.whole_dirty = true; return D3D_OK; }
HRESULT NrTextureVol::AddDirtyBox(const D3DBOX *) { core.whole_dirty = true; return D3D_OK; }

HRESULT NrTexture2D::GetSurfaceLevel(UINT level, IDirect3DSurface9 **out) {
    if (level >= core.levels || !out) return D3DERR_INVALIDCALL;
    *out = levels[level];
    (*out)->AddRef();
    return D3D_OK;
}
HRESULT NrTextureCube::GetCubeMapSurface(D3DCUBEMAP_FACES face, UINT level, IDirect3DSurface9 **out) {
    if ((uint32_t)face >= 6 || level >= core.levels || !out) return D3DERR_INVALIDCALL;
    *out = faces[(uint32_t)face * core.levels + level];
    (*out)->AddRef();
    return D3D_OK;
}
HRESULT NrTextureVol::GetVolumeLevel(UINT level, IDirect3DVolume9 **out) {
    if (level >= core.levels || !out) return D3DERR_INVALIDCALL;
    *out = volumes[level];
    (*out)->AddRef();
    return D3D_OK;
}
void NrTexture2D::GenerateMipSubLevels() {
    if (!core.image) return;
    core.needs_upload = true;   // re-upload + mipgen no proximo uso
}
void NrTextureCube::GenerateMipSubLevels() { core.needs_upload = true; }
void NrTextureVol::GenerateMipSubLevels() { core.needs_upload = true; }

HRESULT NrTextureVol::GetLevelDesc(UINT level, D3DVOLUME_DESC *out) {
    if (level >= core.levels || !out) return D3DERR_INVALIDCALL;
    memset(out, 0, sizeof *out);
    out->Format = core.fmt;
    out->Type = D3DRTYPE_VOLUME;
    out->Usage = core.usage;
    out->Pool = core.pool;
    out->Width = std::max(1u, core.w >> level);
    out->Height = std::max(1u, core.h >> level);
    out->Depth = std::max(1u, core.d >> level);
    return D3D_OK;
}
HRESULT NrVolume::GetDesc(D3DVOLUME_DESC *out) {
    if (!out || !tex) return D3DERR_INVALIDCALL;
    memset(out, 0, sizeof *out);
    out->Format = tex->fmt;
    out->Type = D3DRTYPE_VOLUME;
    out->Usage = tex->usage;
    out->Pool = tex->pool;
    out->Width = std::max(1u, tex->w >> level);
    out->Height = std::max(1u, tex->h >> level);
    out->Depth = std::max(1u, tex->d >> level);
    return D3D_OK;
}
HRESULT NrSurface::GetContainer(REFIID riid, void **out) {
    (void)riid; (void)out;
    return D3DERR_NOTFOUND;   // surf standalone nao tem container
}
HRESULT NrVolume::GetContainer(REFIID riid, void **out) {
    (void)riid; (void)out;
    return D3DERR_NOTFOUND;
}
HRESULT NrSurface::GetDC(HDC *) { return D3DERR_INVALIDCALL; }
HRESULT NrSurface::ReleaseDC(HDC) { return D3DERR_INVALIDCALL; }

// helper do GetLevelDesc do cube (evita duplicacao)
HRESULT NrTextureCube::GetLevelDescImpl(UINT level, D3DSURFACE_DESC *out) {
    if (level >= core.levels || !out) return D3DERR_INVALIDCALL;
    memset(out, 0, sizeof *out);
    out->Format = core.fmt;
    out->Type = D3DRTYPE_SURFACE;
    out->Usage = core.usage;
    out->Pool = core.pool;
    out->Width = std::max(1u, core.w >> level);
    out->Height = std::max(1u, core.h >> level);
    return D3D_OK;
}

// ------------------------------------------------------------------ locks
// MANAGED/SYSTEMMEM: shadow e a fonte (pointer direto, dirty no unlock).
// DEFAULT dinamico: ring do frame corrente, upload no unlock.

HRESULT NrTexture2D::LockRect(UINT level, D3DLOCKED_RECT *out, const RECT *rect, DWORD flags) {
    if (level >= core.levels || !out) return D3DERR_INVALIDCALL;
    if (core.usage & D3DUSAGE_RENDERTARGET && core.pool == D3DPOOL_DEFAULT) return D3DERR_INVALIDCALL;
    if (core.sysmem.empty()) core.compute_level_layout();
    uint32_t lw = std::max(1u, core.w >> level);
    uint32_t lh = std::max(1u, core.h >> level);
    uint8_t *base = core.sysmem.data() + core.level_offset[level];
    out->Pitch = (INT)core.level_pitch[level];
    out->pBits = base;
    if (rect)
        out->pBits = base + (size_t)rect->top * core.level_pitch[level] +
                     rect->left * (core.level_pitch[level] / std::max(1u, lw));
    core.dirty[level] = !(flags & D3DLOCK_READONLY);
    core.whole_dirty = !(flags & D3DLOCK_READONLY);
    (void)lh;
    return D3D_OK;
}
HRESULT NrTexture2D::UnlockRect(UINT level) {
    if (level >= core.levels) return D3DERR_INVALIDCALL;
    if (core.pool == D3DPOOL_DEFAULT && core.image) {
        dev()->upload_texcore(core, false);
    }
    return D3D_OK;
}
HRESULT NrTextureCube::LockRect(D3DCUBEMAP_FACES face, UINT level, D3DLOCKED_RECT *out, const RECT *rect,
                                DWORD flags) {
    if ((uint32_t)face >= 6 || level >= core.levels || !out) return D3DERR_INVALIDCALL;
    if (core.sysmem.empty()) core.compute_level_layout();
    uint8_t *base = core.sysmem.data() + core.level_offset[level] +
                    (size_t)face * (core.sysmem.size() - core.level_offset[level]) / 6;
    out->Pitch = (INT)core.level_pitch[level];
    out->pBits = base;
    core.dirty[level] = !(flags & D3DLOCK_READONLY);
    core.whole_dirty = !(flags & D3DLOCK_READONLY);
    (void)rect;
    return D3D_OK;
}
HRESULT NrTextureCube::UnlockRect(D3DCUBEMAP_FACES, UINT level) {
    if (level >= core.levels) return D3DERR_INVALIDCALL;
    if (core.pool == D3DPOOL_DEFAULT && core.image) dev()->upload_texcore(core, false);
    return D3D_OK;
}
HRESULT NrTextureVol::LockBox(UINT level, D3DLOCKED_BOX *out, const D3DBOX *box, DWORD flags) {
    if (level >= core.levels || !out) return D3DERR_INVALIDCALL;
    if (core.sysmem.empty()) core.compute_level_layout();
    uint32_t lw = std::max(1u, core.w >> level);
    uint32_t lh = std::max(1u, core.h >> level);
    const FormatInfo &fi = format_info(core.fmt);
    uint32_t row_pitch = ((lw + fi.block_width - 1) / fi.block_width) * fi.bytes_per_block;
    uint32_t slice_pitch = row_pitch * lh;
    out->RowPitch = (INT)row_pitch;
    out->SlicePitch = (INT)slice_pitch;
    out->pBits = core.sysmem.data() + core.level_offset[level];
    if (box)
        out->pBits = (uint8_t *)out->pBits + (size_t)box->Front * slice_pitch +
                     (size_t)box->Top * row_pitch + box->Left * (row_pitch / std::max(1u, lw));
    core.dirty[level] = !(flags & D3DLOCK_READONLY);
    core.whole_dirty = !(flags & D3DLOCK_READONLY);
    return D3D_OK;
}
HRESULT NrTextureVol::UnlockBox(UINT level) {
    if (level >= core.levels) return D3DERR_INVALIDCALL;
    if (core.pool == D3DPOOL_DEFAULT && core.image) dev()->upload_texcore(core, false);
    return D3D_OK;
}
HRESULT NrVolume::LockBox(D3DLOCKED_BOX *out, const D3DBOX *box, DWORD flags) {
    if (!tex) return D3DERR_INVALIDCALL;
    if (tex->sysmem.empty()) tex->compute_level_layout();
    uint32_t lw = std::max(1u, tex->w >> level);
    uint32_t lh = std::max(1u, tex->h >> level);
    const FormatInfo &fi = format_info(tex->fmt);
    uint32_t row_pitch = ((lw + fi.block_width - 1) / fi.block_width) * fi.bytes_per_block;
    uint32_t slice_pitch = row_pitch * lh;
    out->RowPitch = (INT)row_pitch;
    out->SlicePitch = (INT)slice_pitch;
    out->pBits = tex->sysmem.data() + tex->level_offset[level];
    if (box)
        out->pBits = (uint8_t *)out->pBits + (size_t)box->Front * slice_pitch +
                     (size_t)box->Top * row_pitch + box->Left * (row_pitch / std::max(1u, lw));
    tex->dirty[level] = !(flags & D3DLOCK_READONLY);
    tex->whole_dirty = !(flags & D3DLOCK_READONLY);
    return D3D_OK;
}
HRESULT NrVolume::UnlockBox() {
    if (!tex) return D3DERR_INVALIDCALL;
    if (tex->pool == D3DPOOL_DEFAULT && tex->image) dev()->upload_texcore(*tex, false);
    return D3D_OK;
}

HRESULT NrVB::Lock(UINT off, UINT size_in, void **data, DWORD flags) {
    if (!data) return D3DERR_INVALIDCALL;
    if (size_in == 0 || off + size_in > this->size) size_in = this->size - off;
    if (pool == D3DPOOL_SYSTEMMEM || pool == D3DPOOL_MANAGED) {
        shadow.resize(this->size);
        *data = shadow.data() + off;
        needs_upload = !(flags & D3DLOCK_READONLY);
        return D3D_OK;
    }
    // DEFAULT: usa o ring do frame (dados vivos ate o submit do frame)
    StagingRing::Alloc a;
    if (!dev()->ring().alloc(size_in, 16, &a)) return D3DERR_OUTOFVIDEOMEMORY;
    this->ring_alloc = a;
    *data = a.mapped;
    if (!(flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE))) {
        // precisa do conteudo anterior: mantem shadow
        shadow.resize(this->size);
        memcpy(a.mapped, shadow.data() + off, size_in);
    }
    needs_upload = !(flags & D3DLOCK_READONLY);
    lock_off = off;
    lock_size = size_in;
    return D3D_OK;
}
HRESULT NrVB::Unlock() {
    if (needs_upload) {
        if (pool == D3DPOOL_SYSTEMMEM || pool == D3DPOOL_MANAGED) {
            needs_upload = true;   // upload no draw (device copia shadow -> buffer)
        } else if (ring_alloc.buffer) {
            dev()->record_buffer_upload(*this, ring_alloc);
            ring_alloc = {};
        }
        needs_upload = false;
    }
    return D3D_OK;
}
HRESULT NrIB::Lock(UINT off, UINT size_in, void **data, DWORD flags) {
    if (!data) return D3DERR_INVALIDCALL;
    if (size_in == 0 || off + size_in > this->size) size_in = this->size - off;
    if (pool == D3DPOOL_SYSTEMMEM || pool == D3DPOOL_MANAGED) {
        shadow.resize(this->size);
        *data = shadow.data() + off;
        needs_upload = !(flags & D3DLOCK_READONLY);
        return D3D_OK;
    }
    StagingRing::Alloc a;
    if (!dev()->ring().alloc(size_in, 16, &a)) return D3DERR_OUTOFVIDEOMEMORY;
    this->ring_alloc = a;
    *data = a.mapped;
    if (!(flags & (D3DLOCK_DISCARD | D3DLOCK_NOOVERWRITE))) {
        shadow.resize(this->size);
        memcpy(a.mapped, shadow.data() + off, size_in);
    }
    needs_upload = !(flags & D3DLOCK_READONLY);
    lock_off = off;
    lock_size = size_in;
    return D3D_OK;
}
HRESULT NrIB::Unlock() {
    if (needs_upload) {
        if (pool == D3DPOOL_SYSTEMMEM || pool == D3DPOOL_MANAGED) {
            needs_upload = true;
        } else if (ring_alloc.buffer) {
            dev()->record_buffer_upload(*this, ring_alloc);
            ring_alloc = {};
        }
        needs_upload = false;
    }
    return D3D_OK;
}
HRESULT NrVB::GetDesc(D3DVERTEXBUFFER_DESC *out) {
    if (!out) return D3DERR_INVALIDCALL;
    memset(out, 0, sizeof *out);
    out->Format = D3DFMT_VERTEXDATA;
    out->Type = D3DRTYPE_VERTEXBUFFER;
    out->Usage = usage;
    out->Pool = pool;
    out->Size = size;
    out->FVF = fvf;
    return D3D_OK;
}
HRESULT NrIB::GetDesc(D3DINDEXBUFFER_DESC *out) {
    if (!out) return D3DERR_INVALIDCALL;
    memset(out, 0, sizeof *out);
    out->Format = fmt;
    out->Type = D3DRTYPE_INDEXBUFFER;
    out->Usage = usage;
    out->Pool = pool;
    out->Size = size;
    return D3D_OK;
}

// ---------------------------------------------------------------- decl/fvf
HRESULT NrDecl::GetDeclaration(D3DVERTEXELEMENT9 *out_elems, UINT *count) {
    if (!count) return D3DERR_INVALIDCALL;
    if (out_elems && *count < elems.size() + 1) return D3DERR_MOREDATA;
    if (out_elems) {
        memcpy(out_elems, elems.data(), elems.size() * sizeof(D3DVERTEXELEMENT9));
        D3DVERTEXELEMENT9 end = {0xFF, 0, D3DDECLTYPE_UNUSED, D3DDECLMETHOD_DEFAULT, 0, 0};
        out_elems[elems.size()] = end;
    }
    *count = (UINT)elems.size() + 1;
    return D3D_OK;
}
HRESULT NrVS::GetFunction(void *data, UINT *size) {
    if (!size) return D3DERR_INVALIDCALL;
    if (data && *size < bytecode.size()) return D3DERR_MOREDATA;
    if (data) memcpy(data, bytecode.data(), bytecode.size());
    *size = (UINT)bytecode.size();
    return D3D_OK;
}
HRESULT NrPS::GetFunction(void *data, UINT *size) {
    if (!size) return D3DERR_INVALIDCALL;
    if (data && *size < bytecode.size()) return D3DERR_MOREDATA;
    if (data) memcpy(data, bytecode.data(), bytecode.size());
    *size = (UINT)bytecode.size();
    return D3D_OK;
}

} // namespace nvr
