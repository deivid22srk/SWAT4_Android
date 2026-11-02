// draw_common + build_pipeline + shaders/pipelines de blit/clear/present.
#include "nvr_device.h"
#include "nvr_com_impl.h"
#include <algorithm>
#include <cmath>
#include <SDL2/SDL.h>

namespace nvr {

UINT primitive_vertex_count(D3DPRIMITIVETYPE prim, UINT count);
VkPrimitiveTopology primitive_topology(D3DPRIMITIVETYPE prim);
void mat3_inverse_transpose(const float m[16], float out[9]);
void pack_fff_vs_ubo(float *dst, uint32_t vec4_count, const FffVsCfg &cfg, bool lights_view_space);
void pack_fff_ps_ubo(float *dst, uint32_t vec4_count, const FffPsCfg &cfg, const DeviceState &st);
void fvf_to_elements(DWORD fvf, std::vector<D3DVERTEXELEMENT9> &out);
uint32_t d3d9_decltype_size(uint8_t type);

uint64_t PipelineKey::hash() const {
    uint32_t h = fnv1a(this, sizeof *this);
    return ((uint64_t)h << 32) | fnv1a(this, sizeof *this, 0x9E3779B9u);
}
bool PipelineKey::operator==(const PipelineKey &o) const {
    return memcmp(this, &o, sizeof *this) == 0;
}

// -------------------------------------------------------- viewport/scissor
void NrDevice::set_dynamic_viewport_scissor() {
    D3DVIEWPORT9 &vp = m_st.vp;
    // D3D9 y-down: viewport com altura NEGATIVA (maintenance1/1.1)
    VkViewport v{(float)vp.X, (float)(vp.Y + (int32_t)vp.Height), (float)vp.Width,
                 -(float)vp.Height, vp.MinZ, vp.MaxZ};
    VkRect2D sc{{(int32_t)vp.X, (int32_t)vp.Y}, {vp.Width, vp.Height}};
    if (m_st.rs[D3DRS_SCISSORTESTENABLE]) {
        sc.offset = {m_st.scissor.left, m_st.scissor.top};
        sc.extent = {(uint32_t)(m_st.scissor.right - m_st.scissor.left),
                     (uint32_t)(m_st.scissor.bottom - m_st.scissor.top)};
    }
    vkCmdSetViewport(cmd(), 0, 1, &v);
    vkCmdSetScissor(cmd(), 0, 1, &sc);
}

// ---------------------------------------------------------- pipeline build
VkPipeline NrDevice::build_pipeline(const PipelineKey &key, const XlateOut *vsx, const XlateOut *psx) {
    std::vector<D3DVERTEXELEMENT9> elems;
    decl_to_elements(m_st, elems);
    uint8_t comps[32];
    input_comps_from_elements(elems, comps);

    // stages
    VkPipelineShaderStageCreateInfo stages[2]{};
    uint32_t nstages = 0;
    VkShaderModule vsm = module_for(vsx->spirv, key.vs ? key.vs : key.fff_vs_hash);
    VkShaderModule psm = module_for(psx->spirv, key.ps ? key.ps : key.fff_ps_hash);
    if (!vsm || !psm) return VK_NULL_HANDLE;
    stages[nstages++] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                         VK_SHADER_STAGE_VERTEX_BIT, vsm, "main", nullptr};
    stages[nstages++] = {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                         VK_SHADER_STAGE_FRAGMENT_BIT, psm, "main", nullptr};

