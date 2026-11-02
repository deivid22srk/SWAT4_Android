// Draws, construcao de pipelines, empacotamento de constantes (SM3 e FFF),
// resolve de samplers, blit de present e helper de viewport/scissor.
#include "nvr_device.h"
#include "nvr_com_impl.h"
#include <algorithm>
#include <cmath>

namespace nvr {

// --------------------------------------------------------- shader modules
VkShaderModule NrDevice::module_for(const std::vector<uint32_t> &spirv, uint64_t hash) {
    auto it = m_modules.find(hash);
    if (it != m_modules.end()) return it->second;
    VkShaderModuleCreateInfo ci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    ci.codeSize = spirv.size() * 4;
    ci.pCode = spirv.data();
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(m_vk->device(), &ci, nullptr, &m) != VK_SUCCESS) {
        NVR_ERROR("vkCreateShaderModule falhou (hash %llx)", (unsigned long long)hash);
        return VK_NULL_HANDLE;
    }
    m_modules[hash] = m;
    return m;
}

// ------------------------------------------------------------- sampler
uint32_t NrDevice::resolve_sampler(uint32_t stage) {
    DeviceState &st = m_st;
    DWORD *a = st.samp[stage];
    uint32_t packed = 0;
    auto filt = [&](DWORD f) -> uint32_t {
        switch (f) {
        case D3DTEXF_LINEAR: return 1;
        case D3DTEXF_ANISOTROPIC: return 1;
        default: return 0;
        }
    };
    packed |= filt(a[D3DSAMP_MINFILTER]);
    packed |= filt(a[D3DSAMP_MAGFILTER]) << 4;
    packed |= (a[D3DSAMP_MIPFILTER] == D3DTEXF_LINEAR ? 1u : 0u) << 8;
    auto addr = [&](DWORD f) -> uint32_t {
        switch (f) {
        case D3DTADDRESS_CLAMP: return 1;
        case D3DTADDRESS_BORDER: return 2;
        case D3DTADDRESS_MIRROR: return 3;
        default: return 0;
        }
    };
    // D3DRS_WRAPn aproxima o wrap por eixo (documentado na ANALISE)
    DWORD wrap = 0;
    if (stage < 8) wrap = st.rs[D3DRS_WRAP0 + stage];
    packed |= addr((wrap & 1) ? D3DTADDRESS_WRAP : a[D3DSAMP_ADDRESSU]) << 12;
    packed |= addr((wrap & 2) ? D3DTADDRESS_WRAP : a[D3DSAMP_ADDRESSV]) << 16;
    packed |= addr(a[D3DSAMP_ADDRESSW]) << 20;
    DWORD aniso = a[D3DSAMP_MAXANISOTROPY];
    if (filt(a[D3DSAMP_MINFILTER]) && aniso > 1) {
        uint32_t log = 0;
        while ((1u << (log + 1)) <= aniso && log < 4) log++;
        packed |= log << 24;
    } else {
        packed |= 1u << 28;   // no_aniso
    }
    return packed;
}

// -------------------------------------------------------- assinatura de input
uint64_t decl_signature_hash(const std::vector<D3DVERTEXELEMENT9> &elems) {
    uint32_t h = 2166136261u;
    for (auto &e : elems) {
        uint32_t v = e.Stream | (e.Offset << 8) | (e.Type << 20) | (e.Usage << 26) | (e.UsageIndex << 30);
        h = fnv1a_mix(h, v);
    }
    return ((uint64_t)h << 32) | fnv1a(&elems[0], elems.size() * sizeof(D3DVERTEXELEMENT9), 0x811C9DC5u);
}

bool decl_to_elements(const DeviceState &st, std::vector<D3DVERTEXELEMENT9> &out) {
    if (st.decl) {
        out = st.decl->elems;
        return !out.empty();
    }
    fvf_to_elements(st.fvf, out);
    return !out.empty();
}

