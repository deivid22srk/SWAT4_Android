// Objetos COM D3D9 do native-render e estado central do device.
// Todas as classes derivam das interfaces dos headers vendidos (ABI identica
// a que a bridge do runtime usa).
#pragma once
#include "../nvr_common.h"
#include "../vulkan/nvr_vk.h"
#include "../vulkan/nvr_gfx.h"
#include "../shader/nvr_compile.h"
#include "../shader/nvr_fff.h"
#include <d3d9.h>
#include <array>

namespace nvr {

class NrDevice;

// base de refcount compartilhada
struct RefBase {
    std::atomic<uint32_t> refs{1};
    void *owner = nullptr;   // NrDevice* nos recursos; unused no NrD3D9
};

// QueryInterface trivial para classes que so expõem a propria interface
#define NVR_QI_SELF_ONLY(I) \
    if (riid == __uuidof(I) || riid == __uuidof(IUnknown)) { \
        *out = (I *)this; \
        AddRef(); \
        return S_OK; \
    } \
    *out = nullptr; \
    return E_NOINTERFACE;


struct VkCore;
class NrDevice;

// -------------------------------------------------------------- shader cache
// Compila bytecode D3D9 -> SPIR-V (com cache por hash do bytecode + assinatura
// de inputs). Instancia por device.
class ShaderCache {
public:
    void init(NrDevice *dev) { m_dev = dev; }
    // vs: assinatura de inputs atual e usada na variante
    const XlateOut *get_vs(const void *bc, size_t len, bool flat);
    const XlateOut *get_ps(const void *bc, size_t len, bool alpha_test, uint32_t cmp);
    const XlateOut *get_fff_vs(uint64_t sig_hash, const FffVsCfg &cfg, const uint8_t *in_comps);
    const XlateOut *get_fff_ps(const FffPsCfg &cfg);
private:
    NrDevice *m_dev = nullptr;
    std::unordered_map<uint64_t, std::pair<std::vector<uint8_t>, XlateOut>> m_vs, m_ps;
    std::unordered_map<uint64_t, std::pair<FffVsCfg, XlateOut>> m_fff_vs;
    std::unordered_map<uint64_t, std::pair<FffPsCfg, XlateOut>> m_fff_ps;
    std::mutex m_mutex;
};

// ---------------------------------------------------------------- TexCore
struct TexCore {
    D3DRESOURCETYPE type = D3DRTYPE_TEXTURE;
    uint32_t w = 0, h = 0, d = 0, levels = 1;
    D3DFORMAT fmt = D3DFMT_UNKNOWN;
    VkFormat vkfmt = VK_FORMAT_UNDEFINED;
    DWORD usage = 0;
    D3DPOOL pool = D3DPOOL_DEFAULT;
    bool autogen = false;
    bool is_depth = false;
    DWORD autogen_lod = 0;
    D3DTEXTUREFILTERTYPE autogen_filter = D3DTEXF_LINEAR;

    VkImage image = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkImageView view_sampled = VK_NULL_HANDLE;   // todos os mips/face
    VkImageView view_rt = VK_NULL_HANDLE;        // color attachment (level 0)
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool needs_upload = true;                    // device copy pendente (managed)

    // copia de sistema (MANAGED/SYSTEMMEM): row-major com pitch D3D
    std::vector<uint8_t> sysmem;
    std::array<uint32_t, 16> level_pitch{};
    std::array<size_t, 16> level_offset{};
    bool dirty[16] = {};
    bool whole_dirty = false;

    size_t level_size(uint32_t l) const;
    void compute_level_layout();
};

// ------------------------------------------------------------- SurfCore
// Surface: nivel de textura OU imagem independente (backbuffer, DS, RT).
struct SurfCore {
    TexCore *tex = nullptr;
    uint32_t level = 0, layer = 0;
    // standalone
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation alloc = VK_NULL_HANDLE;
    VkImageView view_sampled = VK_NULL_HANDLE, view_rt = VK_NULL_HANDLE, view_ds = VK_NULL_HANDLE;
    VkFormat vkfmt = VK_FORMAT_UNDEFINED;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint32_t w = 0, h = 0;
    D3DFORMAT fmt = D3DFMT_UNKNOWN;
    std::vector<uint8_t> sysmem;   // surfaces standalone em SYSTEMMEM
    VkFormat actual_fmt = VK_FORMAT_UNDEFINED;   // depth escolhido de verdade
    DWORD usage = 0;
    D3DPOOL pool = D3DPOOL_DEFAULT;
    bool is_depth = false;
    bool has_stencil = false;

