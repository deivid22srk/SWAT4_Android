// NrDevice: implementacao de IDirect3DDevice9 sobre Vulkan. Ver ANALISE.md.
#pragma once
#include "nvr_internal.h"
#include "nvr_res.h"

namespace nvr {

class NrD3D9;

class NrDevice final : public IDirect3DDevice9, public RefBase {
public:
    bool init(NrD3D9 *d3d, UINT adapter, D3DDEVTYPE type, HWND hwnd, DWORD flags,
              D3DPRESENT_PARAMETERS *pp);
    void shutdown();

    // ------------------------------------------------------------- acesso
    VkCore &vk() { return *m_vk; }
    VkPipelineLayout pipelines_layout() const { return m_pipelines.layout(); }
    VkDescriptorSetLayout pipelines_set_layout() const { return m_pipelines.set_layout(); }
    ShaderCache &shader_cache() { return m_shaders; }
    DeviceState &state() { return m_st; }
    D3DPRESENT_PARAMETERS &present_params() { return m_pp; }
    NrSurface *backbuffer() const { return m_backbuffer; }
    uint32_t frame_slot() const { return m_frame_slot; }
    void mark_destroyed(void *res);
    NrSurface *wrap_standalone(SurfCore core);
    void destroy_surface(NrSurface *s);
    void destroy_texcore(TexCore *t);
    void destroy_buffer(VkBuffer buf, VmaAllocation a);

    // frame/staging helpers
    VkCommandBuffer cmd();
    StagingRing &ring();
    void ensure_frame_started();
    void partial_flush_and_wait();
    void transition_image(SurfCore &s, VkImageLayout to, VkAccessFlags dst_access,
                          VkPipelineStageFlags dst_stage);
    void transition_image(TexCore &t, VkImageLayout to, VkAccessFlags dst_access,
                          VkPipelineStageFlags dst_stage);
    void upload_texcore(TexCore &t, bool all_levels = true);
    void upload_standalone(SurfCore &s);
    void record_buffer_upload(NrVB &b, const StagingRing::Alloc &a);
    void record_buffer_upload(NrIB &b, const StagingRing::Alloc &a);
    void upload_buffer_shadow(NrVB &b);
    void upload_buffer_shadow(NrIB &b);
    VkSampler sampler_for(uint32_t stage);
    void ensure_occlusion_pool();
    HRESULT blit_surfaces(SurfCore &src, const RECT *srect, SurfCore &dst, const RECT *drect,
                          bool linear_filter);
    HRESULT blit_surfaces_fill(SurfCore &dst, const RECT *rect, const float color[4]);
    VkPipeline make_blit_pipeline_for(VkFormat fmt, VkRenderPass rp, bool clear);
    void create_dummy_texture();

    // pass management
    void ensure_pass_open(bool may_clear_color, bool may_clear_ds);
    void end_pass();
    void set_render_targets_internal(NrSurface *rt, NrSurface *ds);
    void set_dynamic_viewport_scissor();
    void record_present_blit(uint32_t image_index);