// in_comps por location (para o tradutor)
void input_comps_from_elements(const std::vector<D3DVERTEXELEMENT9> &elems, uint8_t comps[32]) {
    memset(comps, 0, 32);
    for (auto &e : elems) {
        uint32_t loc = usage_to_location(e.Usage, e.UsageIndex);
        if (loc >= 32) continue;
        switch (e.Type) {
        case D3DDECLTYPE_FLOAT1: comps[loc] = 1; break;
        case D3DDECLTYPE_FLOAT2: comps[loc] = 2; break;
        case D3DDECLTYPE_FLOAT3: comps[loc] = 3; break;
        case D3DDECLTYPE_FLOAT4: case D3DDECLTYPE_UBYTE4: case D3DDECLTYPE_D3DCOLOR:
        case D3DDECLTYPE_UBYTE4N: case D3DDECLTYPE_SHORT4: case D3DDECLTYPE_SHORT4N:
        case D3DDECLTYPE_USHORT4N: case D3DDECLTYPE_DEC3N: case D3DDECLTYPE_FLOAT16_4:
            comps[loc] = 4;
            break;
        case D3DDECLTYPE_SHORT2: case D3DDECLTYPE_SHORT2N: case D3DDECLTYPE_USHORT2N:
        case D3DDECLTYPE_FLOAT16_2: comps[loc] = 2; break;
        case D3DDECLTYPE_UDEC3: comps[loc] = 3; break;
        default: comps[loc] = 4; break;
        }
    }
}

VkFormat decltype_to_vkformat(uint8_t type) {
    switch (type) {
    case D3DDECLTYPE_FLOAT1: return VK_FORMAT_R32_SFLOAT;
    case D3DDECLTYPE_FLOAT2: return VK_FORMAT_R32G32_SFLOAT;
    case D3DDECLTYPE_FLOAT3: return VK_FORMAT_R32G32B32_SFLOAT;
    case D3DDECLTYPE_FLOAT4: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case D3DDECLTYPE_D3DCOLOR: return VK_FORMAT_B8G8R8A8_UNORM;
    case D3DDECLTYPE_UBYTE4: return VK_FORMAT_R8G8B8A8_UINT;
    case D3DDECLTYPE_UBYTE4N: return VK_FORMAT_R8G8B8A8_UNORM;
    case D3DDECLTYPE_SHORT2: return VK_FORMAT_R16G16_SSCALED;
    case D3DDECLTYPE_SHORT4: return VK_FORMAT_R16G16B16A16_SSCALED;
    case D3DDECLTYPE_SHORT2N: return VK_FORMAT_R16G16_SNORM;
    case D3DDECLTYPE_SHORT4N: return VK_FORMAT_R16G16B16A16_SNORM;
    case D3DDECLTYPE_USHORT2N: return VK_FORMAT_R16G16_UNORM;
    case D3DDECLTYPE_USHORT4N: return VK_FORMAT_R16G16B16A16_UNORM;
    case D3DDECLTYPE_UDEC3: return VK_FORMAT_A2B10G10R10_UINT_PACK32;
    case D3DDECLTYPE_DEC3N: return VK_FORMAT_A2B10G10R10_SNORM_PACK32;
    case D3DDECLTYPE_FLOAT16_2: return VK_FORMAT_R16G16_SFLOAT;
    case D3DDECLTYPE_FLOAT16_4: return VK_FORMAT_R16G16B16A16_SFLOAT;
    default: return VK_FORMAT_R32G32B32A32_SFLOAT;
    }
}