    // vertex input
    VkVertexInputBindingDescription binds[16]{};
    VkVertexInputAttributeDescription attrs[32]{};
    uint32_t nbind = 0, nattr = 0;
    std::vector<D3DVERTEXELEMENT9> eff = elems;
    for (auto &e : eff) {
        uint32_t loc = usage_to_location(e.Usage, e.UsageIndex);
        if (loc >= 32 || !comps[loc]) continue;
        uint32_t stream = e.Stream;
        // binding ja existe?
        int found = -1;
        for (uint32_t i = 0; i < nbind; i++)
            if (binds[i].binding == stream) found = (int)i;
        if (found < 0) {
            NrStream &s = m_up_vb.buffer ? m_st.streams[0] : m_st.streams[stream];
            binds[nbind] = {stream, m_up_vb.buffer ? m_up_stride : s.stride,
                            (s.freq & 0x80000000u) ? VK_VERTEX_INPUT_RATE_INSTANCE
                                                   : VK_VERTEX_INPUT_RATE_VERTEX};
            found = (int)nbind;
            nbind++;
        }
        attrs[nattr++] = {loc, (uint32_t)found, decltype_to_vkformat(e.Type), e.Offset};
        if (nattr >= 32) break;
    }
    // stream stride 0 (UP) ainda precisa de binding valido
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    vi.vertexBindingDescriptionCount = nbind;
    vi.pVertexBindingDescriptions = binds;
    vi.vertexAttributeDescriptionCount = nattr;
    vi.pVertexAttributeDescriptions = attrs;

    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = (VkPrimitiveTopology)key.topology;

    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    uint32_t fill = key.raster & 7;
    rs.polygonMode = fill == D3DFILL_WIREFRAME ? VK_POLYGON_MODE_LINE
                     : fill == D3DFILL_POINT   ? VK_POLYGON_MODE_POINT
                                               : VK_POLYGON_MODE_FILL;
    uint32_t cull = (key.raster >> 3) & 3;
    rs.cullMode = cull == D3DCULL_CW ? VK_CULL_MODE_FRONT_BIT
                  : cull == D3DCULL_CCW ? VK_CULL_MODE_BACK_BIT
                                        : (cull == D3DCULL_NONE ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT);
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;   // D3D9: CW e face frontal
    rs.depthBiasEnable = key.depth_bias != 0 || key.slope_bias != 0;
    rs.depthBiasConstantFactor = key.depth_bias;
    rs.depthBiasSlopeFactor = key.slope_bias;
    rs.lineWidth = 1.0f;

    // depth-stencil
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    uint32_t zenable = key.ds & 1;
    uint32_t zwrite = (key.ds >> 1) & 1;
    uint32_t zfunc = (key.ds >> 2) & 7;
    uint32_t st_enable = (key.ds >> 5) & 1;
    static const VkCompareOp cmps[8] = {VK_COMPARE_OP_NEVER, VK_COMPARE_OP_LESS, VK_COMPARE_OP_EQUAL,
                                        VK_COMPARE_OP_LESS_OR_EQUAL, VK_COMPARE_OP_GREATER,
                                        VK_COMPARE_OP_NOT_EQUAL, VK_COMPARE_OP_GREATER_OR_EQUAL,
                                        VK_COMPARE_OP_ALWAYS};
    static const VkStencilOp sops[8] = {
        VK_STENCIL_OP_KEEP, VK_STENCIL_OP_ZERO, VK_STENCIL_OP_REPLACE,
        VK_STENCIL_OP_INCREMENT_AND_CLAMP, VK_STENCIL_OP_DECREMENT_AND_CLAMP,
        VK_STENCIL_OP_INVERT, VK_STENCIL_OP_INCREMENT_AND_WRAP, VK_STENCIL_OP_DECREMENT_AND_WRAP};
    ds.depthTestEnable = zenable ? VK_TRUE : VK_FALSE;
    ds.depthWriteEnable = zwrite ? VK_TRUE : VK_FALSE;
    ds.depthCompareOp = cmps[zfunc & 7];
    if (!m_st.ds || !m_st.ds->core.is_depth) ds.depthTestEnable = VK_FALSE;
    ds.stencilTestEnable = st_enable ? VK_TRUE : VK_FALSE;
    if (st_enable) {
        ds.front.failOp = sops[(key.ds >> 6) & 0xF];
        ds.front.depthFailOp = sops[(key.ds >> 10) & 0xF];
        ds.front.passOp = sops[(key.ds >> 14) & 0xF];
        ds.front.compareOp = cmps[(key.ds >> 18) & 7];
        ds.front.compareMask = key.stencil_mask;
        ds.front.writeMask = key.stencil_write_mask;
        if (key.two_sided) {
            // D3D9: estados CCW_* aplicam-se ao verso (com frontFace CW do
            // Vulkan, o verso D3D9 = back do Vulkan)
            ds.back.failOp = sops[(key.ds_ccw >> 0) & 0xF];
            ds.back.depthFailOp = sops[(key.ds_ccw >> 4) & 0xF];
            ds.back.passOp = sops[(key.ds_ccw >> 8) & 0xF];
            ds.back.compareOp = cmps[(key.ds_ccw >> 12) & 7];
            ds.back.compareMask = key.stencil_mask;
            ds.back.writeMask = key.stencil_write_mask;
        } else {
            ds.back = ds.front;
        }
    }