    // -------------------------------------------------- IDirect3DDevice9
    HRESULT STDMETHODCALLTYPE TestCooperativeLevel() override;
    UINT STDMETHODCALLTYPE GetAvailableTextureMem() override;
    HRESULT STDMETHODCALLTYPE EvictManagedResources() override;
    HRESULT STDMETHODCALLTYPE GetDirect3D(IDirect3D9 **out) override;
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(D3DCAPS9 *out) override;
    HRESULT STDMETHODCALLTYPE GetDisplayMode(UINT sw, D3DDISPLAYMODE *out) override;
    HRESULT STDMETHODCALLTYPE GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *out) override;
    HRESULT STDMETHODCALLTYPE SetCursorProperties(UINT x, UINT y, IDirect3DSurface9 *bmp) override;
    void STDMETHODCALLTYPE SetCursorPosition(int x, int y, DWORD flags) override;
    BOOL STDMETHODCALLTYPE ShowCursor(BOOL show) override;
    HRESULT STDMETHODCALLTYPE CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *pp, IDirect3DSwapChain9 **out) override;
    HRESULT STDMETHODCALLTYPE GetSwapChain(UINT i, IDirect3DSwapChain9 **out) override;
    UINT STDMETHODCALLTYPE GetNumberOfSwapChains() override;
    HRESULT STDMETHODCALLTYPE Reset(D3DPRESENT_PARAMETERS *pp) override;
    HRESULT STDMETHODCALLTYPE Present(const RECT *src, const RECT *dst, HWND ovr, const RGNDATA *rgn) override;
    HRESULT STDMETHODCALLTYPE GetBackBuffer(UINT swap, UINT i, D3DBACKBUFFER_TYPE type, IDirect3DSurface9 **out) override;
    HRESULT STDMETHODCALLTYPE GetRasterStatus(UINT sw, D3DRASTER_STATUS *out) override;
    HRESULT STDMETHODCALLTYPE SetDialogBoxMode(BOOL enable) override;
    void STDMETHODCALLTYPE SetGammaRamp(UINT swap, DWORD flags, const D3DGAMMARAMP *ramp) override;
    void STDMETHODCALLTYPE GetGammaRamp(UINT swap, D3DGAMMARAMP *ramp) override;
    HRESULT STDMETHODCALLTYPE CreateTexture(UINT w, UINT h, UINT levels, DWORD usage, D3DFORMAT fmt,
                                            D3DPOOL pool, IDirect3DTexture9 **out, HANDLE *shared) override;
    HRESULT STDMETHODCALLTYPE CreateVolumeTexture(UINT w, UINT h, UINT d, UINT levels, DWORD usage,
                                                  D3DFORMAT fmt, D3DPOOL pool,
                                                  IDirect3DVolumeTexture9 **out, HANDLE *shared) override;
    HRESULT STDMETHODCALLTYPE CreateCubeTexture(UINT size, UINT levels, DWORD usage, D3DFORMAT fmt,
                                                D3DPOOL pool, IDirect3DCubeTexture9 **out, HANDLE *shared) override;
    HRESULT STDMETHODCALLTYPE CreateVertexBuffer(UINT len, DWORD usage, DWORD fvf, D3DPOOL pool,
                                                 IDirect3DVertexBuffer9 **out, HANDLE *shared) override;
    HRESULT STDMETHODCALLTYPE CreateIndexBuffer(UINT len, DWORD usage, D3DFORMAT fmt, D3DPOOL pool,
                                                IDirect3DIndexBuffer9 **out, HANDLE *shared) override;
    HRESULT STDMETHODCALLTYPE CreateRenderTarget(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms,
                                                 DWORD quality, BOOL lockable, IDirect3DSurface9 **out,
                                                 HANDLE *shared) override;
    HRESULT STDMETHODCALLTYPE CreateDepthStencilSurface(UINT w, UINT h, D3DFORMAT fmt, D3DMULTISAMPLE_TYPE ms,
                                                        DWORD quality, BOOL discard_z,
                                                        IDirect3DSurface9 **out, HANDLE *shared) override;
    HRESULT STDMETHODCALLTYPE UpdateSurface(IDirect3DSurface9 *src, const RECT *srect,
                                            IDirect3DSurface9 *dst, const POINT *dpoint) override;
    HRESULT STDMETHODCALLTYPE UpdateTexture(IDirect3DBaseTexture9 *src, IDirect3DBaseTexture9 *dst) override;
    HRESULT STDMETHODCALLTYPE GetRenderTargetData(IDirect3DSurface9 *rt, IDirect3DSurface9 *dst) override;
    HRESULT STDMETHODCALLTYPE GetFrontBufferData(UINT sw, IDirect3DSurface9 *dst) override;
    HRESULT STDMETHODCALLTYPE StretchRect(IDirect3DSurface9 *src, const RECT *srect,
                                          IDirect3DSurface9 *dst, const RECT *drect,
                                          D3DTEXTUREFILTERTYPE filter) override;
    HRESULT STDMETHODCALLTYPE ColorFill(IDirect3DSurface9 *surf, const RECT *rect, D3DCOLOR color) override;
    HRESULT STDMETHODCALLTYPE CreateOffscreenPlainSurface(UINT w, UINT h, D3DFORMAT fmt, D3DPOOL pool,
                                                          IDirect3DSurface9 **out, HANDLE *shared) override;
    HRESULT STDMETHODCALLTYPE SetRenderTarget(DWORD idx, IDirect3DSurface9 *rt) override;
    HRESULT STDMETHODCALLTYPE GetRenderTarget(DWORD idx, IDirect3DSurface9 **out) override;
    HRESULT STDMETHODCALLTYPE SetDepthStencilSurface(IDirect3DSurface9 *ds) override;
    HRESULT STDMETHODCALLTYPE GetDepthStencilSurface(IDirect3DSurface9 **out) override;
    HRESULT STDMETHODCALLTYPE BeginScene() override;
    HRESULT STDMETHODCALLTYPE EndScene() override;
    HRESULT STDMETHODCALLTYPE Clear(DWORD count, const D3DRECT *rects, DWORD flags,
                                    D3DCOLOR color, float z, DWORD stencil) override;
    HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE t, const D3DMATRIX *m) override;
    HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE t, D3DMATRIX *m) override;
    HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE t, const D3DMATRIX *m) override;
    HRESULT STDMETHODCALLTYPE SetViewport(const D3DVIEWPORT9 *vp) override;
    HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9 *vp) override;
    HRESULT STDMETHODCALLTYPE SetMaterial(const D3DMATERIAL9 *m) override;
    HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9 *m) override;
    HRESULT STDMETHODCALLTYPE SetLight(DWORD idx, const D3DLIGHT9 *l) override;
    HRESULT STDMETHODCALLTYPE GetLight(DWORD idx, D3DLIGHT9 *l) override;
    HRESULT STDMETHODCALLTYPE LightEnable(DWORD idx, BOOL enable) override;
    HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD idx, BOOL *enable) override;
    HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD idx, const float *plane) override;
    HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD idx, float *plane) override;
    HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE rs, DWORD value) override;
    HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE rs, DWORD *value) override;
    HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE type, IDirect3DStateBlock9 **out) override;
    HRESULT STDMETHODCALLTYPE BeginStateBlock() override;
    HRESULT STDMETHODCALLTYPE EndStateBlock(IDirect3DStateBlock9 **out) override;
    HRESULT STDMETHODCALLTYPE SetClipStatus(const D3DCLIPSTATUS9 *cs) override;
    HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9 *cs) override;
    HRESULT STDMETHODCALLTYPE GetTexture(DWORD stage, IDirect3DBaseTexture9 **tex) override;
    HRESULT STDMETHODCALLTYPE SetTexture(DWORD stage, IDirect3DBaseTexture9 *tex) override;
    HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE tss, DWORD *v) override;
    HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE tss, DWORD v) override;
    HRESULT STDMETHODCALLTYPE GetSamplerState(DWORD stage, D3DSAMPLERSTATETYPE ss, DWORD *v) override;
    HRESULT STDMETHODCALLTYPE SetSamplerState(DWORD stage, D3DSAMPLERSTATETYPE ss, DWORD v) override;
    HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD *passes) override;
    HRESULT STDMETHODCALLTYPE SetPaletteEntries(UINT pal, const PALETTEENTRY *entries) override;
    HRESULT STDMETHODCALLTYPE GetPaletteEntries(UINT pal, PALETTEENTRY *entries) override;
    HRESULT STDMETHODCALLTYPE SetCurrentTexturePalette(UINT pal) override;
    HRESULT STDMETHODCALLTYPE GetCurrentTexturePalette(UINT *pal) override;
    HRESULT STDMETHODCALLTYPE SetScissorRect(const RECT *rect) override;
    HRESULT STDMETHODCALLTYPE GetScissorRect(RECT *rect) override;
    HRESULT STDMETHODCALLTYPE SetSoftwareVertexProcessing(BOOL sw) override;
    BOOL STDMETHODCALLTYPE GetSoftwareVertexProcessing() override;
    HRESULT STDMETHODCALLTYPE SetNPatchMode(float nSegments) override;
    float STDMETHODCALLTYPE GetNPatchMode() override;
    HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE prim, UINT start, UINT count) override;
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE prim, INT base_vtx, UINT min_idx,
                                                   UINT num_vtx, UINT start_idx, UINT count) override;
    HRESULT STDMETHODCALLTYPE DrawPrimitiveUP(D3DPRIMITIVETYPE prim, UINT count, const void *data,
                                              UINT stride) override;
    HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveUP(D3DPRIMITIVETYPE prim, UINT min_idx, UINT num_vtx,
                                                     UINT count, const void *idx_data,
                                                     D3DFORMAT idx_fmt, const void *vtx_data,
                                                     UINT vtx_stride) override;
    HRESULT STDMETHODCALLTYPE ProcessVertices(UINT src_start, UINT dst_start, UINT count,
                                              IDirect3DVertexBuffer9 *dst, IDirect3DVertexDeclaration9 *decl,
                                              DWORD flags) override;
    HRESULT STDMETHODCALLTYPE CreateVertexDeclaration(const D3DVERTEXELEMENT9 *elems,
                                                      IDirect3DVertexDeclaration9 **out) override;
    HRESULT STDMETHODCALLTYPE SetVertexDeclaration(IDirect3DVertexDeclaration9 *decl) override;
    HRESULT STDMETHODCALLTYPE GetVertexDeclaration(IDirect3DVertexDeclaration9 **out) override;
    HRESULT STDMETHODCALLTYPE SetFVF(DWORD fvf) override;
    HRESULT STDMETHODCALLTYPE GetFVF(DWORD *fvf) override;
    HRESULT STDMETHODCALLTYPE CreateVertexShader(const DWORD *code, IDirect3DVertexShader9 **out) override;
    HRESULT STDMETHODCALLTYPE SetVertexShader(IDirect3DVertexShader9 *vs) override;
    HRESULT STDMETHODCALLTYPE GetVertexShader(IDirect3DVertexShader9 **vs) override;
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantF(UINT reg, const float *data, UINT count) override;
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantF(UINT reg, float *data, UINT count) override;
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantI(UINT reg, const int *data, UINT count) override;
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantI(UINT reg, int *data, UINT count) override;
    HRESULT STDMETHODCALLTYPE SetVertexShaderConstantB(UINT reg, const BOOL *data, UINT count) override;
    HRESULT STDMETHODCALLTYPE GetVertexShaderConstantB(UINT reg, BOOL *data, UINT count) override;
    HRESULT STDMETHODCALLTYPE SetStreamSource(UINT idx, IDirect3DVertexBuffer9 *vb, UINT off, UINT stride) override;
    HRESULT STDMETHODCALLTYPE GetStreamSource(UINT idx, IDirect3DVertexBuffer9 **vb, UINT *off, UINT *stride) override;
    HRESULT STDMETHODCALLTYPE SetStreamSourceFreq(UINT idx, UINT freq) override;
    HRESULT STDMETHODCALLTYPE GetStreamSourceFreq(UINT idx, UINT *freq) override;
    HRESULT STDMETHODCALLTYPE SetIndices(IDirect3DIndexBuffer9 *ib) override;
    HRESULT STDMETHODCALLTYPE GetIndices(IDirect3DIndexBuffer9 **ib) override;
    HRESULT STDMETHODCALLTYPE CreatePixelShader(const DWORD *code, IDirect3DPixelShader9 **out) override;
    HRESULT STDMETHODCALLTYPE SetPixelShader(IDirect3DPixelShader9 *ps) override;
    HRESULT STDMETHODCALLTYPE GetPixelShader(IDirect3DPixelShader9 **ps) override;
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantF(UINT reg, const float *data, UINT count) override;
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantF(UINT reg, float *data, UINT count) override;
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantI(UINT reg, const int *data, UINT count) override;
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantI(UINT reg, int *data, UINT count) override;
    HRESULT STDMETHODCALLTYPE SetPixelShaderConstantB(UINT reg, const BOOL *data, UINT count) override;
    HRESULT STDMETHODCALLTYPE GetPixelShaderConstantB(UINT reg, BOOL *data, UINT count) override;
    HRESULT STDMETHODCALLTYPE DrawRectPatch(UINT handle, const float *seg, const D3DRECTPATCH_INFO *info) override;
    HRESULT STDMETHODCALLTYPE DrawTriPatch(UINT handle, const float *seg, const D3DTRIPATCH_INFO *info) override;
    HRESULT STDMETHODCALLTYPE DeletePatch(UINT handle) override;
    HRESULT STDMETHODCALLTYPE CreateQuery(D3DQUERYTYPE type, IDirect3DQuery9 **out) override;

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override;

    // ------------------------------------------------------ internals (draw)
    VkPipeline build_pipeline(const PipelineKey &key, const XlateOut *vsx, const XlateOut *psx);
    bool draw_common(D3DPRIMITIVETYPE prim, INT base_vtx, UINT start_vtx, UINT num_vtx,
                     UINT start_idx, UINT count, bool indexed);
    void pack_vs_constants(uint8_t *dst, size_t cap);
    void pack_ps_constants(uint8_t *dst, size_t cap);
    uint64_t fff_vs_hash_current();
    uint64_t fff_ps_hash_current();
    bool build_fff_cfg(FffVsCfg &vs, FffPsCfg &ps, const uint8_t *in_comps);
    uint32_t resolve_sampler(uint32_t stage);
    void issue_query_slots();