// ---------------------------------------------------------------- FFF cfg
bool NrDevice::build_fff_cfg(FffVsCfg &vs, FffPsCfg &ps, const uint8_t *in_comps) {
    DeviceState &st = m_st;
    memset(&vs, 0, sizeof vs);
    memset(&ps, 0, sizeof ps);
    memcpy(vs.world, &st.xfm[D3DTS_WORLD], 64);
    memcpy(vs.view, &st.xfm[D3DTS_VIEW], 64);
    memcpy(vs.proj, &st.xfm[D3DTS_PROJECTION], 64);
    vs.mat = st.mat;
    for (int i = 0; i < 8; i++) {
        FffLightCfg &l = vs.lights[i];
        const D3DLIGHT9 &src = st.lights[i].l;
        l.enabled = st.lights[i].enabled;
        l.type = src.Type;
        memcpy(l.diffuse, &src.Diffuse, 16);
        memcpy(l.specular, &src.Specular, 16);
        memcpy(l.ambient, &src.Ambient, 16);
        memcpy(l.position, &src.Position, 16);
        memcpy(l.direction, &src.Direction, 16);
        l.range = src.Range;
        l.falloff = src.Falloff;
        l.atten0 = src.Attenuation0;
        l.atten1 = src.Attenuation1;
        l.atten2 = src.Attenuation2;
        l.theta = src.Theta;
        l.phi = src.Phi;
    }
    vs.ambient[0] = (float)((st.rs[D3DRS_AMBIENT] >> 16) & 0xFF) / 255.0f;
    vs.ambient[1] = (float)((st.rs[D3DRS_AMBIENT] >> 8) & 0xFF) / 255.0f;
    vs.ambient[2] = (float)(st.rs[D3DRS_AMBIENT] & 0xFF) / 255.0f;
    vs.ambient[3] = (float)((st.rs[D3DRS_AMBIENT] >> 24) & 0xFF) / 255.0f;
    vs.fog_vmode = st.rs[D3DRS_FOGVERTEXMODE];
    vs.fog_tmode = st.rs[D3DRS_FOGTABLEMODE];
    vs.fog_start = *(float *)&st.rs[D3DRS_FOGSTART];
    vs.fog_end = *(float *)&st.rs[D3DRS_FOGEND];
    vs.fog_density = *(float *)&st.rs[D3DRS_FOGDENSITY];
    vs.color_vertex = st.rs[D3DRS_COLORVERTEX] != 0;
    vs.normalize_normals = st.rs[D3DRS_NORMALIZENORMALS] != 0;
    vs.specular_enable = st.rs[D3DRS_SPECULARENABLE] != 0;
    vs.local_viewer = st.rs[D3DRS_LOCALVIEWER] != 0;
    vs.range_fog = st.rs[D3DRS_RANGEFOGENABLE] != 0;
    vs.diffuse_src = st.rs[D3DRS_DIFFUSEMATERIALSOURCE];
    vs.ambient_src = st.rs[D3DRS_AMBIENTMATERIALSOURCE];
    vs.specular_src = st.rs[D3DRS_SPECULARMATERIALSOURCE];
    vs.emissive_src = st.rs[D3DRS_EMISSIVEMATERIALSOURCE];
    vs.in_comps = in_comps;
    for (int i = 0; i < 8; i++) {
        vs.stage[i].coord_index = st.tss[i][D3DTSS_TEXCOORDINDEX];
        vs.stage[i].ttff = st.tss[i][D3DTSS_TEXTURETRANSFORMFLAGS];
        // luzes sao transformadas pela view (D3D9 faz isso no SetLight):
        // aqui transformamos na hora do pack (ver pack_fff_vs_ubo)
    }
    // inverse-transpose(world*view) 3x3
    {
        float wv[16];
        const D3DMATRIX &w = st.xfm[D3DTS_WORLD];
        const D3DMATRIX &v = st.xfm[D3DTS_VIEW];
        for (int i = 0; i < 4; i++)
            for (int j = 0; j < 4; j++) {
                float s = 0;
                for (int k = 0; k < 4; k++) s += w.m[i][k] * v.m[k][j];
                wv[i * 4 + j] = s;
            }
        float it[9];
        mat3_inverse_transpose(wv, it);
        memcpy(vs.world_view_it, it, sizeof it);
    }
    // PS
    ps.fog_tmode = st.rs[D3DRS_FOGTABLEMODE];
    ps.fog_vmode = st.rs[D3DRS_FOGVERTEXMODE];
    ps.fog_start = vs.fog_start;
    ps.fog_end = vs.fog_end;
    ps.fog_density = vs.fog_density;
    ps.alpha_test = st.rs[D3DRS_ALPHATESTENABLE] != 0;
    ps.alpha_cmp = st.rs[D3DRS_ALPHAFUNC];
    uint32_t active = 0;
    for (int i = 0; i < 8; i++) {
        DWORD cop = st.tss[i][D3DTSS_COLOROP];
        if (cop == D3DTOP_DISABLE) break;
        active = i + 1;
        ps.stage[i].color_arg0 = st.tss[i][D3DTSS_COLORARG0];
        ps.stage[i].color_arg1 = st.tss[i][D3DTSS_COLORARG1];
        ps.stage[i].color_arg2 = st.tss[i][D3DTSS_COLORARG2];
        ps.stage[i].color_op = cop;
        ps.stage[i].alpha_arg0 = st.tss[i][D3DTSS_ALPHAARG0];
        ps.stage[i].alpha_arg1 = st.tss[i][D3DTSS_ALPHAARG1];
        ps.stage[i].alpha_arg2 = st.tss[i][D3DTSS_ALPHAARG2];
        ps.stage[i].alpha_op = st.tss[i][D3DTSS_ALPHAOP];
        DWORD tf = st.rs[D3DRS_TEXTUREFACTOR];
        ps.stage[i].tfactor[0] = (float)((tf >> 16) & 0xFF) / 255.0f;
        ps.stage[i].tfactor[1] = (float)((tf >> 8) & 0xFF) / 255.0f;
        ps.stage[i].tfactor[2] = (float)(tf & 0xFF) / 255.0f;
        ps.stage[i].tfactor[3] = (float)((tf >> 24) & 0xFF) / 255.0f;
    }
    ps.active_stages = active;
    return true;
}