    uint32_t width() const { return tex ? std::max(1u, tex->w >> level) : w; }
    uint32_t height() const { return tex ? std::max(1u, tex->h >> level) : h; }
    VkFormat format() const { return tex ? tex->vkfmt : (is_depth ? actual_fmt : vkfmt); }
    VkImage img() const { return tex ? tex->image : image; }
    VkImageLayout *layout_ptr() { return tex ? &tex->layout : &layout; }
};

class NrVB;
class NrIB;
class NrSurface;
class NrDecl;
// ---------------------------------------------------------------- estado
struct NrStream {
    NrVB *vb = nullptr;
    UINT offset = 0, stride = 0;
    UINT freq = 1;   // instancing D3D9
};

struct DeviceState {
    DWORD rs[256] = {};
    DWORD tss[8][33] = {};
    DWORD samp[16][14] = {};
    D3DMATRIX xfm[256] = {};
    D3DMATERIAL9 mat = {};
    struct Light { D3DLIGHT9 l{}; bool enabled = false; } lights[8];
    float clip_planes[6][4] = {};
    IDirect3DVertexShader9 *vs = nullptr;
    IDirect3DPixelShader9 *ps = nullptr;
    float vf[256][4] = {};
    int vi[16][4] = {};
    BOOL vbool[16] = {};
    float pf[224][4] = {};
    int pi[16][4] = {};
    BOOL pb[16] = {};
    NrStream streams[16];
    NrIB *ib = nullptr;
    IDirect3DBaseTexture9 *tex[16] = {};
    NrSurface *rt = nullptr;
    NrSurface *ds = nullptr;
    D3DVIEWPORT9 vp = {0, 0, 1280, 720, 0, 1};
    RECT scissor = {};
    bool scissor_enable = false;
    DWORD fvf = 0;
    NrDecl *decl = nullptr;
    bool swvp = false;
    bool scene_open = false;
};

// defaults D3D9 dos render states / TSS / sampler
void fill_default_state(DeviceState &s);

// helpers compartilhados entre os .cpp da camada d3d9 (definicoes em
// nvr_device2.cpp, nvr_draw.cpp e nvr_pack.cpp)
void fvf_to_elements(DWORD fvf, std::vector<D3DVERTEXELEMENT9> &out);
uint32_t d3d9_decltype_size(uint8_t type);
bool decl_to_elements(const DeviceState &st, std::vector<D3DVERTEXELEMENT9> &out);
uint64_t decl_signature_hash(const std::vector<D3DVERTEXELEMENT9> &elems);
void input_comps_from_elements(const std::vector<D3DVERTEXELEMENT9> &elems, uint8_t comps[32]);
VkFormat decltype_to_vkformat(uint8_t type);
uint32_t usage_to_location(uint32_t usage, uint32_t index);
UINT primitive_vertex_count(D3DPRIMITIVETYPE prim, UINT count);
VkPrimitiveTopology primitive_topology(D3DPRIMITIVETYPE prim);
void mat3_inverse_transpose(const float m[16], float out[9]);
void pack_fff_vs_ubo(float *dst, uint32_t vec4_count, const FffVsCfg &cfg, bool lights_view_space);
void pack_fff_ps_ubo(float *dst, uint32_t vec4_count, const FffPsCfg &cfg, const DeviceState &st);
void pack_fff_vs_ubo(float *dst, uint32_t vec4_count);
void pack_fff_ps_ubo(float *dst, uint32_t vec4_count);
NrDevice *g_current_device();

// pipeline key
struct PipelineKey {
    uint64_t vs = 0;       // hash do SPIR-V vs (0 = FFF: fff_vs_hash)
    uint64_t ps = 0;
    uint64_t fff_vs_hash = 0, fff_ps_hash = 0;
    uint64_t decl_hash = 0;
    uint32_t topology = 0;
    uint32_t raster = 0;   // packed: fill|cull|flat|scissor|bias
    float depth_bias = 0, slope_bias = 0;
    uint32_t ds = 0;       // packed zenable/func/write/stencil ops (CW)
    uint32_t ds_ccw = 0;   // ops CCW (two-sided stencil; 0 = sem two-sided)
    uint32_t two_sided = 0;
    uint32_t stencil_mask = 0, stencil_write_mask = 0;
    uint32_t stencil_ref = 0;
    uint32_t blend = 0;    // packed
    uint32_t color_write = 0;
    uint32_t alpha_cmp = 0;
    uint32_t color_fmt = 0, ds_fmt = 0;
    uint32_t samples = 1;
    uint64_t hash() const;
    bool operator==(const PipelineKey &o) const;
};

} // namespace nvr