private:
    NrD3D9 *m_d3d = nullptr;
    VkCore *m_vk = nullptr;
    Swapchain m_swap;
    FrameCtx m_frames[3];
    uint32_t m_nframes = 2, m_frame_slot = 0, m_started_slot = 0xFFFFFFFFu;
    uint64_t m_frame_no = 0;
    bool m_cb_open = false;
    PassCache m_passes;
    SamplerCache m_samplers;
    PipelineCache m_pipelines;
    DescAllocator m_descs;
    FramebufferCache m_framebuffers;
    ShaderCache m_shaders;
    D3DPRESENT_PARAMETERS m_pp = {};
    HWND m_hwnd = nullptr;
    DWORD m_flags = 0;
    NrSurface *m_backbuffer = nullptr;
    NrSurface *m_autods = nullptr;
    DeviceState m_st;
    bool m_pass_open = false;
    bool m_rt_needs_clear = false, m_ds_needs_clear = false;
    float m_clear_color[4] = {0, 0, 0, 1};
    float m_clear_z = 1.0f;
    uint32_t m_clear_stencil = 0;
    uint32_t m_ds_format_d3d = D3DFMT_D24S8;
    uint64_t m_stat_draws = 0;
    // gamma
    D3DGAMMARAMP m_ramp{};
    bool m_ramp_identity = true;
    // blit
    VkPipeline m_blit_pipe = VK_NULL_HANDLE;
    VkPipeline m_clear_pipe = VK_NULL_HANDLE;
    VkShaderModule m_blit_vs = VK_NULL_HANDLE, m_blit_fs = VK_NULL_HANDLE, m_clear_fs = VK_NULL_HANDLE;
    VkImage m_dummy_image = VK_NULL_HANDLE;
    VmaAllocation m_dummy_alloc = VK_NULL_HANDLE;
    VkImageView m_dummy_view = VK_NULL_HANDLE;
    VkImageLayout m_dummy_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkFramebuffer m_swap_fb[8] = {};
    uint32_t m_sc_gen = 0;
    StagingRing::Alloc m_up_vb{}, m_up_ib{};
    uint32_t m_up_stride = 0;
    bool m_up_index32 = false;
    uint32_t m_up_index_count = 0;
    // queries
    struct QueryPool { VkQueryPool pool; uint32_t used; };
    std::vector<QueryPool> m_occlusion_pools;
    // cursor
    int m_cursor_counter = 0;
    // cache de shader modules
    std::unordered_map<uint64_t, VkShaderModule> m_modules;
    uint32_t m_last_samples = 1;
    // state block em gravacao
    bool m_recording_stateblock = false;
    std::vector<uint8_t> m_stateblock_data;
    void sb_put(const void *p, size_t n);   // grava no stateblock em gravacao
    template <typename T> void sb_rec(uint32_t op, const T &v) {
        if (m_recording_stateblock) {
            sb_put(&op, 4);
            sb_put(&v, sizeof v);
        }
    }
    void sb_capture_subset(int kind);   // 0=all 1=vertex 2=pixel
    // fences de EVENT query
    std::vector<VkFence> m_event_fences;
    VkFence submit_partial_fence();
    uint64_t timestamp_ns();
    VkQueryPool &occlusion_pool(uint32_t slot) { return m_occlusion_pools[slot].pool; }

    bool create_backbuffer_and_ds();
    VkShaderModule module_for(const std::vector<uint32_t> &spirv, uint64_t hash);
    void destroy_all_pools();
    friend class NrSurface;
    friend class NrQuery;
    friend class NrStateBlock;
};