uint64_t NrDevice::fff_vs_hash_current() {
    std::vector<D3DVERTEXELEMENT9> elems;
    decl_to_elements(m_st, elems);
    uint8_t comps[32];
    input_comps_from_elements(elems, comps);
    FffVsCfg cfg{};
    FffPsCfg ps{};
    build_fff_cfg(cfg, ps, comps);
    uint8_t comps_copy[32];
    memcpy(comps_copy, comps, 32);
    // hash sobre a copia (sem ponteiros/padding indeterminado)
    return fnv1a(comps_copy, 32, fnv1a(&cfg.world[0], offsetof(FffVsCfg, in_comps)));
}
uint64_t NrDevice::fff_ps_hash_current() {
    FffPsCfg ps{};
    FffVsCfg vs{};
    std::vector<D3DVERTEXELEMENT9> elems;
    decl_to_elements(m_st, elems);
    uint8_t comps[32];
    input_comps_from_elements(elems, comps);
    build_fff_cfg(vs, ps, comps);
    return fnv1a(&ps, sizeof ps);
}

// ------------------------------------------------------------- packing
void NrDevice::pack_vs_constants(uint8_t *dst, size_t cap) {
    memset(dst, 0, cap);
    if (m_st.vs) {
        NrVS *s = static_cast<NrVS *>(m_st.vs);
        BcShader bc;
        std::string err;
        if (bc_parse((const uint32_t *)s->bytecode.data(), s->bytecode.size() / 4, bc, err)) {
            uint32_t n = std::min(bc.max_const, kMaxVsc);
            memcpy(dst, m_st.vf, n * 16);
            if (bc.uses_ib) {
                memcpy(dst + 4096, m_st.vi, sizeof m_st.vi);
                memcpy(dst + 4096 + 256, m_st.vbool, sizeof m_st.vbool);
            }
            return;
        }
        memcpy(dst, m_st.vf, kMaxVsc * 16);
    } else {
        // FFF
        pack_fff_vs_ubo((float *)dst, cap / 16);
    }
}

void NrDevice::pack_ps_constants(uint8_t *dst, size_t cap) {
    memset(dst, 0, cap);
    if (m_st.ps) {
        memcpy(dst, m_st.pf, kMaxPsc * 16);
        float *sys = (float *)(dst + kMaxPsc * 16);
        sys[0] = (float)(m_st.rs[D3DRS_ALPHAREF] & 0xFF) / 255.0f;
        sys[1] = (float)m_st.rs[D3DRS_ALPHAFUNC];
        sys[2] = 0;
        sys[3] = 0;
    } else {
        pack_fff_ps_ubo((float *)dst, cap / 16);
    }
}