    // blend
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    // D3DBLEND 1..13 (indexamos por valor-1); BOTHSRCALPHA/BOTHINVSRCALPHA
    // viram SRCALPHA/INVSRCALPHA combinados com INVSRCALPHA/SRCALPHA (aprox
    // documentada; Vulkan nao tem os factores duplos)
    static const VkBlendFactor bf[13] = {
        VK_BLEND_FACTOR_ZERO, VK_BLEND_FACTOR_ONE, VK_BLEND_FACTOR_SRC_COLOR,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR, VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA, VK_BLEND_FACTOR_DST_COLOR,
        VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR, VK_BLEND_FACTOR_DST_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA, VK_BLEND_FACTOR_SRC_ALPHA_SATURATE,
        VK_BLEND_FACTOR_SRC_ALPHA, VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA
    };
    static const VkBlendOp bo[5] = {VK_BLEND_OP_ADD, VK_BLEND_OP_SUBTRACT, VK_BLEND_OP_REVERSE_SUBTRACT,
                                    VK_BLEND_OP_MIN, VK_BLEND_OP_MAX};
    auto blend_factor = [&](uint32_t v) -> VkBlendFactor {
        return (v >= 1 && v <= 13) ? bf[v - 1] : VK_BLEND_FACTOR_ONE;
    };
    auto blend_op = [&](uint32_t v) -> VkBlendOp {
        return (v >= 1 && v <= 5) ? bo[v - 1] : VK_BLEND_OP_ADD;
    };
    VkPipelineColorBlendAttachmentState att{};
    att.blendEnable = (key.blend & 1) ? VK_TRUE : VK_FALSE;
    uint32_t srcb = (key.blend >> 1) & 0x1F, dstb = (key.blend >> 6) & 0x1F;
    att.srcColorBlendFactor = blend_factor(srcb);
    att.dstColorBlendFactor = blend_factor(dstb);
    att.colorBlendOp = blend_op((key.blend >> 11) & 0xF);
    att.srcAlphaBlendFactor = (key.blend >> 15) & 1 ? blend_factor((key.blend >> 16) & 0x1F)
                                                    : att.srcColorBlendFactor;
    att.dstAlphaBlendFactor = (key.blend >> 15) & 1 ? blend_factor((key.blend >> 21) & 0x1F)
                                                    : att.dstColorBlendFactor;
    att.alphaBlendOp = blend_op((key.blend >> 26) & 0xF);
    if (att.colorBlendOp == VK_BLEND_OP_MIN || att.colorBlendOp == VK_BLEND_OP_MAX) {
        att.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
        att.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
        att.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        att.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
    }
    DWORD cw = key.color_write;
    att.colorWriteMask = ((cw & 1) ? VK_COLOR_COMPONENT_R_BIT : 0) |
                         ((cw & 2) ? VK_COLOR_COMPONENT_G_BIT : 0) |
                         ((cw & 4) ? VK_COLOR_COMPONENT_B_BIT : 0) |
                         ((cw & 8) ? VK_COLOR_COMPONENT_A_BIT : 0);
    cb.attachmentCount = 1;
    cb.pAttachments = &att;

    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkDynamicState dyns[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR,
                             VK_DYNAMIC_STATE_STENCIL_REFERENCE};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 3;
    dyn.pDynamicStates = dyns;