// IDirect3D9
class NrD3D9 final : public IDirect3D9, public RefBase {
public:
    NrD3D9() = default;
    bool ensure_core();
    VkCore &core() { return m_own_core; }
    ;
    HRESULT STDMETHODCALLTYPE RegisterSoftwareDevice(void *fn) override;
    UINT STDMETHODCALLTYPE GetAdapterCount() override;
    HRESULT STDMETHODCALLTYPE GetAdapterIdentifier(UINT adapter, DWORD flags, D3DADAPTER_IDENTIFIER9 *out) override;
    UINT STDMETHODCALLTYPE GetAdapterModeCount(UINT adapter, D3DFORMAT fmt) override;
    HRESULT STDMETHODCALLTYPE EnumAdapterModes(UINT adapter, D3DFORMAT fmt, UINT mode, D3DDISPLAYMODE *out) override;
    HRESULT STDMETHODCALLTYPE GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE *out) override;
    HRESULT STDMETHODCALLTYPE CheckDeviceType(UINT adapter, D3DDEVTYPE type, D3DFORMAT disp, D3DFORMAT back, BOOL windowed) override;
    HRESULT STDMETHODCALLTYPE CheckDeviceFormat(UINT adapter, D3DDEVTYPE type, D3DFORMAT disp, DWORD usage,
                                                D3DRESOURCETYPE rtype, D3DFORMAT fmt) override;
    HRESULT STDMETHODCALLTYPE CheckDeviceMultiSampleType(UINT adapter, D3DDEVTYPE type, D3DFORMAT back,
                                                         BOOL windowed, D3DMULTISAMPLE_TYPE ms, DWORD *quality) override;
    HRESULT STDMETHODCALLTYPE CheckDepthStencilMatch(UINT adapter, D3DDEVTYPE type, D3DFORMAT disp,
                                                     D3DFORMAT rt, D3DFORMAT ds) override;
    HRESULT STDMETHODCALLTYPE CheckDeviceFormatConversion(UINT adapter, D3DDEVTYPE type, D3DFORMAT src, D3DFORMAT dst) override;
    HRESULT STDMETHODCALLTYPE GetDeviceCaps(UINT adapter, D3DDEVTYPE type, D3DCAPS9 *out) override;
    HMONITOR STDMETHODCALLTYPE GetAdapterMonitor(UINT adapter) override;
    HRESULT STDMETHODCALLTYPE CreateDevice(UINT adapter, D3DDEVTYPE type, HWND hwnd, DWORD flags,
                                           D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out) override;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override;
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs; }
    ULONG STDMETHODCALLTYPE Release() override;
private:
    VkCore m_own_core{};
    VkCore *m_core = nullptr;
    friend class NrDevice;
};

// fills D3DCAPS9 coerente com o que este backend implementa
void fill_device_caps(D3DCAPS9 *caps, const VkCore &vk);
DWORD pack_raster_state(const DeviceState &st);
DWORD pack_ds_state(const DeviceState &st);
DWORD pack_blend_state(const DeviceState &st);
void fill_default_state(DeviceState &s);

} // namespace nvr