// ---------------------------------------------------------------- draws
HRESULT NrDevice::DrawPrimitive(D3DPRIMITIVETYPE prim, UINT start, UINT count) {
    return draw_common(prim, 0, start, count, 0, count, false);
}
HRESULT NrDevice::DrawIndexedPrimitive(D3DPRIMITIVETYPE prim, INT base_vtx, UINT min_idx, UINT num_vtx,
                                       UINT start_idx, UINT count) {
    (void)min_idx; (void)num_vtx;
    return draw_common(prim, base_vtx, 0, count, start_idx, count, true);
}
HRESULT NrDevice::DrawPrimitiveUP(D3DPRIMITIVETYPE prim, UINT count, const void *data, UINT stride) {
    if (!data) return D3DERR_INVALIDCALL;
    UINT vtx = primitive_vertex_count(prim, count);
    VkDeviceSize bytes = (VkDeviceSize)vtx * stride;
    StagingRing::Alloc a;
    if (!ring().alloc(bytes, 16, &a)) { partial_flush_and_wait(); if (!ring().alloc(bytes, 16, &a)) return D3DERR_INVALIDCALL; }
    memcpy(a.mapped, data, bytes);
    m_up_vb = a;
    m_up_stride = stride;
    m_up_ib = {};
    HRESULT hr = draw_common(prim, 0, 0, vtx, 0, count, false);
    m_up_vb = {};
    return hr;
}
HRESULT NrDevice::DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE prim, UINT min_idx, UINT num_vtx, UINT count,
                                         const void *idx_data, D3DFORMAT idx_fmt, const void *vtx_data,
                                         UINT vtx_stride) {
    if (!idx_data || !vtx_data) return D3DERR_INVALIDCALL;
    (void)min_idx; (void)num_vtx;
    UINT idx_count = primitive_vertex_count(prim, count);
    VkDeviceSize ibytes = (VkDeviceSize)idx_count * (idx_fmt == D3DFMT_INDEX32 ? 4 : 2);
    StagingRing::Alloc ia;
    if (!ring().alloc(ibytes, 16, &ia)) return D3DERR_INVALIDCALL;
    memcpy(ia.mapped, idx_data, ibytes);
    std::vector<D3DVERTEXELEMENT9> elems;
    decl_to_elements(m_st, elems);
    uint32_t max_off = 0;
    for (auto &e : elems) max_off = std::max(max_off, (uint32_t)e.Offset + d3d9_decltype_size(e.Type));
    // D3D9 UP: os vertices sao acessados pelos indices (num_vtx); copiamos
    // num_vtx * stride a partir de 0 (indices apontam direto)
    VkDeviceSize vbytes = (VkDeviceSize)num_vtx * vtx_stride;
    StagingRing::Alloc va;
    if (!ring().alloc(vbytes, 16, &va)) return D3DERR_INVALIDCALL;
    memcpy(va.mapped, vtx_data, vbytes);
    m_up_vb = va;
    m_up_ib = ia;
    m_up_stride = vtx_stride;
    m_up_index32 = idx_fmt == D3DFMT_INDEX32;
    m_up_index_count = idx_count;
    HRESULT hr = draw_common(prim, 0, 0, num_vtx, 0, count, true);
    m_up_vb = {};
    m_up_ib = {};
    return hr;
}

HRESULT NrDevice::ProcessVertices(UINT, UINT, UINT, IDirect3DVertexBuffer9 *, IDirect3DVertexDeclaration9 *,
                                  DWORD) {
    // Sem evidencia de uso pelo SWAT4 (SM3/hw T&L) e a maquinaria de empacotar
    // saidas no FVF de destino e grande; erro explicito em vez de silencio.
    NVR_WARN("ProcessVertices nao suportado (D3DERR_INVALIDCALL)");
    return D3DERR_INVALIDCALL;
}

} // namespace nvr