    // render pass compativel (load ops nao importam para compatibilidade)
    PassKey pk{};
    pk.color_fmt = key.color_fmt;
    pk.ds_fmt = key.ds_fmt;
    pk.color_load = VK_ATTACHMENT_LOAD_OP_LOAD;
    pk.color_store = VK_ATTACHMENT_STORE_OP_STORE;
    pk.ds_load = VK_ATTACHMENT_LOAD_OP_LOAD;
    pk.ds_store = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderPass rp = m_passes.get(pk);

    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pi.stageCount = nstages;
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pRasterizationState = &rs;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.pMultisampleState = &ms;
    pi.pDynamicState = &dyn;
    pi.layout = m_pipelines.layout();
    pi.renderPass = rp;
    pi.subpass = 0;
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(m_vk->device(), VK_NULL_HANDLE, 1, &pi, nullptr, &p) != VK_SUCCESS) {
        NVR_ERROR("vkCreateGraphicsPipelines falhou (vs=%llx ps=%llx)", (unsigned long long)key.vs,
                  (unsigned long long)key.ps);
        return VK_NULL_HANDLE;
    }
    return p;
}

// ------------------------------------------------------------- draw common
bool NrDevice::draw_common(D3DPRIMITIVETYPE prim, INT base_vtx, UINT start_vtx, UINT num_vtx,
                           UINT start_idx, UINT count, bool indexed) {
    if (!count) return true;
    ensure_pass_open(m_rt_needs_clear, m_ds_needs_clear);
    if (!m_pass_open) return false;

    std::vector<D3DVERTEXELEMENT9> elems;
    if (!decl_to_elements(m_st, elems)) {
        NVR_WARN("draw sem declaration/FVF");
        return false;
    }
    uint8_t comps[32];
    input_comps_from_elements(elems, comps);

    // shaders
    const XlateOut *vsx = nullptr, *psx = nullptr;
    PipelineKey key{};
    bool flat = m_st.rs[D3DRS_SHADEMODE] == D3DSHADE_FLAT;
    if (m_st.vs) {
        NrVS *s = static_cast<NrVS *>(m_st.vs);
        vsx = m_shaders.get_vs(s->bytecode.data(), s->bytecode.size(), flat);
        if (!vsx) return false;
        key.vs = fnv1a(vsx->spirv.data(), vsx->spirv.size() * 4);
        key.fff_vs_hash = 0;
    } else {
        FffVsCfg cfg{};
        FffPsCfg tmp{};
        build_fff_cfg(cfg, tmp, comps);
        cfg.in_comps = nullptr;              // hash deterministico (sem ponteiro)
        key.fff_vs_hash = fnv1a(&cfg, sizeof cfg);
        key.vs = 0;
        cfg.in_comps = comps;                // geracao usa os comps reais
        vsx = m_shaders.get_fff_vs(key.fff_vs_hash, cfg, comps);
        if (!vsx) return false;
    }
    if (m_st.ps) {
        NrPS *s = static_cast<NrPS *>(m_st.ps);
        bool at = m_st.rs[D3DRS_ALPHATESTENABLE] != 0;
        psx = m_shaders.get_ps(s->bytecode.data(), s->bytecode.size(), at, m_st.rs[D3DRS_ALPHAFUNC]);
        if (!psx) return false;
        key.ps = fnv1a(psx->spirv.data(), psx->spirv.size() * 4);
        key.fff_ps_hash = 0;
    } else {
        FffVsCfg tmp;
        FffPsCfg cfg;
        build_fff_cfg(tmp, cfg, comps);
        key.fff_ps_hash = fnv1a(&cfg, sizeof cfg);
        key.ps = 0;
        psx = m_shaders.get_fff_ps(cfg);
        if (!psx) return false;
    }

    key.decl_hash = decl_signature_hash(elems);
    bool fan = prim == D3DPT_TRIANGLEFAN;
    key.topology = (uint32_t)(fan ? VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST : primitive_topology(prim));
    key.raster = pack_raster_state(m_st);
    key.ds = pack_ds_state(m_st);
    if (m_st.rs[D3DRS_STENCILENABLE] && m_st.rs[D3DRS_TWOSIDEDSTENCILMODE]) {
        uint32_t ccw = 0;
        auto pack4 = [&ccw](DWORD val, int shift) {
            ccw |= (val & 0xF) << shift;
        };
        pack4(m_st.rs[D3DRS_CCW_STENCILFAIL], 0);
        pack4(m_st.rs[D3DRS_CCW_STENCILZFAIL], 4);
        pack4(m_st.rs[D3DRS_CCW_STENCILPASS], 8);
        pack4(m_st.rs[D3DRS_CCW_STENCILFUNC], 12);
        key.ds_ccw = ccw;
        key.two_sided = 1;
    }
    key.stencil_mask = m_st.rs[D3DRS_STENCILMASK];
    key.stencil_write_mask = m_st.rs[D3DRS_STENCILWRITEMASK];
    key.stencil_ref = m_st.rs[D3DRS_STENCILREF];
    key.blend = pack_blend_state(m_st);
    key.color_write = m_st.rs[D3DRS_COLORWRITEENABLE];
    key.depth_bias = *(float *)&m_st.rs[D3DRS_DEPTHBIAS];
    key.slope_bias = *(float *)&m_st.rs[D3DRS_SLOPESCALEDEPTHBIAS];
    key.color_fmt = m_st.rt->core.format();
    key.ds_fmt = m_st.ds ? m_st.ds->core.format() : 0;
    key.samples = 1;

    uint64_t key_hash = key.hash();
    const XlateOut *fvx = vsx, *fpx = psx;
    // O build é sempre na thread do jogo (bloqueante, como um driver real):
    // o worker assincrono foi removido porque build_pipeline acessa estado do
    // device sem locks. O aquecimento vem do VkPipelineCache em disco.
    VkPipeline p = m_pipelines.get_graphics(key_hash, [this, key, fvx, fpx](VkPipelineCache) {
        return build_pipeline(key, fvx, fpx);
    });
    if (!p) return false;

    // managed textures sujas: upload antes de amostrar
    for (uint32_t i = 0; i < 16; i++) {
        if (!m_st.tex[i]) continue;
        NrTexture2D *t2 = dynamic_cast<NrTexture2D *>(m_st.tex[i]);
        NrTextureCube *tc = dynamic_cast<NrTextureCube *>(m_st.tex[i]);
        NrTextureVol *tv = dynamic_cast<NrTextureVol *>(m_st.tex[i]);
        if (t2 && t2->core.pool == D3DPOOL_MANAGED && t2->core.image && t2->core.needs_upload) {
            upload_texcore(t2->core);
            t2->core.needs_upload = false;
        } else if (tc && tc->core.pool == D3DPOOL_MANAGED && tc->core.image && tc->core.needs_upload) {
            upload_texcore(tc->core);
            tc->core.needs_upload = false;
        } else if (tv && tv->core.pool == D3DPOOL_MANAGED && tv->core.image && tv->core.needs_upload) {
            upload_texcore(tv->core);
            tv->core.needs_upload = false;
        }
    }

    // buffers de stream (managed/systemmem: shadow -> device)
    if (!m_up_vb.buffer) {
        for (auto &e : elems) {
            if (e.Stream >= 16) continue;
            NrStream &s = m_st.streams[e.Stream];
            if (s.vb && (s.vb->pool == D3DPOOL_MANAGED || s.vb->pool == D3DPOOL_SYSTEMMEM) &&
                s.vb->needs_upload) {
                upload_buffer_shadow(*s.vb);
                s.vb->needs_upload = false;
            }
        }
        if (indexed && m_st.ib && (m_st.ib->pool == D3DPOOL_MANAGED || m_st.ib->pool == D3DPOOL_SYSTEMMEM) &&
            m_st.ib->needs_upload) {
            upload_buffer_shadow(*m_st.ib);
            m_st.ib->needs_upload = false;
        }
    }

    // constantes primeiro (os descritores apontam para o ring; o offset real
    // vai no bind como dynamic offset relativo a 0)
    StagingRing::Alloc vs_c, ps_c, ib_c;
    // 4096 (consts float) + 512 (i#/b#) no mesmo alloc do VS; pack_vs_constants
    // grava ate cap bytes.
    if (!ring().alloc(4096 + 512, 256, &vs_c)) { partial_flush_and_wait(); if (!ring().alloc(4096 + 512, 256, &vs_c)) return false; }
    pack_vs_constants((uint8_t *)vs_c.mapped, 4096 + 512);
    if (!ring().alloc(4096, 256, &ps_c)) { partial_flush_and_wait(); if (!ring().alloc(4096, 256, &ps_c)) return false; }
    pack_ps_constants((uint8_t *)ps_c.mapped, 4096);
    bool need_ib = (vsx && vsx->uses_ib) || (psx && psx->uses_ib);
    if (need_ib) {
        if (!ring().alloc(512, 256, &ib_c)) { partial_flush_and_wait(); if (!ring().alloc(512, 256, &ib_c)) return false; }
        memcpy(ib_c.mapped, (const uint8_t *)vs_c.mapped + 4096, 512);
    }

    // descriptor set (texturas + samplers + UBOs); cache por frame
    uint32_t samp_packed[16];
    uint64_t texhash = 0x1234;
    for (uint32_t i = 0; i < 16; i++) {
        samp_packed[i] = resolve_sampler(i);
        texhash = fnv1a_mix(texhash, (uint32_t)(uintptr_t)m_st.tex[i]);
        texhash = fnv1a_mix(texhash, samp_packed[i]);
    }
    texhash = fnv1a_mix(texhash, need_ib ? 0x1B : 0);
    uint32_t slot = m_frame_slot;
    FrameCtx &f = m_frames[slot];
    VkDescriptorPool pool = f.desc_pool();
    VkDescriptorSet dset = m_descs.get(pool, texhash, [&](VkDescriptorSet set) {
        VkDescriptorImageInfo imgs[16]{};
        VkWriteDescriptorSet writes[20]{};
        uint32_t nw = 0;
        // UBOs: descriptor = {ring, 0, range}; dynamic offset no bind
        VkDescriptorBufferInfo bvs{vs_c.buffer, 0, 4096};
        VkDescriptorBufferInfo bps{ps_c.buffer, 0, 4096};
        writes[nw] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, kBindVsUbo, 0, 1,
                      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr, &bvs, nullptr};
        nw++;
        writes[nw] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, kBindPsUbo, 0, 1,
                      VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr, &bps, nullptr};
        nw++;
        for (uint32_t i = 0; i < 16; i++) {
            VkImageView view = VK_NULL_HANDLE;
            VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            IDirect3DBaseTexture9 *bt = m_st.tex[i];
            if (bt) {
                NrTexture2D *t2 = dynamic_cast<NrTexture2D *>(bt);
                NrTextureCube *tc = dynamic_cast<NrTextureCube *>(bt);
                NrTextureVol *tv = dynamic_cast<NrTextureVol *>(bt);
                if (t2) { view = t2->core.view_sampled; layout = t2->core.layout; }
                if (tc) { view = tc->core.view_sampled; layout = tc->core.layout; }
                if (tv) { view = tv->core.view_sampled; layout = tv->core.layout; }
                if (layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL && view) {
                    // transicao para amostragem (guarda o layout antigo)
                    SurfCore tmp;
                    if (t2) tmp.tex = &t2->core;
                    if (tc) tmp.tex = &tc->core;
                    if (tv) tmp.tex = &tv->core;
                    transition_image(tmp, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                     VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
                    layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                }
            }
            if (!view) {
                // dummy 1x1 (binding obrigatorio mesmo sem textura)
                create_dummy_texture();
                view = m_dummy_view;
            }
            imgs[i] = {m_samplers.get(samp_packed[i]), view, layout};
            writes[nw] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, kBindSamplerBase + i, 0, 1,
                          VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &imgs[i], nullptr, nullptr};
            nw++;
        }
        if (need_ib) {
            // constantes i#/b# (mova/loop/ifb): mesma regiao do UBO VS (a partir
            // de 4096), empacotadas no ring; range 512
            VkDescriptorBufferInfo bib{ib_c.buffer, 0, 512};
            writes[nw] = {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, kBindIbUbo, 0, 1,
                          VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr, &bib, nullptr};
            nw++;
        }
        vkUpdateDescriptorSets(m_vk->device(), nw, writes, 0, nullptr);
    });
    if (!dset) return false;

    vkCmdBindPipeline(cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, p);
    VkDescriptorSet sets[1] = {dset};
    uint32_t dyn_off[3] = {(uint32_t)vs_c.offset, (uint32_t)ps_c.offset, (uint32_t)ib_c.offset};
    vkCmdBindDescriptorSets(cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines.layout(), 0, 1, sets,
                            need_ib ? 3u : 2u, dyn_off);
    vkCmdSetStencilReference(cmd(), VK_STENCIL_FACE_FRONT_AND_BACK, key.stencil_ref);

    // vertex buffers
    if (m_up_vb.buffer) {
        VkBuffer b = m_up_vb.buffer;
        VkDeviceSize off = m_up_vb.offset;
        vkCmdBindVertexBuffers(cmd(), 0, 1, &b, &off);
        if (indexed) {
            VkIndexType it = m_up_index32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16;
            vkCmdBindIndexBuffer(cmd(), m_up_ib.buffer, m_up_ib.offset, it);
            vkCmdDrawIndexed(cmd(), m_up_index_count, 1, 0, base_vtx, 0);
        } else {
            vkCmdDraw(cmd(), num_vtx, 1, 0, 0);
        }
    } else {
        // bindings por stream: um vkCmdBindVertexBuffers por stream (o binding
        // no pipeline e o numero do stream D3D9)
        uint32_t bound_streams = 0;
        for (auto &e : elems) {
            if (e.Stream >= 16) continue;
            if (bound_streams & (1u << e.Stream)) continue;
            bound_streams |= (1u << e.Stream);
            NrStream &s = m_st.streams[e.Stream];
            if (!s.vb || !s.vb->buffer) continue;
            VkBuffer b = s.vb->buffer;
            VkDeviceSize off = s.offset;
            vkCmdBindVertexBuffers(cmd(), e.Stream, 1, &b, &off);
        }
        if (indexed) {
            if (!m_st.ib || !m_st.ib->buffer) return false;
            vkCmdBindIndexBuffer(cmd(), m_st.ib->buffer, 0,
                                 m_st.ib->fmt == D3DFMT_INDEX32 ? VK_INDEX_TYPE_UINT32
                                                                : VK_INDEX_TYPE_UINT16);
            if (fan) {
                // TRIANGLE_FAN nao existe em Vulkan: indices gerados no ring
                UINT tri = count;
                VkDeviceSize ibytes = (VkDeviceSize)tri * 3 * 4;
                StagingRing::Alloc a;
                if (!ring().alloc(ibytes, 4, &a)) return false;
                uint32_t *idx = (uint32_t *)a.mapped;
                for (UINT t = 0; t < tri; t++) {
                    idx[t * 3 + 0] = 0;
                    idx[t * 3 + 1] = start_idx + t + 1;
                    idx[t * 3 + 2] = start_idx + t + 2;
                }
                vkCmdBindIndexBuffer(cmd(), a.buffer, a.offset, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd(), tri * 3, 1, 0, base_vtx, 0);
            } else {
                UINT icount = primitive_vertex_count(prim, count);
                vkCmdDrawIndexed(cmd(), icount, 1, start_idx, base_vtx, 0);
            }
        } else {
            if (fan) {
                UINT tri = count;
                VkDeviceSize ibytes = (VkDeviceSize)tri * 3 * 4;
                StagingRing::Alloc a;
                if (!ring().alloc(ibytes, 4, &a)) return false;
                uint32_t *idx = (uint32_t *)a.mapped;
                for (UINT t = 0; t < tri; t++) {
                    idx[t * 3 + 0] = 0;
                    idx[t * 3 + 1] = start_vtx + t + 1;
                    idx[t * 3 + 2] = start_vtx + t + 2;
                }
                vkCmdBindIndexBuffer(cmd(), a.buffer, a.offset, VK_INDEX_TYPE_UINT32);
                vkCmdDrawIndexed(cmd(), tri * 3, 1, 0, 0, 0);
            } else {
                UINT pv = primitive_vertex_count(prim, count);
                vkCmdDraw(cmd(), pv, 1, start_vtx, 0);
            }
        }
    }
    m_stat_draws++;
    return true;
}

} // namespace nvr
