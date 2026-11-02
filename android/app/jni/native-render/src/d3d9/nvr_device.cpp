// NrDevice: ciclo de vida, frames, render passes, present, gamma e todos os
// setters de estado. Draws/pipelines em nvr_draw.cpp; recursos em nvr_res.cpp.
#include "nvr_device.h"
#include "nvr_com_impl.h"
#include <SDL2/SDL.h>

namespace nvr {

// ------------------------------------------------------------------ init
bool NrDevice::init(NrD3D9 *d3d, UINT, D3DDEVTYPE, HWND hwnd, DWORD flags,
                    D3DPRESENT_PARAMETERS *pp) {
    m_d3d = d3d;
    m_hwnd = hwnd;
    m_flags = flags;
    m_pp = *pp;

    m_vk = &d3d->core();

    SDL_Window *win = (SDL_Window *)hwnd;
    if (!m_swap.init(m_vk, win)) { NVR_ERROR("swapchain init falhou"); return false; }

    Config::get().load(cache_dir());
    m_nframes = (uint32_t)Config::get().get_int("frames_in_flight", kFramesInFlightDefault);
    m_nframes = std::min(m_nframes, 3u);
    for (uint32_t i = 0; i < m_nframes; i++)
        if (!m_frames[i].init(m_vk, i)) { NVR_ERROR("frame %u init falhou", i); return false; }

    m_passes.init(m_vk);
    m_samplers.init(m_vk);
    if (!m_pipelines.init(m_vk, cache_dir())) { NVR_ERROR("pipeline cache init falhou"); return false; }
    m_descs.init(m_vk, &m_pipelines);
    m_shaders.init(this);

    fill_default_state(m_st);
    m_st.vp = {0, 0, (DWORD)pp->BackBufferWidth, (DWORD)pp->BackBufferHeight, 0.0f, 1.0f};
    m_st.scissor = {0, 0, (LONG)pp->BackBufferWidth, (LONG)pp->BackBufferHeight};

    // gamma identidade
    for (uint32_t i = 0; i < 256; i++) {
        m_ramp.red[i] = m_ramp.green[i] = m_ramp.blue[i] = (WORD)(i * 257);
    }

    if (!create_backbuffer_and_ds()) return false;
    NVR_INFO("device criado: %ux%u fmt=%d ds=%d flags=%x", pp->BackBufferWidth, pp->BackBufferHeight,
             pp->BackBufferFormat, pp->AutoDepthStencilFormat, flags);
    return true;
}

void NrDevice::shutdown() {
    end_pass();
    if (m_cb_open && m_frames[m_started_slot].cb()) {
        m_frames[m_started_slot].end();
        m_cb_open = false;
    }
    // espera todos os frames
    for (uint32_t i = 0; i < m_nframes; i++) m_frames[i].wait_fence();
    m_pipelines.shutdown();
    for (auto &qp : m_occlusion_pools)
        if (qp.pool) vkDestroyQueryPool(m_vk->device(), qp.pool, nullptr);
    m_occlusion_pools.clear();
    for (auto &kv : m_modules)
        if (kv.second) vkDestroyShaderModule(m_vk->device(), kv.second, nullptr);
    m_modules.clear();
    if (m_st.rt) { m_st.rt->Release(); m_st.rt = nullptr; }
    if (m_st.ds) { m_st.ds->Release(); m_st.ds = nullptr; }
    if (m_st.vs) m_st.vs->Release();
    if (m_st.ps) m_st.ps->Release();
    if (m_st.decl) m_st.decl->Release();
    for (uint32_t i = 0; i < 16; i++)
        if (m_st.tex[i]) m_st.tex[i]->Release();
    if (m_backbuffer) { m_backbuffer->Release(); m_backbuffer = nullptr; }
    if (m_autods) { m_autods->Release(); m_autods = nullptr; }
    for (auto &fenc : m_event_fences)
        if (fenc) vkDestroyFence(m_vk->device(), fenc, nullptr);
    m_event_fences.clear();
    for (uint32_t i = 0; i < m_nframes; i++) m_frames[i].shutdown();
    m_passes.shutdown();
    m_samplers.shutdown();
    m_swap.shutdown();
}

bool NrDevice::create_backbuffer_and_ds() {
    destroy_all_pools();
    // backbuffer standalone
    SurfCore sc;
    sc.w = m_pp.BackBufferWidth ? m_pp.BackBufferWidth : 1280;
    sc.h = m_pp.BackBufferHeight ? m_pp.BackBufferHeight : 720;
    sc.fmt = m_pp.BackBufferFormat == D3DFMT_UNKNOWN ? D3DFMT_X8R8G8B8 : m_pp.BackBufferFormat;
    const FormatInfo &fi = format_info(sc.fmt);
    sc.vkfmt = fi.vk;
    sc.usage = D3DUSAGE_RENDERTARGET;
    sc.pool = D3DPOOL_DEFAULT;
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = sc.vkfmt;
    ici.extent = {sc.w, sc.h, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    sc.alloc = mem_alloc_image(m_vk, ici, &sc.image);
    if (!sc.alloc) { NVR_ERROR("backbuffer %ux%u falhou", sc.w, sc.h); return false; }
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = sc.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = sc.vkfmt;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(m_vk->device(), &vi, nullptr, &sc.view_rt);
    vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    vkCreateImageView(m_vk->device(), &vi, nullptr, &sc.view_sampled);

    m_backbuffer = new NrSurface();
    m_backbuffer->owner = (void *)this;
    m_backbuffer->core = std::move(sc);
    m_backbuffer->destroy_cb = [s = m_backbuffer, this]() {
        destroy_surface(const_cast<NrSurface *>(s));
    };
    NVR_TRACE("backbuffer %ux%u", sc.w, sc.h);

    // autodepthstencil
    if (m_pp.EnableAutoDepthStencil) {
        SurfCore dc;
        dc.w = sc.w;
        dc.h = sc.h;
        dc.fmt = m_pp.AutoDepthStencilFormat;
        dc.is_depth = true;
        dc.pool = D3DPOOL_DEFAULT;
        bool stencil = false;
        dc.actual_fmt = choose_depth_format(m_vk->physical(), dc.fmt, &stencil);
        dc.has_stencil = stencil;
        if (dc.actual_fmt == VK_FORMAT_UNDEFINED) {
            NVR_WARN("depth %d sem suporte; usando D16", dc.fmt);
            dc.actual_fmt = VK_FORMAT_D16_UNORM;
        }
        VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = dc.actual_fmt;
        ici.extent = {dc.w, dc.h, 1};
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling = VK_IMAGE_TILING_OPTIMAL;
        ici.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        dc.alloc = mem_alloc_image(m_vk, ici, &dc.image);
        if (!dc.alloc) { NVR_ERROR("autodepth falhou"); return false; }
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = dc.image;
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = dc.actual_fmt;
        vi.subresourceRange = {(VkImageAspectFlags)(dc.has_stencil
                                                        ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                                                        : VK_IMAGE_ASPECT_DEPTH_BIT),
                               0, 1, 0, 1};
        vkCreateImageView(m_vk->device(), &vi, nullptr, &dc.view_ds);
        m_autods = new NrSurface();
        m_autods->owner = (void *)this;
        m_autods->core = std::move(dc);
        m_autods->destroy_cb = [s = m_autods, this]() { destroy_surface(const_cast<NrSurface *>(s)); };
        m_st.ds = m_autods;
        m_ds_format_d3d = dc.fmt;
    }
    m_st.rt = m_backbuffer;
    if (m_st.rt) m_st.rt->AddRef();
    if (m_st.ds) m_st.ds->AddRef();
    return true;
}

void NrDevice::destroy_all_pools() {
    if (m_backbuffer) { m_backbuffer->Release(); m_backbuffer = nullptr; }
    if (m_autods) { m_autods->Release(); m_autods = nullptr; }
}

// textura dummy 1x1 branca (bindings obrigatorios sem textura)
void NrDevice::create_dummy_texture() {
    if (m_dummy_view) return;
    VkImageCreateInfo ici{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {1, 1, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    m_dummy_alloc = mem_alloc_image(m_vk, ici, &m_dummy_image);
    if (!m_dummy_alloc) { NVR_ERROR("dummy texture falhou"); return; }
    VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vi.image = m_dummy_image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = ici.format;
    vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCreateImageView(m_vk->device(), &vi, nullptr, &m_dummy_view);
    // branco via staging (o frame e aberto sob demanda por cmd()/ring())
    VkCommandBuffer cb = cmd();
    StagingRing::Alloc a;
    if (ring().alloc(4, 16, &a)) {
        memset(a.mapped, 0xFF, 4);
        VkImageLayout l = VK_IMAGE_LAYOUT_UNDEFINED;
        upload_image_data(m_vk, cb, ring(), m_dummy_image, ici.format, 1, 1, 1, 0, 0,
                          a.mapped, 4, 4, &l);
        image_barrier(cb, m_dummy_image, VK_IMAGE_ASPECT_COLOR_BIT,
                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                      0, 1, 1, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        m_dummy_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
}

// ------------------------------------------------------------------ frames
VkCommandBuffer NrDevice::cmd() {
    ensure_frame_started();
    return m_frames[m_started_slot].cb();
}

StagingRing &NrDevice::ring() {
    ensure_frame_started();
    return m_frames[m_started_slot].ring();
}

void NrDevice::ensure_frame_started() {
    if (m_cb_open) return;
    m_frame_slot = (m_frame_slot + 1) % m_nframes;
    uint32_t slot = m_frame_slot;
    FrameCtx &f = m_frames[slot];
    f.wait_fence();
    f.reset_pool();
    f.begin();
    m_started_slot = slot;
    m_cb_open = true;
    m_frame_no++;
    // warm de pipelines do frame anterior (worker assincrono)
    // (as chaves sao enfileiradas pelo draw; o worker ja esta aquecendo)
}

void NrDevice::partial_flush_and_wait() {
    if (!m_cb_open) return;
    FrameCtx &f = m_frames[m_started_slot];
    f.end();
    m_cb_open = false;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    VkCommandBuffer cb = f.cb();
    si.pCommandBuffers = &cb;
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    vkCreateFence(m_vk->device(), &fci, nullptr, &fence);
    vkQueueSubmit(m_vk->graphics_queue().queue, 1, &si, fence);
    vkWaitForFences(m_vk->device(), 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(m_vk->device(), fence, nullptr);
    // recomeca o CB do mesmo slot
    f.reset_pool();
    f.begin();
    m_cb_open = true;
}

// ------------------------------------------------------------- transicoes
void NrDevice::transition_image(SurfCore &s, VkImageLayout to, VkAccessFlags dst_access,
                                VkPipelineStageFlags dst_stage) {
    VkImageLayout *cur = s.layout_ptr();
    if (*cur == to) return;
    if (!s.img()) return;
    VkImageAspectFlags aspect = s.is_depth
        ? (VkImageAspectFlags)(s.has_stencil ? (VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT)
                                             : VK_IMAGE_ASPECT_DEPTH_BIT)
        : VK_IMAGE_ASPECT_COLOR_BIT;
    uint32_t levels = s.tex ? s.tex->levels : 1;
    uint32_t layers = s.tex ? (s.tex->type == D3DRTYPE_CUBETEXTURE ? 6u : 1u) : 1;
    image_barrier(cmd(), s.img(), aspect, *cur, to, 0, levels, layers,
                  VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, dst_access);
    *cur = to;
    (void)dst_stage;
}

void NrDevice::transition_image(TexCore &t, VkImageLayout to, VkAccessFlags dst_access,
                                VkPipelineStageFlags dst_stage) {
    if (t.layout == to || !t.image) return;
    uint32_t layers = t.type == D3DRTYPE_CUBETEXTURE ? 6u : 1u;
    image_barrier(cmd(), t.image, VK_IMAGE_ASPECT_COLOR_BIT, t.layout, to, 0, t.levels, layers,
                  VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, dst_access);
    t.layout = to;
    (void)dst_stage;
}

// ------------------------------------------------------------- pass mgmt
void NrDevice::ensure_pass_open(bool, bool) {
    if (m_pass_open) return;
    if (!m_st.rt) return;
    ensure_frame_started();
    SurfaceKey sk{m_st.rt};
    PassKey pk{};
    pk.color_fmt = m_st.rt->core.format();
    pk.ds_fmt = m_st.ds ? m_st.ds->core.format() : 0;
    pk.sample_count = 1;
    // load ops: CLEAR se houver clear pendente cobrindo o alvo; DONT_CARE para
    // o backbuffer no primeiro uso do frame (swap effect DISCARD); LOAD caso geral
    pk.color_load = m_rt_needs_clear ? VK_ATTACHMENT_LOAD_OP_CLEAR
                  : (m_st.rt == m_backbuffer ? VK_ATTACHMENT_LOAD_OP_DONT_CARE
                                             : VK_ATTACHMENT_LOAD_OP_LOAD);
    pk.color_store = VK_ATTACHMENT_STORE_OP_STORE;
    pk.ds_load = (m_st.ds && m_ds_needs_clear) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
    pk.ds_store = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderPass rp = m_passes.get(pk);
    if (!rp) { NVR_ERROR("render pass null"); return; }

    VkImageView rtv = m_st.rt->core.tex ? m_st.rt->core.tex->view_rt : m_st.rt->core.view_rt;
    VkImageView dsv = m_st.ds ? m_st.ds->core.view_ds : VK_NULL_HANDLE;
    if (!rtv) { NVR_ERROR("rt sem view"); return; }
    VkFramebuffer fb = m_framebuffers.get_by_view(rtv, dsv, rp, m_st.rt->core.width(),
                                                  m_st.rt->core.height());
    if (!fb) { NVR_ERROR("framebuffer null"); return; }

    VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    bi.renderPass = rp;
    bi.framebuffer = fb;
    D3DVIEWPORT9 &vp = m_st.vp;
    bi.renderArea = {{(int32_t)vp.X, (int32_t)vp.Y},
                     {std::max(1u, m_st.rt->core.width()), std::max(1u, m_st.rt->core.height())}};
    VkClearValue cvs[2] = {};
    cvs[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    cvs[1].depthStencil = {m_clear_z, (uint32_t)m_clear_stencil};
    bi.clearValueCount = pk.ds_fmt ? 2u : 1u;
    bi.pClearValues = cvs;
    vkCmdBeginRenderPass(cmd(), &bi, VK_SUBPASS_CONTENTS_INLINE);
    m_pass_open = true;
    m_rt_needs_clear = false;
    m_ds_needs_clear = false;
    // viewport/scissor dinamicos
    set_dynamic_viewport_scissor();
}

void NrDevice::end_pass() {
    if (!m_pass_open) return;
    vkCmdEndRenderPass(cmd());
    m_pass_open = false;
    // layout das targets: ficam em COLOR/DEPTH_ATTACHMENT_OPTIMAL; transicoes
    // ocorrem sob demanda (sample/transfer) via transition_image
}

void NrDevice::set_render_targets_internal(NrSurface *rt, NrSurface *ds) {
    end_pass();
    m_st.rt = rt;
    m_st.ds = ds;
    // alvos novos: sem clear pendente (LOAD) — a semantica D3D9 preserva
    m_rt_needs_clear = false;
    m_ds_needs_clear = false;
}

// ------------------------------------------------------------------ present
HRESULT NrDevice::Present(const RECT *src, const RECT *dst, HWND, const RGNDATA *) {
    (void)src; (void)dst;
    if (m_pass_open) end_pass();
    ensure_frame_started();

    uint32_t image_index = 0;
    FrameCtx &f0 = m_frames[m_started_slot];
    VkResult ar = m_swap.acquire(f0.acquire_sem(), &image_index);
    if (ar == VK_ERROR_SURFACE_LOST_KHR || ar == VK_ERROR_DEVICE_LOST) {
        NVR_ERROR("acquire falhou (%d)", (int)ar);
        return D3DERR_DEVICELOST;
    }
    if (image_index >= 8) {
        NVR_ERROR("swapchain com %u imagens (limite do cache local)", image_index + 1);
        return D3DERR_DEVICELOST;
    }

    // blit backbuffer -> swapchain (escala + gamma + rotacao)
    record_present_blit(image_index);

    FrameCtx &f = m_frames[m_started_slot];
    f.end();
    m_cb_open = false;

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = 1;
    VkSemaphore acquire_sem = f.acquire_sem();
    si.pWaitSemaphores = &acquire_sem;
    si.pWaitDstStageMask = &wait_stage;
    si.commandBufferCount = 1;
    VkCommandBuffer cb = f.cb();
    si.pCommandBuffers = &cb;
    si.signalSemaphoreCount = 1;
    VkSemaphore done = f.render_done();
    si.pSignalSemaphores = &done;
    VkFence fence = f.fence_handle();
    if (vkQueueSubmit(m_vk->graphics_queue().queue, 1, &si, fence) != VK_SUCCESS) {
        NVR_ERROR("vkQueueSubmit falhou");
        return D3DERR_DEVICELOST;
    }
    m_vk->present_queue((SDL_Window *)m_hwnd);
    m_swap.present(m_vk->present_queue((SDL_Window *)m_hwnd), done, image_index);

    // estatisticas
    if ((m_frame_no & 63) == 0) {
        NVR_INFO("frame %llu: draws=%llu pipelines=%llu hits=%llu", (unsigned long long)m_frame_no,
                 (unsigned long long)m_stat_draws, (unsigned long long)m_pipelines.stat_creates(),
                 (unsigned long long)m_pipelines.stat_hits());
        m_stat_draws = 0;
        if ((m_frame_no & 1023) == 0) m_pipelines.save_disk();
    }
    return D3D_OK;
}

HRESULT NrDevice::Reset(D3DPRESENT_PARAMETERS *pp) {
    NVR_INFO("Reset %ux%u fmt=%d ds=%d/%d", pp->BackBufferWidth, pp->BackBufferHeight,
             pp->BackBufferFormat, pp->EnableAutoDepthStencil, pp->AutoDepthStencilFormat);
    end_pass();
    if (m_cb_open) { m_frames[m_started_slot].end(); m_cb_open = false; }
    // espera tudo
    for (uint32_t i = 0; i < m_nframes; i++) m_frames[i].wait_fence();

    bool ds_changed = m_pp.EnableAutoDepthStencil != pp->EnableAutoDepthStencil ||
                      (pp->EnableAutoDepthStencil && m_ds_format_d3d != pp->AutoDepthStencilFormat);
    (void)ds_changed;
    m_pp = *pp;
    // D3D9 Reset: o estado completo volta ao default, incluindo shaders, texturas,
    // streams e decl (o device segura refs: solta antes de limpar)
    if (m_st.rt) m_st.rt->Release();
    if (m_st.ds) m_st.ds->Release();
    if (m_st.vs) m_st.vs->Release();
    if (m_st.ps) m_st.ps->Release();
    if (m_st.decl) m_st.decl->Release();
    for (uint32_t i = 0; i < 16; i++)
        if (m_st.tex[i]) m_st.tex[i]->Release();
    fill_default_state(m_st);

    // swapchain: so recria se o tamanho do drawable mudou (o drawable segue a
    // janela; o backbuffer do jogo e independente)
    m_swap.recreate();
    if (!create_backbuffer_and_ds()) return D3DERR_DEVICELOST;
    m_st.vp = {0, 0, m_pp.BackBufferWidth, m_pp.BackBufferHeight, 0.0f, 1.0f};
    m_st.scissor = {0, 0, (LONG)m_pp.BackBufferWidth, (LONG)m_pp.BackBufferHeight};
    return D3D_OK;
}

// ------------------------------------------------------------------ gamma
void NrDevice::SetGammaRamp(UINT, DWORD, const D3DGAMMARAMP *ramp) {
    if (!ramp) return;
    m_ramp = *ramp;
    m_ramp_identity = true;
    for (uint32_t i = 0; i < 256; i++) {
        if (m_ramp.red[i] != (WORD)(i * 257) || m_ramp.green[i] != (WORD)(i * 257) ||
            m_ramp.blue[i] != (WORD)(i * 257)) {
            m_ramp_identity = false;
            break;
        }
    }
}
void NrDevice::GetGammaRamp(UINT, D3DGAMMARAMP *ramp) { if (ramp) *ramp = m_ramp; }

// ------------------------------------------------------------------ clear
HRESULT NrDevice::Clear(DWORD count, const D3DRECT *rects, DWORD flags, D3DCOLOR color, float z,
                        DWORD stencil) {
    if (!flags) return D3D_OK;
    bool full = count == 0;
    bool full_color = full || !rects;
    if (!full) {
        // retangulos: so via ClearAttachments (pass aberto)
        ensure_pass_open(true, true);
        VkClearAttachment ca{};
        VkClearRect cr[16]{};
        uint32_t n = std::min(count, 16u);
        for (uint32_t i = 0; i < n; i++) {
            cr[i].rect = {{rects[i].x1, rects[i].y1},
                          {(uint32_t)(rects[i].x2 - rects[i].x1), (uint32_t)(rects[i].y2 - rects[i].y1)}};
            cr[i].baseArrayLayer = 0;
            cr[i].layerCount = 1;
        }
        if (flags & D3DCLEAR_TARGET) {
            ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ca.colorAttachment = 0;
            float c[4] = {(float)((color >> 16) & 0xFF) / 255.0f, (float)((color >> 8) & 0xFF) / 255.0f,
                          (float)(color & 0xFF) / 255.0f, (float)((color >> 24) & 0xFF) / 255.0f};
            memcpy(ca.clearValue.color.float32, c, 16);
            vkCmdClearAttachments(cmd(), 1, &ca, n, cr);
        }
        if (flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL) && m_st.ds) {
            VkClearAttachment da{};
            da.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if ((flags & D3DCLEAR_STENCIL) && m_st.ds->core.has_stencil)
                da.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            da.clearValue.depthStencil = {z, stencil};
            vkCmdClearAttachments(cmd(), 1, &da, n, cr);
        }
        return D3D_OK;
    }
    // full clear: fica pendente como load op do proximo pass (tiler-friendly)
    if (flags & D3DCLEAR_TARGET) {
        m_rt_needs_clear = true;
        m_clear_color[0] = (float)((color >> 16) & 0xFF) / 255.0f;
        m_clear_color[1] = (float)((color >> 8) & 0xFF) / 255.0f;
        m_clear_color[2] = (float)(color & 0xFF) / 255.0f;
        m_clear_color[3] = (float)((color >> 24) & 0xFF) / 255.0f;
        // se o pass ja esta aberto com este rt: clear attachments agora
        if (m_pass_open) {
            m_rt_needs_clear = false;
            VkClearAttachment ca{};
            ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ca.colorAttachment = 0;
            memcpy(ca.clearValue.color.float32, m_clear_color, 16);
            VkClearRect cr{};
            cr.rect = {{0, 0}, {m_st.rt->core.width(), m_st.rt->core.height()}};
            cr.baseArrayLayer = 0;
            cr.layerCount = 1;
            vkCmdClearAttachments(cmd(), 1, &ca, 1, &cr);
        }
    }
    if (flags & (D3DCLEAR_ZBUFFER | D3DCLEAR_STENCIL) && m_st.ds) {
        m_ds_needs_clear = true;
        m_clear_z = z;
        m_clear_stencil = stencil;
        if (m_pass_open) {
            m_ds_needs_clear = false;
            VkClearAttachment da{};
            da.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            if ((flags & D3DCLEAR_STENCIL) && m_st.ds->core.has_stencil)
                da.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
            da.clearValue.depthStencil = {z, stencil};
            VkClearRect cr{};
            cr.rect = {{0, 0}, {m_st.ds->core.width(), m_st.ds->core.height()}};
            cr.baseArrayLayer = 0;
            cr.layerCount = 1;
            vkCmdClearAttachments(cmd(), 1, &da, 1, &cr);
        }
    }
    return D3D_OK;
}

// ------------------------------------------------------- setters de estado
HRESULT NrDevice::SetTransform(D3DTRANSFORMSTATETYPE t, const D3DMATRIX *m) {
    if ((DWORD)t >= 256 || !m) return D3DERR_INVALIDCALL;
    m_st.xfm[(DWORD)t] = *m;
    if (m_recording_stateblock) {
        uint32_t op = 4;
        sb_put(&op, 4);
        DWORD tt = (DWORD)t;
        sb_put(&tt, 4);
        sb_put(m, sizeof(D3DMATRIX));
    }
    return D3D_OK;
}
HRESULT NrDevice::GetTransform(D3DTRANSFORMSTATETYPE t, D3DMATRIX *m) {
    if ((DWORD)t >= 256 || !m) return D3DERR_INVALIDCALL;
    *m = m_st.xfm[(DWORD)t];
    return D3D_OK;
}
HRESULT NrDevice::MultiplyTransform(D3DTRANSFORMSTATETYPE t, const D3DMATRIX *m) {
    if ((DWORD)t >= 256 || !m) return D3DERR_INVALIDCALL;
    D3DMATRIX &d = m_st.xfm[(DWORD)t];
    D3DMATRIX r{};
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++) {
            float s = 0;
            for (int k = 0; k < 4; k++) s += d.m[i][k] * m->m[k][j];
            r.m[i][j] = s;
        }
    d = r;
    return D3D_OK;
}
HRESULT NrDevice::SetViewport(const D3DVIEWPORT9 *vp) {
    if (!vp || vp->Width == 0 || vp->Height == 0) return D3DERR_INVALIDCALL;
    m_st.vp = *vp;
    if (m_pass_open) set_dynamic_viewport_scissor();
    if (m_recording_stateblock) { uint32_t op = 23; sb_put(&op, 4); sb_put(vp, sizeof(D3DVIEWPORT9)); }
    return D3D_OK;
}
HRESULT NrDevice::GetViewport(D3DVIEWPORT9 *vp) { if (!vp) return D3DERR_INVALIDCALL; *vp = m_st.vp; return D3D_OK; }
HRESULT NrDevice::SetMaterial(const D3DMATERIAL9 *m) {
    if (!m) return D3DERR_INVALIDCALL;
    m_st.mat = *m;
    if (m_recording_stateblock) { uint32_t op = 5; sb_put(&op, 4); sb_put(m, sizeof(D3DMATERIAL9)); }
    return D3D_OK;
}
HRESULT NrDevice::GetMaterial(D3DMATERIAL9 *m) { if (!m) return D3DERR_INVALIDCALL; *m = m_st.mat; return D3D_OK; }
HRESULT NrDevice::SetLight(DWORD idx, const D3DLIGHT9 *l) {
    if (idx >= 8 || !l) return D3DERR_INVALIDCALL;
    m_st.lights[idx].l = *l;
    if (m_recording_stateblock) {
        uint32_t op = 6;
        sb_put(&op, 4);
        sb_put(&idx, 4);
        sb_put(l, sizeof(D3DLIGHT9));
    }
    return D3D_OK;
}
HRESULT NrDevice::GetLight(DWORD idx, D3DLIGHT9 *l) {
    if (idx >= 8 || !l) return D3DERR_INVALIDCALL;
    *l = m_st.lights[idx].l;
    return D3D_OK;
}
HRESULT NrDevice::LightEnable(DWORD idx, BOOL enable) {
    if (idx >= 8) return D3DERR_INVALIDCALL;
    m_st.lights[idx].enabled = enable != 0;
    if (m_recording_stateblock) { uint32_t op = 7; sb_put(&op, 4); sb_put(&idx, 4); sb_put(&enable, 4); }
    return D3D_OK;
}
HRESULT NrDevice::GetLightEnable(DWORD idx, BOOL *enable) {
    if (idx >= 8 || !enable) return D3DERR_INVALIDCALL;
    *enable = m_st.lights[idx].enabled ? TRUE : FALSE;
    return D3D_OK;
}
HRESULT NrDevice::SetClipPlane(DWORD idx, const float *plane) {
    if (idx >= 6 || !plane) return D3DERR_INVALIDCALL;
    memcpy(m_st.clip_planes[idx], plane, 16);
    if (m_recording_stateblock) {
        uint32_t op = 8;
        sb_put(&op, 4);
        sb_put(&idx, 4);
        sb_put(plane, 16);
    }
    return D3D_OK;
}
HRESULT NrDevice::GetClipPlane(DWORD idx, float *plane) {
    if (idx >= 6 || !plane) return D3DERR_INVALIDCALL;
    memcpy(plane, m_st.clip_planes[idx], 16);
    return D3D_OK;
}
HRESULT NrDevice::SetRenderState(D3DRENDERSTATETYPE rs, DWORD value) {
    if (rs > 210) { NVR_TRACE("SetRenderState fora da faixa %u", rs); return D3D_OK; }
    m_st.rs[rs] = value;
    if (m_recording_stateblock) {
        uint32_t op = 1;
        sb_put(&op, 4);
        DWORD r = (DWORD)rs;
        sb_put(&r, 4);
        sb_put(&value, 4);
    }
    return D3D_OK;
}
HRESULT NrDevice::GetRenderState(D3DRENDERSTATETYPE rs, DWORD *value) {
    if (!value || rs > 210) return D3DERR_INVALIDCALL;
    *value = m_st.rs[rs];
    return D3D_OK;
}
HRESULT NrDevice::SetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE tss, DWORD v) {
    if (stage >= 8 || tss > 32) return D3DERR_INVALIDCALL;
    m_st.tss[stage][tss] = v;
    if (m_recording_stateblock) {
        uint32_t op = 2;
        sb_put(&op, 4);
        DWORD s2 = stage, t2 = (DWORD)tss;
        sb_put(&s2, 4);
        sb_put(&t2, 4);
        sb_put(&v, 4);
    }
    return D3D_OK;
}
HRESULT NrDevice::GetTextureStageState(DWORD stage, D3DTEXTURESTAGESTATETYPE tss, DWORD *v) {
    if (stage >= 8 || tss > 32 || !v) return D3DERR_INVALIDCALL;
    *v = m_st.tss[stage][tss];
    return D3D_OK;
}
HRESULT NrDevice::SetSamplerState(DWORD stage, D3DSAMPLERSTATETYPE ss, DWORD v) {
    if (stage >= 16 || ss > 13) return D3DERR_INVALIDCALL;
    m_st.samp[stage][ss] = v;
    if (m_recording_stateblock) {
        uint32_t op = 3;
        sb_put(&op, 4);
        DWORD s2 = stage, t2 = (DWORD)ss;
        sb_put(&s2, 4);
        sb_put(&t2, 4);
        sb_put(&v, 4);
    }
    return D3D_OK;
}
HRESULT NrDevice::GetSamplerState(DWORD stage, D3DSAMPLERSTATETYPE ss, DWORD *v) {
    if (stage >= 16 || ss > 13 || !v) return D3DERR_INVALIDCALL;
    *v = m_st.samp[stage][ss];
    return D3D_OK;
}
HRESULT NrDevice::SetTexture(DWORD stage, IDirect3DBaseTexture9 *tex) {
    if (stage >= 16) return D3DERR_INVALIDCALL;
    if (m_st.tex[stage]) m_st.tex[stage]->Release();
    m_st.tex[stage] = tex;
    if (tex) tex->AddRef();
    if (m_recording_stateblock) {
        uint32_t op = 9;
        sb_put(&op, 4);
        sb_put(&stage, 4);
        void *p = (void *)tex;
        sb_put(&p, 8);
        if (tex) tex->AddRef();   // ref do record (soltada no destrutor do bloco)
    }
    return D3D_OK;
}
HRESULT NrDevice::GetTexture(DWORD stage, IDirect3DBaseTexture9 **tex) {
    if (stage >= 16 || !tex) return D3DERR_INVALIDCALL;
    *tex = m_st.tex[stage];
    if (*tex) (*tex)->AddRef();
    return D3D_OK;
}
HRESULT NrDevice::SetRenderTarget(DWORD idx, IDirect3DSurface9 *rt) {
    if (idx != 0) return D3DERR_INVALIDCALL;   // caps.MaxSimultaneousRenderTargets = 1
    NrSurface *surf = static_cast<NrSurface *>(rt);
    if (surf && !(surf->core.usage & D3DUSAGE_RENDERTARGET) && surf != m_backbuffer &&
        surf->core.pool != D3DPOOL_DEFAULT) {
        return D3DERR_INVALIDCALL;
    }
    // DeviceState.rt/ds sao refs do device (AddRef/Release pareados)
    NrSurface *nova = surf ? surf : m_backbuffer;
    NrSurface *velha = m_st.rt;
    if (nova) nova->AddRef();
    set_render_targets_internal(nova, m_st.ds);
    if (velha) velha->Release();
    return D3D_OK;
}
HRESULT NrDevice::GetRenderTarget(DWORD idx, IDirect3DSurface9 **out) {
    if (idx != 0 || !out) return D3DERR_INVALIDCALL;
    *out = m_st.rt;
    if (*out) (*out)->AddRef();
    return D3D_OK;
}
HRESULT NrDevice::SetDepthStencilSurface(IDirect3DSurface9 *ds) {
    NrSurface *nova = static_cast<NrSurface *>(ds);
    NrSurface *velha = m_st.ds;
    if (nova) nova->AddRef();
    set_render_targets_internal(m_st.rt, nova);
    if (velha) velha->Release();
    return D3D_OK;
}
HRESULT NrDevice::GetDepthStencilSurface(IDirect3DSurface9 **out) {
    if (!out) return D3DERR_NOTFOUND;
    if (!m_st.ds) return D3DERR_NOTFOUND;
    *out = m_st.ds;
    (*out)->AddRef();
    return D3D_OK;
}
HRESULT NrDevice::SetScissorRect(const RECT *rect) {
    if (!rect) return D3DERR_INVALIDCALL;
    m_st.scissor = *rect;
    if (m_pass_open) set_dynamic_viewport_scissor();
    if (m_recording_stateblock) { uint32_t op = 24; sb_put(&op, 4); sb_put(rect, sizeof(RECT)); }
    return D3D_OK;
}
HRESULT NrDevice::GetScissorRect(RECT *rect) {
    if (!rect) return D3DERR_INVALIDCALL;
    *rect = m_st.scissor;
    return D3D_OK;
}
HRESULT NrDevice::SetSoftwareVertexProcessing(BOOL sw) {
    m_st.swvp = sw != 0;
    if (m_recording_stateblock) { uint32_t op = 26; sb_put(&op, 4); sb_put(&sw, 4); }
    return D3D_OK;
}
BOOL NrDevice::GetSoftwareVertexProcessing() { return m_st.swvp ? TRUE : FALSE; }
HRESULT NrDevice::SetNPatchMode(float) { return D3D_OK; }
float NrDevice::GetNPatchMode() { return 0.0f; }   // N-patches nao sao usados (SM3)
HRESULT NrDevice::SetClipStatus(const D3DCLIPSTATUS9 *) { return D3D_OK; }  // informativo (clipping e feito no GPU fixo)
HRESULT NrDevice::GetClipStatus(D3DCLIPSTATUS9 *cs) { if (cs) memset(cs, 0, sizeof *cs); return D3D_OK; }
HRESULT NrDevice::SetDialogBoxMode(BOOL) { return D3D_OK; }
HRESULT NrDevice::GetRasterStatus(UINT, D3DRASTER_STATUS *out) {
    if (!out) return D3DERR_INVALIDCALL;
    out->InVBlank = FALSE;
    out->ScanLine = 0;
    return D3D_OK;
}
HRESULT NrDevice::GetDisplayMode(UINT sw, D3DDISPLAYMODE *out) {
    if (!out) return D3DERR_INVALIDCALL;
    out->Width = m_pp.BackBufferWidth;
    out->Height = m_pp.BackBufferHeight;
    out->RefreshRate = 60;
    out->Format = m_pp.BackBufferFormat == D3DFMT_UNKNOWN ? D3DFMT_X8R8G8B8 : m_pp.BackBufferFormat;
    (void)sw;
    return D3D_OK;
}
HRESULT NrDevice::GetCreationParameters(D3DDEVICE_CREATION_PARAMETERS *out) {
    if (!out) return D3DERR_INVALIDCALL;
    out->AdapterOrdinal = 0;
    out->DeviceType = D3DDEVTYPE_HAL;
    out->hFocusWindow = m_hwnd;
    out->BehaviorFlags = m_flags;
    return D3D_OK;
}
UINT NrDevice::GetAvailableTextureMem() { return 512 * 1024 * 1024; }   // valor plausivel (D3D9 nao garante precisao)
HRESULT NrDevice::EvictManagedResources() {
    // libera copias device de texturas managed (re-upload on demand)
    return D3D_OK;
}
HRESULT NrDevice::TestCooperativeLevel() { return D3D_OK; }
HRESULT NrDevice::ValidateDevice(DWORD *passes) { if (passes) *passes = 0; return D3D_OK; }
HRESULT NrDevice::BeginScene() {
    if (m_st.scene_open) return D3DERR_INVALIDCALL;
    m_st.scene_open = true;
    ensure_frame_started();
    return D3D_OK;
}
HRESULT NrDevice::EndScene() {
    m_st.scene_open = false;
    return D3D_OK;
}
HRESULT NrDevice::SetPaletteEntries(UINT, const PALETTEENTRY *) { return D3D_OK; }
HRESULT NrDevice::GetPaletteEntries(UINT, PALETTEENTRY *e) { if (e) memset(e, 0, sizeof(PALETTEENTRY) * 256); return D3D_OK; }
HRESULT NrDevice::SetCurrentTexturePalette(UINT) { return D3D_OK; }
HRESULT NrDevice::GetCurrentTexturePalette(UINT *pal) { if (pal) *pal = 0; return D3D_OK; }
HRESULT NrDevice::DrawRectPatch(UINT, const float *, const D3DRECTPATCH_INFO *) { return D3DERR_INVALIDCALL; }
HRESULT NrDevice::DrawTriPatch(UINT, const float *, const D3DTRIPATCH_INFO *) { return D3DERR_INVALIDCALL; }
HRESULT NrDevice::DeletePatch(UINT) { return D3D_OK; }

HRESULT NrDevice::QueryInterface(REFIID riid, void **out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (riid == __uuidof(IDirect3DDevice9) || riid == __uuidof(IUnknown)) {
        *out = (IDirect3DDevice9 *)this;
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG NrDevice::Release() {
    ULONG r = --refs;
    if (!r) {
        shutdown();
        delete this;
    }
    return r;
}

HRESULT NrDevice::GetDirect3D(IDirect3D9 **out) {
    if (!out) return D3DERR_INVALIDCALL;
    *out = m_d3d;
    if (*out) (*out)->AddRef();
    return D3D_OK;
}

HRESULT NrDevice::GetDeviceCaps(D3DCAPS9 *out) {
    if (!out) return D3DERR_INVALIDCALL;
    fill_device_caps(out, *m_vk);
    return D3D_OK;
}

HRESULT NrDevice::GetBackBuffer(UINT swap, UINT i, D3DBACKBUFFER_TYPE, IDirect3DSurface9 **out) {
    // uma swapchain implicita com um unico backbuffer (modelo da bridge)
    if (swap != 0 || i != 0 || !out) return D3DERR_INVALIDCALL;
    if (!m_backbuffer) return D3DERR_INVALIDCALL;
    *out = m_backbuffer;
    (*out)->AddRef();
    return D3D_OK;
}

HRESULT NrDevice::SetCursorProperties(UINT, UINT, IDirect3DSurface9 *) { return D3D_OK; }
void NrDevice::SetCursorPosition(int, int, DWORD) {}
BOOL NrDevice::ShowCursor(BOOL show) {
    // sem cursor de hardware (o HUD e Java); mantem o contador D3D9
    if (show) m_cursor_counter++;
    else m_cursor_counter--;
    return m_cursor_counter > 0 ? TRUE : FALSE;
}
HRESULT NrDevice::CreateAdditionalSwapChain(D3DPRESENT_PARAMETERS *, IDirect3DSwapChain9 **) {
    return D3DERR_INVALIDCALL;   // o jogo usa a swapchain implicita (log se ocorrer)
}
HRESULT NrDevice::GetSwapChain(UINT i, IDirect3DSwapChain9 **out) {
    (void)i; (void)out;
    return D3DERR_NOTFOUND;
}
UINT NrDevice::GetNumberOfSwapChains() { return 0; }

} // namespace nvr
