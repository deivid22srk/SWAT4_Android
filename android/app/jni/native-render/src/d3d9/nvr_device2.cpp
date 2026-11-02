// Buffers, decls, shaders, state blocks, queries e blits/readbacks.
// Continuacao de nvr_device.cpp (mesmo objeto).
#include "nvr_device.h"
#include "nvr_com_impl.h"
#include <algorithm>
#include <SDL2/SDL.h>

namespace nvr {

// ------------------------------------------------------------------ FVF
void fvf_to_elements(DWORD fvf, std::vector<D3DVERTEXELEMENT9> &out) {
    DWORD texcount = (fvf & D3DFVF_TEXCOUNT_MASK) >> D3DFVF_TEXCOUNT_SHIFT;
    uint16_t stream = 0;
    uint16_t offset = 0;
    auto add = [&](uint8_t usage, uint8_t idx, uint8_t type) {
        D3DVERTEXELEMENT9 e{stream, offset, type, D3DDECLMETHOD_DEFAULT, usage, idx};
        out.push_back(e);
        offset += (uint16_t)d3d9_decltype_size(type);
    };
    if (fvf & D3DFVF_XYZRHW) { add(D3DDECLUSAGE_POSITIONT, 0, D3DDECLTYPE_FLOAT4); }
    else if (fvf & D3DFVF_XYZ) { add(D3DDECLUSAGE_POSITION, 0, D3DDECLTYPE_FLOAT3); }
    else if (fvf & D3DFVF_XYZB5) {
        add(D3DDECLUSAGE_POSITION, 0, D3DDECLTYPE_FLOAT3);
        add(D3DDECLUSAGE_BLENDWEIGHT, 0, D3DDECLTYPE_FLOAT4);
        if (fvf & D3DFVF_LASTBETA_UBYTE4) add(D3DDECLUSAGE_BLENDINDICES, 0, D3DDECLTYPE_UBYTE4);
        else add(D3DDECLUSAGE_BLENDINDICES, 0, D3DDECLTYPE_FLOAT1);
    } else if (fvf & D3DFVF_XYZB4) {
        add(D3DDECLUSAGE_POSITION, 0, D3DDECLTYPE_FLOAT3);
        add(D3DDECLUSAGE_BLENDWEIGHT, 0, D3DDECLTYPE_FLOAT4);
    } else if (fvf & D3DFVF_XYZB3) {
        add(D3DDECLUSAGE_POSITION, 0, D3DDECLTYPE_FLOAT3);
        add(D3DDECLUSAGE_BLENDWEIGHT, 0, D3DDECLTYPE_FLOAT3);
    } else if (fvf & D3DFVF_XYZB2) {
        add(D3DDECLUSAGE_POSITION, 0, D3DDECLTYPE_FLOAT3);
        add(D3DDECLUSAGE_BLENDWEIGHT, 0, D3DDECLTYPE_FLOAT2);
    } else if (fvf & D3DFVF_XYZB1) {
        add(D3DDECLUSAGE_POSITION, 0, D3DDECLTYPE_FLOAT3);
        add(D3DDECLUSAGE_BLENDWEIGHT, 0, D3DDECLTYPE_FLOAT1);
    }
    if (fvf & D3DFVF_NORMAL) add(D3DDECLUSAGE_NORMAL, 0, D3DDECLTYPE_FLOAT3);
    if (fvf & D3DFVF_PSIZE) add(D3DDECLUSAGE_PSIZE, 0, D3DDECLTYPE_FLOAT1);
    if (fvf & D3DFVF_DIFFUSE) add(D3DDECLUSAGE_COLOR, 0, D3DDECLTYPE_D3DCOLOR);
    if (fvf & D3DFVF_SPECULAR) add(D3DDECLUSAGE_COLOR, 1, D3DDECLTYPE_D3DCOLOR);
    static const uint8_t tex_types[4] = {D3DDECLTYPE_FLOAT2, D3DDECLTYPE_FLOAT1, D3DDECLTYPE_FLOAT3,
                                         D3DDECLTYPE_FLOAT4};
    for (DWORD i = 0; i < texcount; i++) {
        DWORD t = (fvf >> (16 + i * 2)) & 3;
        add(D3DDECLUSAGE_TEXCOORD, (uint8_t)i, tex_types[t]);
    }
}

uint32_t d3d9_decltype_size(uint8_t type) {
    switch (type) {
    case D3DDECLTYPE_FLOAT1: return 4;
    case D3DDECLTYPE_FLOAT2: return 8;
    case D3DDECLTYPE_FLOAT3: return 12;
    case D3DDECLTYPE_FLOAT4: return 16;
    case D3DDECLTYPE_D3DCOLOR: return 4;
    case D3DDECLTYPE_UBYTE4: return 4;
    case D3DDECLTYPE_SHORT2: return 4;
    case D3DDECLTYPE_SHORT4: return 8;
    case D3DDECLTYPE_UBYTE4N: return 4;
    case D3DDECLTYPE_SHORT2N: return 4;
    case D3DDECLTYPE_SHORT4N: return 8;
    case D3DDECLTYPE_USHORT2N: return 4;
    case D3DDECLTYPE_USHORT4N: return 8;
    case D3DDECLTYPE_UDEC3: return 4;
    case D3DDECLTYPE_DEC3N: return 4;
    case D3DDECLTYPE_FLOAT16_2: return 4;
    case D3DDECLTYPE_FLOAT16_4: return 8;
    default: return 0;
    }
}

// ---------------------------------------------------------------- buffers
HRESULT NrDevice::CreateVertexBuffer(UINT len, DWORD usage, DWORD fvf, D3DPOOL pool,
                                     IDirect3DVertexBuffer9 **out, HANDLE *) {
    if (!out || !len) return D3DERR_INVALIDCALL;
    auto *b = new NrVB();
    b->owner = (void *)this;
    b->size = len;
    b->usage = usage;
    b->fvf = fvf;
    b->pool = pool;
    if (pool == D3DPOOL_SYSTEMMEM || pool == D3DPOOL_MANAGED) {
        // host-visible direto (o shadow alimenta o buffer no draw)
        b->shadow.resize(len);
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = len;
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        b->alloc = mem_alloc_buffer(m_vk, bci, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, &b->buffer);
        if (!b->alloc) { delete b; return D3DERR_OUTOFVIDEOMEMORY; }
        mem_map(m_vk, b->alloc, &b->mapped);
    } else {
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = len;
        bci.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        b->alloc = mem_alloc_buffer(m_vk, bci, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, &b->buffer);
        if (!b->alloc) { delete b; return D3DERR_OUTOFVIDEOMEMORY; }
    }
    b->destroy_cb = [b, this]() { destroy_buffer(b->buffer, b->alloc); };
    *out = b;
    return D3D_OK;
}

HRESULT NrDevice::CreateIndexBuffer(UINT len, DWORD usage, D3DFORMAT fmt, D3DPOOL pool,
                                    IDirect3DIndexBuffer9 **out, HANDLE *) {
    if (!out || !len) return D3DERR_INVALIDCALL;
    if (fmt != D3DFMT_INDEX16 && fmt != D3DFMT_INDEX32) return D3DERR_INVALIDCALL;
    auto *b = new NrIB();
    b->owner = (void *)this;
    b->size = len;
    b->usage = usage;
    b->fmt = fmt;
    b->pool = pool;
    // D3DFMT_INDEX32 exige o feature fullDrawIndexUint32 (consultado no boot)
    if (pool == D3DPOOL_SYSTEMMEM || pool == D3DPOOL_MANAGED) {
        b->shadow.resize(len);
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = len;
        bci.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT |
                    VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        b->alloc = mem_alloc_buffer(m_vk, bci, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, &b->buffer);
        if (!b->alloc) { delete b; return D3DERR_OUTOFVIDEOMEMORY; }
        mem_map(m_vk, b->alloc, &b->mapped);
    } else {
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bci.size = len;
        bci.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        b->alloc = mem_alloc_buffer(m_vk, bci, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, &b->buffer);
        if (!b->alloc) { delete b; return D3DERR_OUTOFVIDEOMEMORY; }
    }
    b->destroy_cb = [b, this]() { destroy_buffer(b->buffer, b->alloc); };
    *out = b;
    return D3D_OK;
}

void NrDevice::record_buffer_upload(NrVB &b, const StagingRing::Alloc &a) {
    VkBufferCopy cpy{a.offset, 0, a.size};
    vkCmdCopyBuffer(cmd(), a.buffer, b.buffer, 1, &cpy);
    (void)b;
}
void NrDevice::record_buffer_upload(NrIB &b, const StagingRing::Alloc &a) {
    VkBufferCopy cpy{a.offset, 0, a.size};
    vkCmdCopyBuffer(cmd(), a.buffer, b.buffer, 1, &cpy);
    (void)b;
}

void NrDevice::upload_buffer_shadow(NrVB &b) {
    if (b.shadow.empty() || !b.buffer) return;
    StagingRing::Alloc a;
    if (!ring().alloc(b.size, 16, &a)) { partial_flush_and_wait(); if (!ring().alloc(b.size, 16, &a)) return; }
    memcpy(a.mapped, b.shadow.data(), b.size);
    record_buffer_upload(b, a);
}
void NrDevice::upload_buffer_shadow(NrIB &b) {
    if (b.shadow.empty() || !b.buffer) return;
    StagingRing::Alloc a;
    if (!ring().alloc(b.size, 16, &a)) { partial_flush_and_wait(); if (!ring().alloc(b.size, 16, &a)) return; }
    memcpy(a.mapped, b.shadow.data(), b.size);
    record_buffer_upload(b, a);
}

// ------------------------------------------------------------------ decls
HRESULT NrDevice::CreateVertexDeclaration(const D3DVERTEXELEMENT9 *elems, IDirect3DVertexDeclaration9 **out) {
    if (!out || !elems) return D3DERR_INVALIDCALL;
    auto *d = new NrDecl();
    d->owner = (void *)this;
    while (!(elems->Stream == 0xFF && elems->Type == D3DDECLTYPE_UNUSED)) {
        d->elems.push_back(*elems);
        elems++;
        if (d->elems.size() > 64) { delete d; return D3DERR_INVALIDCALL; }
    }
    *out = d;
    return D3D_OK;
}
HRESULT NrDevice::SetVertexDeclaration(IDirect3DVertexDeclaration9 *decl) {
    if (m_st.decl) m_st.decl->Release();
    m_st.decl = static_cast<NrDecl *>(decl);
    if (m_st.decl) {
        m_st.decl->AddRef();
        m_st.fvf = 0;
    }
    if (m_recording_stateblock) {
        uint32_t op = 22;
        sb_put(&op, 4);
        void *p = (void *)decl;
        sb_put(&p, 8);
        if (decl) decl->AddRef();
    }
    return D3D_OK;
}
HRESULT NrDevice::GetVertexDeclaration(IDirect3DVertexDeclaration9 **out) {
    if (!out) return D3DERR_INVALIDCALL;
    *out = m_st.decl;
    if (*out) (*out)->AddRef();
    return D3D_OK;
}
HRESULT NrDevice::SetFVF(DWORD fvf) {
    if (m_st.decl) m_st.decl->Release();
    m_st.decl = nullptr;
    m_st.fvf = fvf;
    if (m_recording_stateblock) { uint32_t op = 21; sb_put(&op, 4); sb_put(&fvf, 4); }
    return D3D_OK;
}
HRESULT NrDevice::GetFVF(DWORD *fvf) {
    if (!fvf) return D3DERR_INVALIDCALL;
    *fvf = m_st.fvf;
    return D3D_OK;
}

// ---------------------------------------------------------------- streams
HRESULT NrDevice::SetStreamSource(UINT idx, IDirect3DVertexBuffer9 *vb, UINT off, UINT stride) {
    if (idx >= 16) return D3DERR_INVALIDCALL;
    NrVB *b = static_cast<NrVB *>(vb);
    NrVB *velho = m_st.streams[idx].vb;
    if (b) b->AddRef();
    m_st.streams[idx].vb = b;
    m_st.streams[idx].offset = off;
    m_st.streams[idx].stride = stride;
    if (velho) velho->Release();
    if (m_recording_stateblock) {
        uint32_t op = 18;
        sb_put(&op, 4);
        sb_put(&idx, 4);
        void *p = (void *)b;
        sb_put(&p, 8);
        sb_put(&off, 4);
        sb_put(&stride, 4);
        if (b) b->AddRef();
    }
    return D3D_OK;
}
HRESULT NrDevice::GetStreamSource(UINT idx, IDirect3DVertexBuffer9 **vb, UINT *off, UINT *stride) {
    if (idx >= 16 || !vb || !off || !stride) return D3DERR_INVALIDCALL;
    *vb = m_st.streams[idx].vb;
    *off = m_st.streams[idx].offset;
    *stride = m_st.streams[idx].stride;
    if (*vb) (*vb)->AddRef();
    return D3D_OK;
}
HRESULT NrDevice::SetStreamSourceFreq(UINT idx, UINT freq) {
    if (idx >= 16 || !(freq & 1) == !(freq & 0x80000000u)) {
        // divisor precisa de bit alto (instancing) ou valor simples
    }
    if (idx >= 16) return D3DERR_INVALIDCALL;
    m_st.streams[idx].freq = freq;
    if (m_recording_stateblock) { uint32_t op = 19; sb_put(&op, 4); sb_put(&idx, 4); sb_put(&freq, 4); }
    return D3D_OK;
}
HRESULT NrDevice::GetStreamSourceFreq(UINT idx, UINT *freq) {
    if (idx >= 16 || !freq) return D3DERR_INVALIDCALL;
    *freq = m_st.streams[idx].freq;
    return D3D_OK;
}
HRESULT NrDevice::SetIndices(IDirect3DIndexBuffer9 *ib) {
    NrIB *b = static_cast<NrIB *>(ib);
    NrIB *velho = m_st.ib;
    if (b) b->AddRef();
    m_st.ib = b;
    if (velho) velho->Release();
    if (m_recording_stateblock) {
        uint32_t op = 20;
        sb_put(&op, 4);
        void *p = (void *)b;
        sb_put(&p, 8);
        if (b) b->AddRef();
    }
    return D3D_OK;
}
HRESULT NrDevice::GetIndices(IDirect3DIndexBuffer9 **ib) {
    if (!ib) return D3DERR_INVALIDCALL;
    *ib = m_st.ib;
    if (*ib) (*ib)->AddRef();
    return D3D_OK;
}

// ---------------------------------------------------------------- shaders
HRESULT NrDevice::CreateVertexShader(const DWORD *code, IDirect3DVertexShader9 **out) {
    if (!out || !code) return D3DERR_INVALIDCALL;
    uint32_t len = 0;
    while (code[len] != 0x0000FFFF && len < (1u << 20)) len++;
    len += 1;
    auto *s = new NrVS();
    s->owner = (void *)this;
    s->bytecode.assign((const uint8_t *)code, (const uint8_t *)code + len * 4);
    // valida o parse cedo (erro nomeado no log; o jogo trata como shader ruim)
    BcShader bc;
    std::string err;
    if (!bc_parse((const uint32_t *)code, len, bc, err)) {
        NVR_ERROR("CreateVertexShader: %s", err.c_str());
        delete s;
        return D3DERR_INVALIDCALL;
    }
    if (bc.dynamic_sampler) {
        NVR_ERROR("CreateVertexShader: indexacao dinamica de sampler");
        delete s;
        return D3DERR_INVALIDCALL;
    }
    *out = s;
    return D3D_OK;
}
HRESULT NrDevice::CreatePixelShader(const DWORD *code, IDirect3DPixelShader9 **out) {
    if (!out || !code) return D3DERR_INVALIDCALL;
    uint32_t len = 0;
    while (code[len] != 0x0000FFFF && len < (1u << 20)) len++;
    len += 1;
    auto *s = new NrPS();
    s->owner = (void *)this;
    s->bytecode.assign((const uint8_t *)code, (const uint8_t *)code + len * 4);
    BcShader bc;
    std::string err;
    if (!bc_parse((const uint32_t *)code, len, bc, err)) {
        NVR_ERROR("CreatePixelShader: %s", err.c_str());
        delete s;
        return D3DERR_INVALIDCALL;
    }
    if (bc.dynamic_sampler) {
        NVR_ERROR("CreatePixelShader: indexacao dinamica de sampler");
        delete s;
        return D3DERR_INVALIDCALL;
    }
    *out = s;
    return D3D_OK;
}
HRESULT NrDevice::SetVertexShader(IDirect3DVertexShader9 *vs) {
    if (m_st.vs) m_st.vs->Release();
    m_st.vs = vs;
    if (vs) vs->AddRef();
    if (m_recording_stateblock) {
        uint32_t op = 10;
        sb_put(&op, 4);
        void *p = (void *)vs;
        sb_put(&p, 8);
        if (vs) vs->AddRef();
    }
    return D3D_OK;
}
HRESULT NrDevice::GetVertexShader(IDirect3DVertexShader9 **vs) {
    if (!vs) return D3DERR_INVALIDCALL;
    *vs = m_st.vs;
    if (*vs) (*vs)->AddRef();
    return D3D_OK;
}
HRESULT NrDevice::SetPixelShader(IDirect3DPixelShader9 *ps) {
    if (m_st.ps) m_st.ps->Release();
    m_st.ps = ps;
    if (ps) ps->AddRef();
    if (m_recording_stateblock) {
        uint32_t op = 11;
        sb_put(&op, 4);
        void *p = (void *)ps;
        sb_put(&p, 8);
        if (ps) ps->AddRef();
    }
    return D3D_OK;
}
HRESULT NrDevice::GetPixelShader(IDirect3DPixelShader9 **ps) {
    if (!ps) return D3DERR_INVALIDCALL;
    *ps = m_st.ps;
    if (*ps) (*ps)->AddRef();
    return D3D_OK;
}
HRESULT NrDevice::SetVertexShaderConstantF(UINT reg, const float *data, UINT count) {
    if (reg + count > kMaxVsc || !data) return D3DERR_INVALIDCALL;
    memcpy(m_st.vf[reg], data, count * 16);
    if (m_recording_stateblock) {
        uint32_t op = 12;
        sb_put(&op, 4);
        sb_put(&reg, 4);
        sb_put(&count, 4);
        sb_put(data, count * 16);
    }
    return D3D_OK;
}
HRESULT NrDevice::GetVertexShaderConstantF(UINT reg, float *data, UINT count) {
    if (reg + count > kMaxVsc || !data) return D3DERR_INVALIDCALL;
    memcpy(data, m_st.vf[reg], count * 16);
    return D3D_OK;
}
HRESULT NrDevice::SetVertexShaderConstantI(UINT reg, const int *data, UINT count) {
    if (reg + count > kMaxIsc || !data) return D3DERR_INVALIDCALL;
    memcpy(m_st.vi[reg], data, count * 16);
    if (m_recording_stateblock) {
        uint32_t op = 13;
        sb_put(&op, 4);
        sb_put(&reg, 4);
        sb_put(&count, 4);
        sb_put(data, count * 16);
    }
    return D3D_OK;
}
HRESULT NrDevice::GetVertexShaderConstantI(UINT reg, int *data, UINT count) {
    if (reg + count > kMaxIsc || !data) return D3DERR_INVALIDCALL;
    memcpy(data, m_st.vi[reg], count * 16);
    return D3D_OK;
}
HRESULT NrDevice::SetVertexShaderConstantB(UINT reg, const BOOL *data, UINT count) {
    if (reg + count > kMaxBsc || !data) return D3DERR_INVALIDCALL;
    memcpy(&m_st.vbool[reg], data, count * 4);
    if (m_recording_stateblock) {
        uint32_t op = 14;
        sb_put(&op, 4);
        sb_put(&reg, 4);
        sb_put(&count, 4);
        sb_put(data, count * 4);
    }
    return D3D_OK;
}
HRESULT NrDevice::GetVertexShaderConstantB(UINT reg, BOOL *data, UINT count) {
    if (reg + count > kMaxBsc || !data) return D3DERR_INVALIDCALL;
    memcpy(data, &m_st.vbool[reg], count * 4);
    return D3D_OK;
}
HRESULT NrDevice::SetPixelShaderConstantF(UINT reg, const float *data, UINT count) {
    if (reg + count > kMaxPsc || !data) return D3DERR_INVALIDCALL;
    memcpy(m_st.pf[reg], data, count * 16);
    if (m_recording_stateblock) {
        uint32_t op = 15;
        sb_put(&op, 4);
        sb_put(&reg, 4);
        sb_put(&count, 4);
        sb_put(data, count * 16);
    }
    return D3D_OK;
}
HRESULT NrDevice::GetPixelShaderConstantF(UINT reg, float *data, UINT count) {
    if (reg + count > kMaxPsc || !data) return D3DERR_INVALIDCALL;
    memcpy(data, m_st.pf[reg], count * 16);
    return D3D_OK;
}
HRESULT NrDevice::SetPixelShaderConstantI(UINT reg, const int *data, UINT count) {
    if (reg + count > kMaxIsc || !data) return D3DERR_INVALIDCALL;
    memcpy(m_st.pi[reg], data, count * 16);
    if (m_recording_stateblock) {
        uint32_t op = 16;
        sb_put(&op, 4);
        sb_put(&reg, 4);
        sb_put(&count, 4);
        sb_put(data, count * 16);
    }
    return D3D_OK;
}
HRESULT NrDevice::GetPixelShaderConstantI(UINT reg, int *data, UINT count) {
    if (reg + count > kMaxIsc || !data) return D3DERR_INVALIDCALL;
    memcpy(data, m_st.pi[reg], count * 16);
    return D3D_OK;
}
HRESULT NrDevice::SetPixelShaderConstantB(UINT reg, const BOOL *data, UINT count) {
    if (reg + count > kMaxBsc || !data) return D3DERR_INVALIDCALL;
    memcpy(&m_st.pb[reg], data, count * 4);
    if (m_recording_stateblock) {
        uint32_t op = 17;
        sb_put(&op, 4);
        sb_put(&reg, 4);
        sb_put(&count, 4);
        sb_put(data, count * 4);
    }
    return D3D_OK;
}
HRESULT NrDevice::GetPixelShaderConstantB(UINT reg, BOOL *data, UINT count) {
    if (reg + count > kMaxBsc || !data) return D3DERR_INVALIDCALL;
    memcpy(data, &m_st.pb[reg], count * 4);
    return D3D_OK;
}

// ------------------------------------------------------------ state block
// Modelo: os setters do device gravam registros binarios quando
// BeginStateBlock esta ativo; CreateStateBlock gera o mesmo stream por
// subconjunto (all/vertex/pixel). Apply repassa os registros pelos proprios
// setters (efeito identico as chamadas originais). Ponteiros de objetos
// (shaders/texturas/decl/RT) sao AddRef no record e Release no destrutor.

// opcodes do stream
enum SbOp {
    SB_END = 0, SB_RS = 1, SB_TSS = 2, SB_SAMP = 3, SB_XFM = 4, SB_MAT = 5,
    SB_LIGHT = 6, SB_LIGHTEN = 7, SB_CLIP = 8, SB_TEX = 9, SB_VS = 10, SB_PS = 11,
    SB_VSCF = 12, SB_VSCI = 13, SB_VSCB = 14, SB_PSCF = 15, SB_PSCI = 16,
    SB_PSCB = 17, SB_STREAM = 18, SB_FREQ = 19, SB_IB = 20, SB_FVF = 21,
    SB_DECL = 22, SB_VP = 23, SB_SCISSOR = 24, SB_SCISSOREN = 25, SB_SWVP = 26
};

void NrDevice::sb_put(const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    m_stateblock_data.insert(m_stateblock_data.end(), b, b + n);
}

void NrDevice::sb_capture_subset(int kind) {
    DeviceState &st = m_st;
    bool vertex = kind == 0 || kind == 1;
    bool pixel = kind == 0 || kind == 2;
    uint32_t op;
    if (vertex) {
        static const D3DRENDERSTATETYPE rs_v[] = {
            D3DRS_SHADEMODE, D3DRS_VERTEXBLEND, D3DRS_FOGENABLE, D3DRS_FOGVERTEXMODE,
            D3DRS_FOGSTART, D3DRS_FOGEND, D3DRS_FOGDENSITY, D3DRS_RANGEFOGENABLE,
            D3DRS_AMBIENT, D3DRS_COLORVERTEX, D3DRS_LOCALVIEWER, D3DRS_NORMALIZENORMALS,
            D3DRS_DIFFUSEMATERIALSOURCE, D3DRS_SPECULARMATERIALSOURCE,
            D3DRS_AMBIENTMATERIALSOURCE, D3DRS_EMISSIVEMATERIALSOURCE,
            D3DRS_TEXTUREFACTOR, D3DRS_POINTSIZE, D3DRS_POINTSIZE_MIN, D3DRS_POINTSIZE_MAX,
            D3DRS_POINTSPRITEENABLE, D3DRS_POINTSCALEENABLE, D3DRS_CLIPPING,
            D3DRS_LIGHTING, D3DRS_WRAP0, D3DRS_WRAP1, D3DRS_WRAP2, D3DRS_WRAP3,
            D3DRS_WRAP4, D3DRS_WRAP5, D3DRS_WRAP6, D3DRS_WRAP7};
        for (DWORD r : rs_v) {
            op = SB_RS;
            sb_put(&op, 4);
            sb_put(&r, 4);
            sb_put(&st.rs[r], 4);
        }
        for (int t = 0; t < 256; t++) {
            op = SB_XFM;
            sb_put(&op, 4);
            sb_put(&t, 4);
            sb_put(&st.xfm[t], sizeof(D3DMATRIX));
        }
        op = SB_MAT;
        sb_put(&op, 4);
        sb_put(&st.mat, sizeof st.mat);
        for (int i = 0; i < 8; i++) {
            op = SB_LIGHT;
            sb_put(&op, 4);
            sb_put(&i, 4);
            sb_put(&st.lights[i].l, sizeof(D3DLIGHT9));
            op = SB_LIGHTEN;
            sb_put(&op, 4);
            sb_put(&i, 4);
            BOOL en = st.lights[i].enabled ? TRUE : FALSE;
            sb_put(&en, 4);
        }
        for (int i = 0; i < 6; i++) {
            op = SB_CLIP;
            sb_put(&op, 4);
            sb_put(&i, 4);
            sb_put(st.clip_planes[i], 16);
        }
        op = SB_VS;
        sb_put(&op, 4);
        sb_put(&st.vs, 8);
        if (st.vs) st.vs->AddRef();
        for (int i = 0; i < 3; i++) {
            static const D3DTRANSFORMSTATETYPE ttypes[3] = {D3DTS_WORLD, D3DTS_VIEW, D3DTS_PROJECTION};
            (void)ttypes[i];
        }
        op = SB_VSCF;
        sb_put(&op, 4);
        UINT reg0 = 0, cnt = kMaxVsc;
        sb_put(&reg0, 4);
        sb_put(&cnt, 4);
        sb_put(st.vf, sizeof st.vf);
        op = SB_VSCI;
        sb_put(&op, 4);
        cnt = kMaxIsc;
        sb_put(&reg0, 4);
        sb_put(&cnt, 4);
        sb_put(st.vi, sizeof st.vi);
        op = SB_VSCB;
        sb_put(&op, 4);
        cnt = kMaxBsc;
        sb_put(&reg0, 4);
        sb_put(&cnt, 4);
        sb_put(st.vbool, sizeof st.vbool);
        for (int i = 0; i < 16; i++) {
            if (!st.streams[i].vb) continue;
            op = SB_STREAM;
            sb_put(&op, 4);
            sb_put(&i, 4);
            sb_put(&st.streams[i].vb, 8);
            sb_put(&st.streams[i].offset, 4);
            sb_put(&st.streams[i].stride, 4);
            st.streams[i].vb->AddRef();
        }
        if (st.ib) {
            op = SB_IB;
            sb_put(&op, 4);
            sb_put(&st.ib, 8);
            st.ib->AddRef();
        }
        if (st.decl) {
            op = SB_DECL;
            sb_put(&op, 4);
            sb_put(&st.decl, 8);
            st.decl->AddRef();
        } else {
            op = SB_FVF;
            sb_put(&op, 4);
            sb_put(&st.fvf, 4);
        }
        op = SB_VP;
        sb_put(&op, 4);
        sb_put(&st.vp, sizeof st.vp);
        op = SB_SWVP;
        sb_put(&op, 4);
        BOOL sw = st.swvp ? TRUE : FALSE;
        sb_put(&sw, 4);
    }
    if (pixel) {
        op = SB_RS;
        sb_put(&op, 4);
        D3DRENDERSTATETYPE r = D3DRS_ALPHATESTENABLE;
        sb_put(&r, 4);
        sb_put(&st.rs[D3DRS_ALPHATESTENABLE], 4);
        static const D3DRENDERSTATETYPE rs_p[] = {
            D3DRS_ALPHATESTENABLE, D3DRS_ALPHAREF, D3DRS_ALPHAFUNC,
            D3DRS_ALPHABLENDENABLE, D3DRS_SRCBLEND, D3DRS_DESTBLEND, D3DRS_BLENDOP,
            D3DRS_SEPARATEALPHABLENDENABLE, D3DRS_SRCBLENDALPHA, D3DRS_DESTBLENDALPHA,
            D3DRS_BLENDOPALPHA, D3DRS_COLORWRITEENABLE, D3DRS_STENCILENABLE,
            D3DRS_STENCILFAIL, D3DRS_STENCILZFAIL, D3DRS_STENCILPASS, D3DRS_STENCILFUNC,
            D3DRS_STENCILREF, D3DRS_STENCILMASK, D3DRS_STENCILWRITEMASK,
            D3DRS_TWOSIDEDSTENCILMODE, D3DRS_CCW_STENCILFAIL, D3DRS_CCW_STENCILZFAIL,
            D3DRS_CCW_STENCILPASS, D3DRS_CCW_STENCILFUNC, D3DRS_ZENABLE, D3DRS_ZWRITEENABLE,
            D3DRS_ZFUNC, D3DRS_SCISSORTESTENABLE, D3DRS_DEPTHBIAS,
            D3DRS_SLOPESCALEDEPTHBIAS, D3DRS_CULLMODE, D3DRS_FILLMODE, D3DRS_DITHERENABLE,
            D3DRS_FOGCOLOR, D3DRS_FOGTABLEMODE, D3DRS_MULTISAMPLEANTIALIAS};
        for (DWORD r2 : rs_p) {
            op = SB_RS;
            sb_put(&op, 4);
            sb_put(&r2, 4);
            sb_put(&st.rs[r2], 4);
        }
        for (int i = 0; i < 8; i++)
            for (int t = 0; t <= 32; t++) {
                op = SB_TSS;
                sb_put(&op, 4);
                sb_put(&i, 4);
                sb_put(&t, 4);
                sb_put(&st.tss[i][t], 4);
            }
        for (int i = 0; i < 16; i++)
            for (int t = 0; t <= 13; t++) {
                op = SB_SAMP;
                sb_put(&op, 4);
                sb_put(&i, 4);
                sb_put(&t, 4);
                sb_put(&st.samp[i][t], 4);
            }
        op = SB_PS;
        sb_put(&op, 4);
        sb_put(&st.ps, 8);
        if (st.ps) st.ps->AddRef();
        for (int i = 0; i < 16; i++) {
            op = SB_TEX;
            sb_put(&op, 4);
            sb_put(&i, 4);
            sb_put(&st.tex[i], 8);
            if (st.tex[i]) st.tex[i]->AddRef();
        }
        op = SB_PSCF;
        sb_put(&op, 4);
        UINT reg0 = 0, cnt = kMaxPsc;
        sb_put(&reg0, 4);
        sb_put(&cnt, 4);
        sb_put(st.pf, sizeof st.pf);
        op = SB_PSCI;
        sb_put(&op, 4);
        cnt = kMaxIsc;
        sb_put(&reg0, 4);
        sb_put(&cnt, 4);
        sb_put(st.pi, sizeof st.pi);
        op = SB_PSCB;
        sb_put(&op, 4);
        cnt = kMaxBsc;
        sb_put(&reg0, 4);
        sb_put(&cnt, 4);
        sb_put(st.pb, sizeof st.pb);
        op = SB_SCISSOREN;
        sb_put(&op, 4);
        BOOL se = st.scissor_enable ? TRUE : FALSE;
        sb_put(&se, 4);
        op = SB_SCISSOR;
        sb_put(&op, 4);
        sb_put(&st.scissor, sizeof st.scissor);
    }
    op = SB_END;
    sb_put(&op, 4);
}

HRESULT NrDevice::CreateStateBlock(D3DSTATEBLOCKTYPE type, IDirect3DStateBlock9 **out) {
    if (!out) return D3DERR_INVALIDCALL;
    auto *sb = new NrStateBlock();
    sb->owner = (void *)this;
    m_stateblock_data.clear();
    m_recording_stateblock = true;
    switch (type) {
    case D3DSBT_ALL: sb_capture_subset(0); break;
    case D3DSBT_VERTEXSTATE: sb_capture_subset(1); break;
    case D3DSBT_PIXELSTATE: sb_capture_subset(2); break;
    default:
        m_recording_stateblock = false;
        delete sb;
        return D3DERR_INVALIDCALL;
    }
    m_recording_stateblock = false;
    sb->data = std::move(m_stateblock_data);
    m_stateblock_data.clear();
    *out = sb;
    return D3D_OK;
}

HRESULT NrDevice::BeginStateBlock() {
    if (m_recording_stateblock) return D3DERR_INVALIDCALL;
    m_recording_stateblock = true;
    m_stateblock_data.clear();
    return D3D_OK;
}

HRESULT NrDevice::EndStateBlock(IDirect3DStateBlock9 **out) {
    if (!out || !m_recording_stateblock) return D3DERR_INVALIDCALL;
    m_recording_stateblock = false;
    uint32_t op = SB_END;
    sb_put(&op, 4);
    auto *sb = new NrStateBlock();
    sb->owner = (void *)this;
    sb->data = std::move(m_stateblock_data);
    m_stateblock_data.clear();
    *out = sb;
    return D3D_OK;
}

NrStateBlock::~NrStateBlock() {
    // solta as refs dos objetos gravados no stream (tamanhos exatos por op)
    static const struct { uint16_t size; uint8_t ptr_off; uint8_t kind; } kOps[27] = {
        /*END*/ {0,0,0}, /*RS*/ {8,0,0}, /*TSS*/ {12,0,0}, /*SAMP*/ {12,0,0},
        /*XFM*/ {68,0,0}, /*MAT*/ {sizeof(D3DMATERIAL9),0,0}, /*LIGHT*/ {4+sizeof(D3DLIGHT9),0,0},
        /*LIGHTEN*/ {8,0,0}, /*CLIP*/ {20,0,0}, /*TEX*/ {12,4,1}, /*VS*/ {8,0,2},
        /*PS*/ {8,0,3}, /*VSCF*/ {8,0,0}, /*VSCI*/ {8,0,0}, /*VSCB*/ {8,0,0},
        /*PSCF*/ {8,0,0}, /*PSCI*/ {8,0,0}, /*PSCB*/ {8,0,0},
        /*STREAM*/ {20,4,4}, /*FREQ*/ {8,0,0}, /*IB*/ {8,0,5}, /*FVF*/ {4,0,0},
        /*DECL*/ {8,0,6}, /*VP*/ {sizeof(D3DVIEWPORT9),0,0}, /*SCISSOR*/ {sizeof(RECT),0,0},
        /*SCISSOREN*/ {4,0,0}, /*SWVP*/ {4,0,0}
    };
    const uint8_t *p = data.data();
    const uint8_t *end = data.data() + data.size();
    while (p + 4 <= end) {
        uint32_t op;
        memcpy(&op, p, 4);
        p += 4;
        if (op >= 27) break;
        auto &o = kOps[op];
        if (!o.size) break;
        if (p + o.size > end) break;
        if (o.ptr_off) {
            void *obj = nullptr;
            memcpy(&obj, p + o.ptr_off, 8);
            if (obj) {
                switch (o.kind) {
                case 1: ((IDirect3DBaseTexture9 *)obj)->Release(); break;
                case 2: ((IDirect3DVertexShader9 *)obj)->Release(); break;
                case 3: ((IDirect3DPixelShader9 *)obj)->Release(); break;
                case 4: ((IDirect3DVertexBuffer9 *)obj)->Release(); break;
                case 5: ((IDirect3DIndexBuffer9 *)obj)->Release(); break;
                case 6: ((IDirect3DVertexDeclaration9 *)obj)->Release(); break;
                }
            }
        }
        p += o.size;
    }
}

HRESULT NrStateBlock::Capture() {
    // Capture em um bloco de Begin/End: nao suportado pelo D3D9 (so CreateStateBlock
    // captura). Mantemos o stream gravado como esta.
    return D3D_OK;
}

HRESULT NrStateBlock::Apply() {
    NrDevice *d = static_cast<NrDevice *>(owner);
    if (!d || data.size() < 4) return D3DERR_INVALIDCALL;
    const uint8_t *p = data.data();
    const uint8_t *pend = data.data() + data.size();
    auto take = [&](void *o, size_t sz) {
        if (p + sz > pend) return false;
        memcpy(o, p, sz);
        p += sz;
        return true;
    };
    while (p < pend) {
        uint32_t op = SB_END;
        if (!take(&op, 4) || op == SB_END) break;
        switch (op) {
        case SB_RS: { DWORD r, v; if (!take(&r, 4) || !take(&v, 4)) return D3DERR_INVALIDCALL;
                      d->SetRenderState((D3DRENDERSTATETYPE)r, v); break; }
        case SB_TSS: { DWORD s, t, v; if (!take(&s, 4) || !take(&t, 4) || !take(&v, 4)) return D3DERR_INVALIDCALL;
                       d->SetTextureStageState(s, (D3DTEXTURESTAGESTATETYPE)t, v); break; }
        case SB_SAMP: { DWORD s, t, v; if (!take(&s, 4) || !take(&t, 4) || !take(&v, 4)) return D3DERR_INVALIDCALL;
                        d->SetSamplerState(s, (D3DSAMPLERSTATETYPE)t, v); break; }
        case SB_XFM: { DWORD t; D3DMATRIX m; if (!take(&t, 4) || !take(&m, sizeof m)) return D3DERR_INVALIDCALL;
                       d->SetTransform((D3DTRANSFORMSTATETYPE)t, &m); break; }
        case SB_MAT: { D3DMATERIAL9 m; if (!take(&m, sizeof m)) return D3DERR_INVALIDCALL;
                       d->SetMaterial(&m); break; }
        case SB_LIGHT: { DWORD i; D3DLIGHT9 l; if (!take(&i, 4) || !take(&l, sizeof l)) return D3DERR_INVALIDCALL;
                         d->SetLight(i, &l); break; }
        case SB_LIGHTEN: { DWORD i; BOOL e; if (!take(&i, 4) || !take(&e, 4)) return D3DERR_INVALIDCALL;
                           d->LightEnable(i, e); break; }
        case SB_CLIP: { DWORD i; float pl[4]; if (!take(&i, 4) || !take(pl, 16)) return D3DERR_INVALIDCALL;
                        d->SetClipPlane(i, pl); break; }
        case SB_TEX: { DWORD i; void *t; if (!take(&i, 4) || !take(&t, 8)) return D3DERR_INVALIDCALL;
                       d->SetTexture(i, (IDirect3DBaseTexture9 *)t); break; }
        case SB_VS: { void *v; if (!take(&v, 8)) return D3DERR_INVALIDCALL;
                      d->SetVertexShader((IDirect3DVertexShader9 *)v); break; }
        case SB_PS: { void *v; if (!take(&v, 8)) return D3DERR_INVALIDCALL;
                      d->SetPixelShader((IDirect3DPixelShader9 *)v); break; }
        case SB_VSCF: case SB_PSCF: { DWORD r, c; if (!take(&r, 4) || !take(&c, 4)) return D3DERR_INVALIDCALL;
                      std::vector<float> f((size_t)c * 4);
                      if (!take(f.data(), (size_t)c * 16)) return D3DERR_INVALIDCALL;
                      if (op == SB_VSCF) d->SetVertexShaderConstantF(r, f.data(), c);
                      else d->SetPixelShaderConstantF(r, f.data(), c);
                      break; }
        case SB_VSCI: case SB_PSCI: { DWORD r, c; if (!take(&r, 4) || !take(&c, 4)) return D3DERR_INVALIDCALL;
                      std::vector<int> iv((size_t)c * 4);
                      if (!take(iv.data(), (size_t)c * 16)) return D3DERR_INVALIDCALL;
                      if (op == SB_VSCI) d->SetVertexShaderConstantI(r, iv.data(), c);
                      else d->SetPixelShaderConstantI(r, iv.data(), c);
                      break; }
        case SB_VSCB: case SB_PSCB: { DWORD r, c; if (!take(&r, 4) || !take(&c, 4)) return D3DERR_INVALIDCALL;
                      std::vector<BOOL> bv((size_t)c);
                      if (!take(bv.data(), (size_t)c * 4)) return D3DERR_INVALIDCALL;
                      if (op == SB_VSCB) d->SetVertexShaderConstantB(r, bv.data(), c);
                      else d->SetPixelShaderConstantB(r, bv.data(), c);
                      break; }
        case SB_STREAM: { DWORD i; void *v; DWORD off, st_; if (!take(&i, 4) || !take(&v, 8) ||
                              !take(&off, 4) || !take(&st_, 4)) return D3DERR_INVALIDCALL;
                          d->SetStreamSource(i, (IDirect3DVertexBuffer9 *)v, off, st_); break; }
        case SB_FREQ: { DWORD i, f; if (!take(&i, 4) || !take(&f, 4)) return D3DERR_INVALIDCALL;
                        d->SetStreamSourceFreq(i, f); break; }
        case SB_IB: { void *v; if (!take(&v, 8)) return D3DERR_INVALIDCALL;
                      d->SetIndices((IDirect3DIndexBuffer9 *)v); break; }
        case SB_FVF: { DWORD f; if (!take(&f, 4)) return D3DERR_INVALIDCALL;
                       d->SetFVF(f); break; }
        case SB_DECL: { void *v; if (!take(&v, 8)) return D3DERR_INVALIDCALL;
                        d->SetVertexDeclaration((IDirect3DVertexDeclaration9 *)v); break; }
        case SB_VP: { D3DVIEWPORT9 vp; if (!take(&vp, sizeof vp)) return D3DERR_INVALIDCALL;
                      d->SetViewport(&vp); break; }
        case SB_SCISSOR: { RECT r; if (!take(&r, sizeof r)) return D3DERR_INVALIDCALL;
                           d->SetScissorRect(&r); break; }
        case SB_SCISSOREN: { BOOL e; if (!take(&e, 4)) return D3DERR_INVALIDCALL;
                             d->SetRenderState(D3DRS_SCISSORTESTENABLE, (DWORD)e); break; }
        case SB_SWVP: { BOOL e; if (!take(&e, 4)) return D3DERR_INVALIDCALL;
                        d->SetSoftwareVertexProcessing(e); break; }
        default:
            NVR_WARN("stateblock: op desconhecido %u no replay", op);
            return D3DERR_INVALIDCALL;
        }
    }
    return D3D_OK;
}

// ---------------------------------------------------------------- queries
void NrDevice::ensure_occlusion_pool() {
    if (!m_occlusion_pools.empty() && m_occlusion_pools.back().used < 256) return;
    VkQueryPoolCreateInfo pi{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};
    pi.queryType = VK_QUERY_TYPE_OCCLUSION;
    pi.queryCount = 256;
    QueryPool qp{};
    if (vkCreateQueryPool(m_vk->device(), &pi, nullptr, &qp.pool) == VK_SUCCESS) {
        qp.used = 0;
        m_occlusion_pools.push_back(qp);
    }
}

HRESULT NrDevice::CreateQuery(D3DQUERYTYPE type, IDirect3DQuery9 **out) {
    if (!out) return D3DERR_INVALIDCALL;
    // tipos que o device realmente suporta (o resto e honesto: NOTAVAILABLE)
    switch (type) {
    case D3DQUERYTYPE_OCCLUSION:
    case D3DQUERYTYPE_EVENT:
    case D3DQUERYTYPE_TIMESTAMP:
        break;
    default:
        return D3DERR_NOTAVAILABLE;
    }
    auto *q = new NrQuery();
    q->owner = (void *)this;
    q->type = type;
    if (type == D3DQUERYTYPE_OCCLUSION) {
        ensure_occlusion_pool();
        if (m_occlusion_pools.empty()) { delete q; return D3DERR_NOTAVAILABLE; }
        q->pool_slot = (uint32_t)(m_occlusion_pools.size() - 1);
        q->query_idx = m_occlusion_pools.back().used++;
    }
    *out = q;
    return D3D_OK;
}

DWORD NrQuery::GetDataSize() {
    switch (type) {
    case D3DQUERYTYPE_OCCLUSION: return sizeof(DWORD);
    case D3DQUERYTYPE_EVENT: return sizeof(BOOL);
    case D3DQUERYTYPE_TIMESTAMP: return sizeof(UINT64);
    default: return 0;
    }
}

HRESULT NrQuery::Issue(DWORD flags) {
    NrDevice *d = static_cast<NrDevice *>(owner);
    if (!d) return D3DERR_INVALIDCALL;
    if (flags & D3DISSUE_BEGIN) {
        issued = false;
        if (type == D3DQUERYTYPE_OCCLUSION && pool_slot != 0xFFFFFFFFu) {
            auto &pool = d->occlusion_pool(pool_slot);
            // reset FORA de render pass (o begin/end entram no pass corrente)
            d->end_pass();
            vkCmdResetQueryPool(d->cmd(), pool, query_idx, 1);
            d->ensure_pass_open(false, false);
            vkCmdBeginQuery(d->cmd(), pool, query_idx, 0);
            m_begun = true;
        }
    }
    if (flags & D3DISSUE_END) {
        if (type == D3DQUERYTYPE_OCCLUSION && pool_slot != 0xFFFFFFFFu) {
            auto &pool = d->occlusion_pool(pool_slot);
            if (m_begun) {
                vkCmdEndQuery(d->cmd(), pool, query_idx);
                m_begun = false;
            }
        } else if (type == D3DQUERYTYPE_EVENT) {
            // EVENT: fence de submit parcial marca o ponto de sincronizacao
            d->m_event_fences.push_back(d->submit_partial_fence());
        }
        issued = true;
    }
    return D3D_OK;
}

HRESULT NrQuery::GetData(void *data, DWORD size, DWORD flags) {
    NrDevice *d = static_cast<NrDevice *>(owner);
    if (!d) return D3DERR_INVALIDCALL;
    if (size < GetDataSize()) return D3DERR_INVALIDCALL;
    if (!issued) return S_FALSE;
    if (type == D3DQUERYTYPE_OCCLUSION && pool_slot != 0xFFFFFFFFu) {
        if (flags & D3DGETDATA_FLUSH) d->partial_flush_and_wait();
        auto &pool = d->occlusion_pool(pool_slot);
        uint64_t result = 0;
        VkResult r = vkGetQueryPoolResults(d->vk().device(), pool, query_idx, 1, sizeof result,
                                           &result, sizeof result, VK_QUERY_RESULT_64_BIT);
        if (r == VK_NOT_READY) return S_FALSE;
        if (r != VK_SUCCESS) return D3DERR_INVALIDCALL;
        if (data) *(DWORD *)data = (DWORD)result;
        return S_OK;
    }
    if (type == D3DQUERYTYPE_EVENT) {
        if (flags & D3DGETDATA_FLUSH) {
            auto &fences = d->m_event_fences;
            for (auto f : fences) {
                vkWaitForFences(d->vk().device(), 1, &f, VK_TRUE, UINT64_MAX);
                vkDestroyFence(d->vk().device(), f, nullptr);
            }
            fences.clear();
        } else {
            return S_FALSE;
        }
        if (data) *(BOOL *)data = TRUE;
        return S_OK;
    }
    if (type == D3DQUERYTYPE_TIMESTAMP) {
        if (flags & D3DGETDATA_FLUSH) d->partial_flush_and_wait();
        if (data) *(UINT64 *)data = d->timestamp_ns();
        return S_OK;
    }
    return D3DERR_INVALIDCALL;
}

// ------------------------------------------------------------------ blits
HRESULT NrDevice::ColorFill(IDirect3DSurface9 *surf, const RECT *rect, D3DCOLOR color) {
    if (!surf) return D3DERR_INVALIDCALL;
    NrSurface *s = static_cast<NrSurface *>(surf);
    if (s->core.pool == D3DPOOL_SYSTEMMEM || !s->core.img()) {
        // sysmem: preenche o shadow (surface standalone sysmem nao tem shadow;
        // GetRenderTargetData e quem popula sysmem surfaces aqui)
        return D3D_OK;
    }
    RECT full = {0, 0, (LONG)s->core.width(), (LONG)s->core.height()};
    if (!rect) rect = &full;
    float c[4] = {(float)((color >> 16) & 0xFF) / 255.0f, (float)((color >> 8) & 0xFF) / 255.0f,
                  (float)(color & 0xFF) / 255.0f, (float)((color >> 24) & 0xFF) / 255.0f};
    if (m_pass_open && m_st.rt == s && !rect) {
        VkClearAttachment ca{};
        ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        memcpy(ca.clearValue.color.float32, c, 16);
        VkClearRect cr{};
        cr.rect = {{0, 0}, {s->core.width(), s->core.height()}};
        cr.layerCount = 1;
        vkCmdClearAttachments(cmd(), 1, &ca, 1, &cr);
        return D3D_OK;
    }
    bool opened_here = false;
    if (!m_pass_open) {
        NrSurface *old_rt = m_st.rt;
        set_render_targets_internal(s, m_st.ds);
        ensure_pass_open(false, false);
        opened_here = true;
        VkClearAttachment ca{};
        ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        memcpy(ca.clearValue.color.float32, c, 16);
        VkClearRect cr{};
        cr.rect = {{rect->left, rect->top},
                   {(uint32_t)(rect->right - rect->left), (uint32_t)(rect->bottom - rect->top)}};
        cr.layerCount = 1;
        vkCmdClearAttachments(cmd(), 1, &ca, 1, &cr);
        if (opened_here) {
            end_pass();
            set_render_targets_internal(old_rt, m_st.ds);
        }
        return D3D_OK;
    }
    // pass aberto com outro rt: usa o blit pipeline (clear color via shader)
    return blit_surfaces_fill(s->core, rect, c);
}

HRESULT NrDevice::StretchRect(IDirect3DSurface9 *src, const RECT *srect, IDirect3DSurface9 *dst,
                              const RECT *drect, D3DTEXTUREFILTERTYPE filter) {
    if (!src || !dst) return D3DERR_INVALIDCALL;
    NrSurface *s = static_cast<NrSurface *>(src);
    NrSurface *d2 = static_cast<NrSurface *>(dst);
    RECT fs = {0, 0, (LONG)s->core.width(), (LONG)s->core.height()};
    RECT fd = {0, 0, (LONG)d2->core.width(), (LONG)d2->core.height()};
    return blit_surfaces(s->core, srect ? srect : &fs, d2->core, drect ? drect : &fd,
                         filter != D3DTEXF_POINT);
}

HRESULT NrDevice::UpdateSurface(IDirect3DSurface9 *src, const RECT *srect, IDirect3DSurface9 *dst,
                                const POINT *dpoint) {
    if (!src || !dst) return D3DERR_INVALIDCALL;
    NrSurface *s = static_cast<NrSurface *>(src);
    NrSurface *d2 = static_cast<NrSurface *>(dst);
    if (s->core.pool != D3DPOOL_SYSTEMMEM) return D3DERR_INVALIDCALL;
    if (s->core.fmt != d2->core.fmt) return D3DERR_INVALIDCALL;
    RECT fs = {0, 0, (LONG)s->core.width(), (LONG)s->core.height()};
    if (!srect) srect = &fs;
    POINT pt{0, 0};
    if (!dpoint) dpoint = &pt;
    // src sysmem: copia APENAS o sub-rect (buffer compacto = extent do copy)
    const FormatInfo &fi = format_info(s->core.fmt);
    uint32_t rect_w = (uint32_t)(srect->right - srect->left);
    uint32_t rect_h = (uint32_t)(srect->bottom - srect->top);
    uint32_t src_row_bytes = (s->core.width() / fi.block_width) * fi.bytes_per_block;
    uint32_t row_bytes = (rect_w / fi.block_width) * fi.bytes_per_block;
    uint8_t *data = s->sysmem_data();
    if (!data) return D3DERR_INVALIDCALL;
    VkBufferImageCopy cpy{};
    StagingRing::Alloc a;
    VkDeviceSize total = (VkDeviceSize)row_bytes * rect_h;
    if (!ring().alloc(total, 16, &a)) { partial_flush_and_wait(); if (!ring().alloc(total, 16, &a)) return D3DERR_INVALIDCALL; }
    for (uint32_t r = 0; r < rect_h; r++)
        memcpy((uint8_t *)a.mapped + (VkDeviceSize)r * row_bytes,
               data + (VkDeviceSize)(srect->top + r) * src_row_bytes +
                   (size_t)(srect->left / fi.block_width) * fi.bytes_per_block,
               row_bytes);
    VkImageLayout *layout = d2->core.layout_ptr();
    if (*layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
        transition_image(d2->core, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT);
    cpy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    cpy.imageOffset = {dpoint->x + srect->left, dpoint->y + srect->top, 0};
    cpy.imageExtent = {rect_w, rect_h, 1};
    cpy.bufferOffset = a.offset;
    cpy.bufferRowLength = 0;
    cpy.bufferImageHeight = 0;
    vkCmdCopyBufferToImage(cmd(), a.buffer, d2->core.img(), VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cpy);
    *layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    return D3D_OK;
}

HRESULT NrDevice::UpdateTexture(IDirect3DBaseTexture9 *src, IDirect3DBaseTexture9 *dst) {
    if (!src || !dst) return D3DERR_INVALIDCALL;
    NrTexture2D *s = dynamic_cast<NrTexture2D *>(static_cast<IDirect3DTexture9 *>(src));
    NrTexture2D *d2 = dynamic_cast<NrTexture2D *>(static_cast<IDirect3DTexture9 *>(dst));
    if (s && d2) {
        if (s->core.fmt != d2->core.fmt) return D3DERR_INVALIDCALL;
        for (uint32_t l = 0; l < std::min(s->core.levels, d2->core.levels); l++) {
            if (!s->core.dirty[l] && !s->core.whole_dirty) continue;
            if (!d2->core.image) break;
            upload_image_data(m_vk, cmd(), ring(), d2->core.image, d2->core.vkfmt,
                              d2->core.w, d2->core.h, 1, l, 0,
                              s->core.sysmem.data() + s->core.level_offset[l],
                              s->core.level_pitch[l], s->core.level_pitch[l] *
                                                          std::max(1u, s->core.h >> l),
                              &d2->core.layout);
            d2->core.dirty[l] = false;
            // layout para shader read
            transition_image(d2->core, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                             VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        }
        s->core.whole_dirty = false;
        return D3D_OK;
    }
    NrTextureCube *cs = dynamic_cast<NrTextureCube *>(static_cast<IDirect3DCubeTexture9 *>(src));
    NrTextureCube *cd = dynamic_cast<NrTextureCube *>(static_cast<IDirect3DCubeTexture9 *>(dst));
    if (cs && cd) {
        for (uint32_t f = 0; f < 6; f++)
            for (uint32_t l = 0; l < std::min(cs->core.levels, cd->core.levels); l++) {
                size_t face_size = (cs->core.level_size(l)) / 6;
                upload_image_data(m_vk, cmd(), ring(), cd->core.image, cd->core.vkfmt,
                                  cd->core.w, cd->core.h, 1, l, f,
                                  cs->core.sysmem.data() + cs->core.level_offset[l] + face_size * f,
                                  cs->core.level_pitch[l], cs->core.level_pitch[l] *
                                                               std::max(1u, cs->core.h >> l),
                                  &cd->core.layout);
            }
        transition_image(cd->core, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        return D3D_OK;
    }
    NrTextureVol *vs = dynamic_cast<NrTextureVol *>(static_cast<IDirect3DVolumeTexture9 *>(src));
    NrTextureVol *vd = dynamic_cast<NrTextureVol *>(static_cast<IDirect3DVolumeTexture9 *>(dst));
    if (vs && vd) {
        for (uint32_t l = 0; l < std::min(vs->core.levels, vd->core.levels); l++) {
            upload_image_data(m_vk, cmd(), ring(), vd->core.image, vd->core.vkfmt,
                              vd->core.w, vd->core.h, vd->core.d, l, 0,
                              vs->core.sysmem.data() + vs->core.level_offset[l],
                              vs->core.level_pitch[l],
                              vs->core.level_pitch[l] * std::max(1u, vs->core.h >> l),
                              &vd->core.layout);
        }
        transition_image(vd->core, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
                         VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        return D3D_OK;
    }
    return D3DERR_INVALIDCALL;
}

HRESULT NrDevice::GetRenderTargetData(IDirect3DSurface9 *rt, IDirect3DSurface9 *dst) {
    if (!rt || !dst) return D3DERR_INVALIDCALL;
    NrSurface *s = static_cast<NrSurface *>(rt);
    NrSurface *d2 = static_cast<NrSurface *>(dst);
    if (d2->core.pool != D3DPOOL_SYSTEMMEM) return D3DERR_INVALIDCALL;
    if (s->core.width() != d2->core.width() || s->core.height() != d2->core.height())
        return D3DERR_INVALIDCALL;
    // a imagem pode estar em render pass aberto: fecha e espera antes do
    // transfer (o readback cria o proprio CB)
    end_pass();
    partial_flush_and_wait();
    const FormatInfo &fi = format_info(s->core.fmt);
    uint32_t row_bytes = (s->core.width() / fi.block_width) * fi.bytes_per_block;
    if (!d2->sysmem_data_resize(s->core.height() * row_bytes)) return D3DERR_INVALIDCALL;
    VkFormat src_fmt = s->core.format();
    if (!readback_image(m_vk, s->core.img(), src_fmt, s->core.width(), s->core.height(), 0, 0,
                        *s->core.layout_ptr(), d2->sysmem_data(), row_bytes, false))
        return D3DERR_INVALIDCALL;
    return D3D_OK;
}

HRESULT NrDevice::GetFrontBufferData(UINT, IDirect3DSurface9 *dst) {
    if (!dst) return D3DERR_INVALIDCALL;
    NrSurface *d2 = static_cast<NrSurface *>(dst);
    if (d2->core.width() != m_backbuffer->core.width() ||
        d2->core.height() != m_backbuffer->core.height())
        return D3DERR_INVALIDCALL;
    const FormatInfo &fi = format_info(m_backbuffer->core.fmt);
    uint32_t row_bytes = (m_backbuffer->core.width() / fi.block_width) * fi.bytes_per_block;
    if (!d2->sysmem_data_resize(m_backbuffer->core.height() * row_bytes)) return D3DERR_INVALIDCALL;
    end_pass();
    partial_flush_and_wait();
    // GetFrontBufferData D3D9: imagem bottom-up quando fullscreen; aqui
    // (windowed) devolvemos top-down do backbuffer corrente.
    if (!readback_image(m_vk, m_backbuffer->core.img(), m_backbuffer->core.format(),
                        m_backbuffer->core.width(), m_backbuffer->core.height(), 0, 0,
                        m_backbuffer->core.layout, d2->sysmem_data(), row_bytes, false))
        return D3DERR_INVALIDCALL;
    return D3D_OK;
}

} // namespace nvr
