// ShaderCache (compilacao de bytecode/FFF com cache), uploads de textura,
// blit/gamma/present e fences auxiliares.
#include "nvr_device.h"
#include "nvr_com_impl.h"
#include <algorithm>
#include <SDL2/SDL.h>

namespace nvr {

// ------------------------------------------------------------ ShaderCache
static uint8_t *sig_comps_for(NrDevice *dev, uint8_t comps[32]) {
    DeviceState &st = dev->state();
    std::vector<D3DVERTEXELEMENT9> elems;
    if (st.decl) elems = st.decl->elems;
    else fvf_to_elements(st.fvf, elems);
    memset(comps, 0, 32);
    for (auto &e : elems) {
        uint32_t loc = usage_to_location(e.Usage, e.UsageIndex);
        if (loc >= 32) continue;
        switch (e.Type) {
        case D3DDECLTYPE_FLOAT1: comps[loc] = 1; break;
        case D3DDECLTYPE_FLOAT2: comps[loc] = 2; break;
        case D3DDECLTYPE_FLOAT3: comps[loc] = 3; break;
        default: comps[loc] = 4; break;
        }
    }
    return comps;
}

const XlateOut *ShaderCache::get_vs(const void *bc, size_t len, bool flat) {
    uint64_t hash = fnv1a(bc, len);
    hash = fnv1a_mix(hash, flat ? 0xF1A7 : 0);
    uint8_t comps[32];
    sig_comps_for(m_dev, comps);
    hash = fnv1a(comps, 32, (uint32_t)hash);
    std::lock_guard<std::mutex> l(m_mutex);
    auto it = m_vs.find(hash);
    if (it != m_vs.end()) return &it->second.second;
    BcShader parsed;
    std::string err;
    if (!bc_parse((const uint32_t *)bc, len / 4, parsed, err)) {
        NVR_ERROR("vs: parse falhou: %s", err.c_str());
        return nullptr;
    }
    VsXlateIn in{&parsed, comps, flat};
    XlateOut out;
    if (!translate_vs(in, out)) {
        NVR_ERROR("vs: traducao falhou: %s", out.error.c_str());
        return nullptr;
    }
    auto &slot = m_vs[hash];
    slot.first.assign((const uint8_t *)bc, (const uint8_t *)bc + len);
    slot.second = std::move(out);
    return &slot.second;
}

const XlateOut *ShaderCache::get_ps(const void *bc, size_t len, bool alpha_test, uint32_t cmp) {
    uint64_t hash = fnv1a(bc, len);
    hash = fnv1a_mix(hash, (alpha_test ? 0xA700 : 0) | (cmp << 8));
    std::lock_guard<std::mutex> l(m_mutex);
    auto it = m_ps.find(hash);
    if (it != m_ps.end()) return &it->second.second;
    BcShader parsed;
    std::string err;
    if (!bc_parse((const uint32_t *)bc, len / 4, parsed, err)) {
        NVR_ERROR("ps: parse falhou: %s", err.c_str());
        return nullptr;
    }
    PsXlateIn in{&parsed, alpha_test, cmp};
    XlateOut out;
    if (!translate_ps(in, out)) {
        NVR_ERROR("ps: traducao falhou: %s", out.error.c_str());
        return nullptr;
    }
    auto &slot = m_ps[hash];
    slot.first.assign((const uint8_t *)bc, (const uint8_t *)bc + len);
    slot.second = std::move(out);
    return &slot.second;
}

const XlateOut *ShaderCache::get_fff_vs(uint64_t sig_hash, const FffVsCfg &cfg, const uint8_t *in_comps) {
    std::lock_guard<std::mutex> l(m_mutex);
    auto it = m_fff_vs.find(sig_hash);
    if (it != m_fff_vs.end()) return &it->second.second;
    FffVsCfg cfg2 = cfg;
    cfg2.in_comps = in_comps;
    XlateOut out;
    if (!generate_fff_vs(cfg2, out)) {
        NVR_ERROR("fff vs: geracao falhou");
        return nullptr;
    }
    auto &slot = m_fff_vs[sig_hash];
    slot.first = cfg2;
    slot.first.in_comps = nullptr;   // nao guardamos ponteiro (o cache e por hash)
    slot.second = std::move(out);
    return &slot.second;
}

const XlateOut *ShaderCache::get_fff_ps(const FffPsCfg &cfg) {
    uint64_t hash = fnv1a(&cfg, sizeof cfg);
    std::lock_guard<std::mutex> l(m_mutex);
    auto it = m_fff_ps.find(hash);
    if (it != m_fff_ps.end()) return &it->second.second;
    XlateOut out;
    if (!generate_fff_ps(cfg, out)) {
        NVR_ERROR("fff ps: geracao falhou");
        return nullptr;
    }
    auto &slot = m_fff_ps[hash];
    slot.first = cfg;
    slot.second = std::move(out);
    return &slot.second;
}

// ----------------------------------------------------------- uploads
void NrDevice::upload_texcore(TexCore &t, bool) {
    if (!t.image || t.sysmem.empty()) return;
    for (uint32_t l = 0; l < t.levels; l++) {
        if (!t.dirty[l] && !t.whole_dirty && !t.needs_upload) continue;
        uint32_t layers = t.type == D3DRTYPE_CUBETEXTURE ? 6u : 1u;
        uint32_t depth = t.type == D3DRTYPE_VOLUMETEXTURE ? std::max(1u, t.d >> l) : 1u;
        for (uint32_t lay = 0; lay < layers; lay++) {
            upload_image_data(m_vk, cmd(), ring(), t.image, t.vkfmt, t.w, t.h, t.d, l, lay,
                              t.sysmem.data() + t.level_offset[l] +
                                  (t.type == D3DRTYPE_CUBETEXTURE
                                       ? (size_t)lay * (t.level_size(l) / 6)
                                       : 0),
                              t.level_pitch[l], t.level_pitch[l] * std::max(1u, t.h >> l),
                              &t.layout);
        }
        t.dirty[l] = false;
        (void)depth;
    }
    t.whole_dirty = false;
    if (t.autogen && t.levels > 1) {
        record_mipgen(cmd(), m_vk, t.image, t.vkfmt, t.w, t.h,
                      t.type == D3DRTYPE_VOLUMETEXTURE ? t.d : (t.type == D3DRTYPE_CUBETEXTURE ? 6u : 1u),
                      t.type == D3DRTYPE_VOLUMETEXTURE, t.levels, &t.layout);
    }
    t.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void NrDevice::upload_standalone(SurfCore &s) {
    (void)s;
    // surfaces standalone nao tem shadow (os dados chegam via UpdateSurface)
}

// -------------------------------------------------- fence / timestamp aux
VkFence NrDevice::submit_partial_fence() {
    partial_flush_and_wait();
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    vkCreateFence(m_vk->device(), &fci, nullptr, &fence);
    // fence sinalizado: marca "comandos anteriores completos"
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    vkQueueSubmit(m_vk->graphics_queue().queue, 0, &si, fence);
    return fence;
}

uint64_t NrDevice::timestamp_ns() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + ts.tv_nsec;
}

// ----------------------------------------------------- blit/gamma/present
// Shaders do blit: VS passa pos/uv; FS amostra + gamma opcional (ramp no UBO
// ps) + rotation via UBO vs. Clear FS: cor solida em ps c[192].
static void gen_blit_shaders(std::vector<uint32_t> &vs, std::vector<uint32_t> &fs,
                             std::vector<uint32_t> &clear_fs) {
    {
        SpirvBuilder b;
        b.capability(spv::CapabilityShader);
        b.ext_import("GLSL.std.450");
        uint32_t t_void = b.type_void();
        uint32_t t_f32 = b.type_float();
        uint32_t t_v2 = b.type_vec(t_f32, 2);
        uint32_t t_v4 = b.type_vec(t_f32, 4);
        b.start(spv::ExecutionModelVertex, "main");
        b.memory_model(spv::AddressingModelLogical, spv::MemoryModelGLSL450);
        // UBO c[256] binding 0: c[0] = (rotation, 0, 0, 0)
        uint32_t arr = b.type_array_ubo(t_v4, b.const_u32(256), 16);
        uint32_t blk = b.type_struct({arr});
        b.member_decorate(blk, 0, spv::DecorationOffset, 0);
        b.decorate(blk, spv::DecorationBlock);
        uint32_t ubo = b.variable(spv::StorageClassUniform, blk);
        b.decorate(ubo, spv::DecorationDescriptorSet, {0});
        b.decorate(ubo, spv::DecorationBinding, {0});
        // in: pos(vec2) uv(vec2)
        uint32_t in_pos = b.variable(spv::StorageClassInput, t_v2);
        b.decorate(in_pos, spv::DecorationLocation, {0});
        b.add_interface_var(spv::StorageClassInput, in_pos);
        uint32_t in_uv = b.variable(spv::StorageClassInput, t_v2);
        b.decorate(in_uv, spv::DecorationLocation, {1});
        b.add_interface_var(spv::StorageClassInput, in_uv);
        uint32_t o_pos = b.variable(spv::StorageClassOutput, t_v4);
        b.decorate(o_pos, spv::DecorationBuiltIn, {(uint32_t)spv::BuiltInPosition});
        b.add_interface_var(spv::StorageClassOutput, o_pos);
        uint32_t o_uv = b.variable(spv::StorageClassOutput, t_v2);
        b.decorate(o_uv, spv::DecorationLocation, {0});
        b.add_interface_var(spv::StorageClassOutput, o_uv);

        uint32_t fn = b.type_function(t_void, {});
        b.function_begin(b.entry_id(), t_void, fn);
        uint32_t p2 = b.load(t_v2, in_pos);
        uint32_t u2 = b.load(t_v2, in_uv);
        // rotacao em c[0].x: 0=0 1=90 2=180 3=270 (pre-rotation)
        uint32_t v4p = b.type_pointer(spv::StorageClassUniform, t_v4);
        uint32_t slot0 = b.load(t_v4, b.access_chain(v4p, ubo, {b.const_i32(0), b.const_i32(0)}));
        uint32_t rot = b.op(spv::OpConvertFToS, b.type_int(true),
                            b.composite_extract(t_f32, slot0, {0}));
        // rotaciona o NDC pos: 90: (x,y)->(-y,x); 180: ->(-x,-y); 270: ->(y,-x)
        uint32_t fx = b.composite_extract(t_f32, p2, {0});
        uint32_t fy = b.composite_extract(t_f32, p2, {1});
        uint32_t rx = fx, ry = fy;
        // 90 graus: (x,y) -> (-y, x)
        rx = b.select(t_f32, b.op(spv::OpIEqual, b.type_bool(), {rot, b.const_i32(1)}),
                      b.fneg(t_f32, fy), fx);
        ry = b.select(t_f32, b.op(spv::OpIEqual, b.type_bool(), {rot, b.const_i32(1)}), fx, fy);
        rx = b.select(t_f32, b.op(spv::OpIEqual, b.type_bool(), {rot, b.const_i32(2)}),
                      b.fneg(t_f32, fx), rx);
        ry = b.select(t_f32, b.op(spv::OpIEqual, b.type_bool(), {rot, b.const_i32(2)}),
                      b.fneg(t_f32, fy), ry);
        rx = b.select(t_f32, b.op(spv::OpIEqual, b.type_bool(), {rot, b.const_i32(3)}), fy, rx);
        ry = b.select(t_f32, b.op(spv::OpIEqual, b.type_bool(), {rot, b.const_i32(3)}),
                      b.fneg(t_f32, fx), ry);
        uint32_t pos4 = b.composite_construct(t_v4, {rx, ry, b.const_f32(0.0f), b.const_f32(1.0f)});
        b.store(o_pos, pos4);
        b.store(o_uv, u2);
        b.ret();
        b.function_end();
        vs = b.finish();
    }
    {
        SpirvBuilder b;
        b.capability(spv::CapabilityShader);
        b.ext_import("GLSL.std.450");
        uint32_t t_void = b.type_void();
        uint32_t t_f32 = b.type_float();
        uint32_t t_i32 = b.type_int(true);
        uint32_t t_v4 = b.type_vec(t_f32, 4);
        uint32_t t_f4res = b.type_sampled_image(
            b.type_image(t_f32, spv::Dim2D, false, false, false, 1, spv::ImageFormatUnknown));
        b.start(spv::ExecutionModelFragment, "main");
        b.memory_model(spv::AddressingModelLogical, spv::MemoryModelGLSL450);
        b.execution_mode(spv::ExecutionModeOriginUpperLeft);
        uint32_t arr = b.type_array_ubo(t_v4, b.const_u32(224), 16);
        uint32_t blk = b.type_struct({arr, t_v4});
        b.member_decorate(blk, 0, spv::DecorationOffset, 0);
        b.member_decorate(blk, 1, spv::DecorationOffset, 224 * 16);
        b.decorate(blk, spv::DecorationBlock);
        uint32_t ubo = b.variable(spv::StorageClassUniform, blk);
        b.decorate(ubo, spv::DecorationDescriptorSet, {0});
        b.decorate(ubo, spv::DecorationBinding, {1});
        uint32_t samp = b.variable(spv::StorageClassUniformConstant, t_f4res);
        b.decorate(samp, spv::DecorationDescriptorSet, {0});
        b.decorate(samp, spv::DecorationBinding, {2});
        uint32_t in_uv = b.variable(spv::StorageClassInput, b.type_vec(t_f32, 2));
        b.decorate(in_uv, spv::DecorationLocation, {0});
        b.add_interface_var(spv::StorageClassInput, in_uv);
        uint32_t o_col = b.variable(spv::StorageClassOutput, t_v4);
        b.decorate(o_col, spv::DecorationLocation, {0});
        b.add_interface_var(spv::StorageClassOutput, o_col);
        uint32_t fn = b.type_function(t_void, {});
        b.function_begin(b.entry_id(), t_void, fn);
        uint32_t uv = b.load(b.type_vec(t_f32, 2), in_uv);
        uint32_t col = b.image_sample_implicit(t_f4res, samp, uv);
        // gamma: c[195].x flag; ramp: red em c[0..63] (4 por vec4), green c[64..127],
        // blue c[128..191]
        uint32_t v4p = b.type_pointer(spv::StorageClassUniform, t_v4);
        uint32_t flag4 = b.load(t_v4, b.access_chain(v4p, ubo, {b.const_i32(0), b.const_i32(195)}));
        uint32_t use_gamma = b.op(spv::OpFUnordNotEqual, b.type_bool(),
                                  {b.composite_extract(t_f32, flag4, {0}), b.const_f32(0.0f)});
        uint32_t src_r = b.composite_extract(t_f32, col, {0});
        uint32_t src_g = b.composite_extract(t_f32, col, {1});
        uint32_t src_b = b.composite_extract(t_f32, col, {2});
        auto ramp_lookup = [&](uint32_t base) {
            uint32_t idx = b.op(spv::OpConvertFToS, t_i32,
                                b.fmul(t_f32, src_r, b.const_f32(255.0f)));
            uint32_t q = b.op(spv::OpShiftRightLogical, t_i32, {idx, b.const_i32(2)});
            uint32_t c = b.op(spv::OpBitwiseAnd, t_i32, {idx, b.const_i32(3)});
            uint32_t v4 = b.load(t_v4, b.access_chain(v4p, ubo, {b.const_i32(0), b.op(spv::OpIAdd, t_i32, {b.const_i32(base), q})}));
            // componente dinamico: OpVectorExtractDynamic (CompositeExtract exige constante)
            return b.op(spv::OpVectorExtractDynamic, t_f32, {v4, c});
        };
        uint32_t r = ramp_lookup(0);
        // verde/azul usam src_g/src_b: reimplementa com canal certo
        auto ramp_lookup2 = [&](uint32_t base, uint32_t src) {
            uint32_t idx = b.op(spv::OpConvertFToS, t_i32, b.fmul(t_f32, src, b.const_f32(255.0f)));
            uint32_t q = b.op(spv::OpShiftRightLogical, t_i32, {idx, b.const_i32(2)});
            uint32_t c = b.op(spv::OpBitwiseAnd, t_i32, {idx, b.const_i32(3)});
            uint32_t v4 = b.load(t_v4, b.access_chain(v4p, ubo, {b.const_i32(0), b.op(spv::OpIAdd, t_i32, {b.const_i32(base), q})}));
            // componente dinamico: OpVectorExtractDynamic (CompositeExtract exige constante)
            return b.op(spv::OpVectorExtractDynamic, t_f32, {v4, c});
        };
        uint32_t g = ramp_lookup2(64, src_g);
        uint32_t bb = ramp_lookup2(128, src_b);
        uint32_t one4 = b.composite_construct(t_v4, {b.const_f32(1.0f), b.const_f32(1.0f),
                                                     b.const_f32(1.0f), b.const_f32(1.0f)});
        uint32_t gcol = b.composite_construct(t_v4, {r, g, bb, b.composite_extract(t_f32, col, {3})});
        uint32_t outc = b.select(t_v4, use_gamma, gcol, col);
        (void)one4;
        b.store(o_col, outc);
        b.ret();
        b.function_end();
        fs = b.finish();
    }
    {
        SpirvBuilder b;
        b.capability(spv::CapabilityShader);
        uint32_t t_void = b.type_void();
        uint32_t t_f32 = b.type_float();
        uint32_t t_v4 = b.type_vec(t_f32, 4);
        b.start(spv::ExecutionModelFragment, "main");
        b.memory_model(spv::AddressingModelLogical, spv::MemoryModelGLSL450);
        b.execution_mode(spv::ExecutionModeOriginUpperLeft);
        uint32_t arr = b.type_array_ubo(t_v4, b.const_u32(224), 16);
        uint32_t blk = b.type_struct({arr, t_v4});
        b.member_decorate(blk, 0, spv::DecorationOffset, 0);
        b.member_decorate(blk, 1, spv::DecorationOffset, 224 * 16);
        b.decorate(blk, spv::DecorationBlock);
        uint32_t ubo = b.variable(spv::StorageClassUniform, blk);
        b.decorate(ubo, spv::DecorationDescriptorSet, {0});
        b.decorate(ubo, spv::DecorationBinding, {1});
        uint32_t o_col = b.variable(spv::StorageClassOutput, t_v4);
        b.decorate(o_col, spv::DecorationLocation, {0});
        b.add_interface_var(spv::StorageClassOutput, o_col);
        uint32_t fn = b.type_function(t_void, {});
        b.function_begin(b.entry_id(), t_void, fn);
        uint32_t v4p = b.type_pointer(spv::StorageClassUniform, t_v4);
        uint32_t c4 = b.load(t_v4, b.access_chain(v4p, ubo, {b.const_i32(0), b.const_i32(192)}));
        b.store(o_col, c4);
        b.ret();
        b.function_end();
        clear_fs = b.finish();
    }
}

static VkPipeline make_fullscreen_pipeline(NrDevice *dev, VkShaderModule vs, VkShaderModule fs,
                                           VkFormat color_fmt, VkRenderPass rp) {
    VkPipelineShaderStageCreateInfo stages[2] = {
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,
         vs, "main", nullptr},
        {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT,
         fs, "main", nullptr}};
    VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkVertexInputBindingDescription bind{0, 16, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[2] = {{0, 0, VK_FORMAT_R32G32_SFLOAT, 0},
                                                  {1, 0, VK_FORMAT_R32G32_SFLOAT, 8}};
    vi.vertexBindingDescriptionCount = 1;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = 2;
    vi.pVertexAttributeDescriptions = attrs;
    VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = VK_CULL_MODE_NONE;
    rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    rs.lineWidth = 1.0f;
    VkPipelineDepthStencilStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
    VkPipelineColorBlendAttachmentState att{};
    att.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT |
                         VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    cb.attachmentCount = 1;
    cb.pAttachments = &att;
    VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkDynamicState dyns[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = 2;
    dyn.pDynamicStates = dyns;
    VkGraphicsPipelineCreateInfo pi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pi.stageCount = 2;
    pi.pStages = stages;
    pi.pVertexInputState = &vi;
    pi.pInputAssemblyState = &ia;
    pi.pRasterizationState = &rs;
    pi.pDepthStencilState = &ds;
    pi.pColorBlendState = &cb;
    pi.pMultisampleState = &ms;
    pi.pDynamicState = &dyn;
    pi.layout = dev->pipelines_layout();
    pi.renderPass = rp;
    VkPipeline p = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(dev->vk().device(), VK_NULL_HANDLE, 1, &pi, nullptr, &p) != VK_SUCCESS)
        NVR_ERROR("pipeline de blit falhou");
    return p;
}

void NrDevice::record_present_blit(uint32_t image_index) {
    // cria pipelines de blit/clear uma vez
    if (!m_blit_vs) {
        std::vector<uint32_t> vs, fs, cfs;
        gen_blit_shaders(vs, fs, cfs);
        m_blit_vs = module_for(vs, 0xB1C70);
        m_blit_fs = module_for(fs, 0xB1C71);
        m_clear_fs = module_for(cfs, 0xB1C72);
    }
    // recreate da swapchain (resize/rotacao): framebuffers antigos morrem
    if (m_sc_gen != m_swap.generation()) {
        for (uint32_t i = 0; i < m_nframes; i++) m_frames[i].wait_fence();   // FBs em uso
        for (auto &fbi : m_swap_fb)
            if (fbi) { vkDestroyFramebuffer(m_vk->device(), fbi, nullptr); fbi = VK_NULL_HANDLE; }
        m_sc_gen = m_swap.generation();
    }
    // render pass da swapchain (cache)
    PassKey pk{};
    pk.color_fmt = m_swap.format();
    pk.color_load = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    pk.color_store = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderPass rp = m_passes.get(pk);
    if (!m_blit_pipe || !m_clear_pipe) {
        m_blit_pipe = make_fullscreen_pipeline(this, m_blit_vs, m_blit_fs, m_swap.format(), rp);
        m_clear_pipe = make_fullscreen_pipeline(this, m_blit_vs, m_clear_fs, m_swap.format(), rp);
    }

    // transicao da imagem da swapchain p/ COLOR_ATTACHMENT
    image_barrier(cmd(), m_swap.image(image_index), VK_IMAGE_ASPECT_COLOR_BIT,
                  VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0, 1, 1,
                  VK_ACCESS_MEMORY_READ_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT);

    PassKey fpk = pk;
    fpk.color_load = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    VkRenderPass fbp_rp = m_passes.get(fpk);
    // framebuffer por imagem (propriedade do device; invalidado por geracao)
    VkFramebuffer fb = m_swap_fb[image_index];
    if (!fb) {
        VkImageView view = m_swap.view(image_index);
        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        fi.renderPass = fbp_rp;
        fi.attachmentCount = 1;
        fi.pAttachments = &view;
        fi.width = m_swap.extent().width;
        fi.height = m_swap.extent().height;
        fi.layers = 1;
        if (vkCreateFramebuffer(m_vk->device(), &fi, nullptr, &fb) != VK_SUCCESS) {
            NVR_ERROR("framebuffer da swapchain %u falhou", image_index);
            return;
        }
        m_swap_fb[image_index] = fb;
    }

    VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    bi.renderPass = fbp_rp;
    bi.framebuffer = fb;
    VkExtent2D ext = m_swap.extent();
    bool rotated = m_swap.transform() == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ||
                   m_swap.transform() == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR;
    bi.renderArea = {{0, 0}, {ext.width, ext.height}};
    VkClearValue cv{};
    bi.clearValueCount = 1;
    bi.pClearValues = &cv;
    vkCmdBeginRenderPass(cmd(), &bi, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0, 0, (float)ext.width, (float)ext.height, 0, 1};
    if (rotated) vp = {0, 0, (float)ext.height, (float)ext.width, 0, 1};
    vkCmdSetViewport(cmd(), 0, 1, &vp);
    VkRect2D sc{{0, 0}, {ext.width, ext.height}};
    if (rotated) sc.extent = {ext.height, ext.width};
    vkCmdSetScissor(cmd(), 0, 1, &sc);

    // verts do fullscreen triangle (pos2+uv2)
    StagingRing::Alloc a;
    if (!ring().alloc(48, 16, &a)) return;
    float *v = (float *)a.mapped;
    v[0] = -1; v[1] = -1; v[2] = 0; v[3] = 1;   // pos, uv
    v[4] = 3; v[5] = -1; v[6] = 2; v[7] = 1;
    v[8] = -1; v[9] = 3; v[10] = 0; v[11] = -1;

    // UBO vs: rotation; UBO ps: gamma ramp + flag
    StagingRing::Alloc vsc, psc;
    ring().alloc(4096, 256, &vsc);
    memset(vsc.mapped, 0, 4096);
    uint32_t rot = m_swap.transform() == VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR ? 1
                 : m_swap.transform() == VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR ? 2
                 : m_swap.transform() == VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR ? 3 : 0;
    ((float *)vsc.mapped)[0] = (float)rot;
    ring().alloc(4096, 256, &psc);
    float *pc = (float *)psc.mapped;
    memset(pc, 0, 4096);
    if (!m_ramp_identity) {
        for (uint32_t i = 0; i < 256; i++) {
            pc[(i / 4) * 4 + (i % 4)] = (float)m_ramp.red[i] / 65535.0f;                 // red: c[0..63]
            pc[256 + (i / 4) * 4 + (i % 4)] = (float)m_ramp.green[i] / 65535.0f;         // green: c[64..127]
            pc[512 + (i / 4) * 4 + (i % 4)] = (float)m_ramp.blue[i] / 65535.0f;          // blue: c[128..191]
        }
        pc[195 * 4] = 1.0f;   // use_gamma
    }

    // descriptor set do blit: UBOs + backbuffer em binding 2
    VkDescriptorPool pool = m_frames[m_started_slot].desc_pool();
    VkDescriptorSetLayout sl = m_pipelines.set_layout();
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &sl;
    VkDescriptorSet set = VK_NULL_HANDLE;
    vkAllocateDescriptorSets(m_vk->device(), &ai, &set);
    // UBOs dinamicos: descriptor aponta para o ring (offset 0); o offset real
    // vai no bind (pDynamicOffsets). range = 4096 cobre os slots usados.
    VkDescriptorBufferInfo bvs{vsc.buffer, 0, 4096};
    VkDescriptorBufferInfo bps{psc.buffer, 0, 4096};
    VkDescriptorImageInfo img{m_samplers.get(0x11), m_backbuffer->core.view_sampled,
                              m_backbuffer->core.layout};
    if (m_backbuffer->core.layout != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        transition_image(m_backbuffer->core, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                         VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        img.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    VkWriteDescriptorSet w[3] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr, &bvs, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr, &bps, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1,
         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &img, nullptr, nullptr}};
    vkUpdateDescriptorSets(m_vk->device(), 3, w, 0, nullptr);

    vkCmdBindPipeline(cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_blit_pipe);
    vkCmdBindDescriptorSets(cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines.layout(), 0, 1, &set,
                            2, (uint32_t[]){(uint32_t)vsc.offset, (uint32_t)psc.offset});
    vkCmdBindVertexBuffers(cmd(), 0, 1, &a.buffer, &a.offset);
    vkCmdDraw(cmd(), 3, 1, 0, 0);
    vkCmdEndRenderPass(cmd());
    // backbuffer volta para COLOR_ATTACHMENT_OPTIMAL (estado padrao do rt)
    transition_image(m_backbuffer->core, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
}

// helpers do blit de superficies (StretchRect/UpdateSurface/ColorFill):
// pass proprio (sem tocar no rt corrente do estado), framebuffer por view,
// layouts controlados nos objetos reais.
static void blit_quad_verts(StagingRing &ring, const RECT *srect, const RECT *drect,
                            uint32_t sw, uint32_t sh, uint32_t dw, uint32_t dh,
                            StagingRing::Alloc *out) {
    ring.alloc(48, 16, out);
    float *v = (float *)out->mapped;
    float x0 = 2.0f * drect->left / (float)dw - 1.0f, x1 = 2.0f * drect->right / (float)dw - 1.0f;
    float y0 = 1.0f - 2.0f * drect->top / (float)dh, y1 = 1.0f - 2.0f * drect->bottom / (float)dh;
    float u0 = (float)srect->left / (float)sw, u1 = (float)srect->right / (float)sw;
    float v0 = (float)srect->top / (float)sh, v1 = (float)srect->bottom / (float)sh;
    v[0] = x0; v[1] = y0; v[2] = u0; v[3] = v0;
    v[4] = x1; v[5] = y0; v[6] = u1; v[7] = v0;
    v[8] = x0; v[9] = y1; v[10] = u0; v[11] = v1;
    v[12] = x1; v[13] = y0; v[14] = u1; v[15] = v0;
    v[16] = x1; v[17] = y1; v[18] = u1; v[19] = v1;
    v[20] = x0; v[21] = y1; v[22] = u0; v[23] = v1;
}

VkPipeline NrDevice::make_blit_pipeline_for(VkFormat fmt, VkRenderPass rp, bool clear) {
    return make_fullscreen_pipeline(this, m_blit_vs, clear ? m_clear_fs : m_blit_fs, fmt, rp);
}

HRESULT NrDevice::blit_surfaces(SurfCore &src, const RECT *srect_in, SurfCore &dst,
                                const RECT *drect_in, bool linear) {
    if (!src.img() || !dst.img()) return D3DERR_INVALIDCALL;
    if (!m_blit_vs) {
        std::vector<uint32_t> vs, fs, cfs;
        gen_blit_shaders(vs, fs, cfs);
        m_blit_vs = module_for(vs, 0xB1C70);
        m_blit_fs = module_for(fs, 0xB1C71);
        m_clear_fs = module_for(cfs, 0xB1C72);
    }
    uint32_t sw = src.width(), sh = src.height(), dw = dst.width(), dh = dst.height();
    RECT fs = {0, 0, (LONG)sw, (LONG)sh}, fd = {0, 0, (LONG)dw, (LONG)dh};
    const RECT *srect = srect_in ? srect_in : &fs;
    const RECT *drect = drect_in ? drect_in : &fd;

    VkImageView sview = src.tex ? src.tex->view_sampled : src.view_sampled;
    VkImageView dview = dst.tex ? dst.tex->view_rt : dst.view_rt;
    if (!sview || !dview) return D3DERR_INVALIDCALL;

    // transicao do src para amostragem (no objeto real: layout continua valido)
    transition_image(src, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
    // dst precisa estar em COLOR_ATTACHMENT_OPTIMAL (estado normal de um RT)
    transition_image(dst, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    end_pass();   // blit roda fora do pass do jogo; o pass reabre depois (load)

    PassKey pk{};
    pk.color_fmt = dst.format();
    pk.color_load = VK_ATTACHMENT_LOAD_OP_LOAD;
    pk.color_store = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderPass rp = m_passes.get(pk);
    if (!rp) return D3DERR_INVALIDCALL;
    VkFramebuffer fb = m_framebuffers.get_by_view(dview, VK_NULL_HANDLE, rp, dw, dh);
    if (!fb) return D3DERR_INVALIDCALL;

    VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    bi.renderPass = rp;
    bi.framebuffer = fb;
    bi.renderArea = {{0, 0}, {dw, dh}};
    vkCmdBeginRenderPass(cmd(), &bi, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0, 0, (float)dw, (float)dh, 0, 1};
    vkCmdSetViewport(cmd(), 0, 1, &vp);
    VkRect2D sc{{0, 0}, {dw, dh}};
    vkCmdSetScissor(cmd(), 0, 1, &sc);

    StagingRing::Alloc a;
    blit_quad_verts(ring(), srect, drect, sw, sh, dw, dh, &a);
    StagingRing::Alloc vsc, psc;
    if (!ring().alloc(4096, 256, &vsc) || !ring().alloc(4096, 256, &psc)) {
        partial_flush_and_wait();
        if (!ring().alloc(4096, 256, &vsc) || !ring().alloc(4096, 256, &psc))
            return D3DERR_OUTOFVIDEOMEMORY;
    }
    memset(vsc.mapped, 0, 4096);
    memset(psc.mapped, 0, 4096);

    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorPool pool = m_frames[m_started_slot].desc_pool();
    VkDescriptorSetLayout sl = m_pipelines.set_layout();
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &sl;
    vkAllocateDescriptorSets(m_vk->device(), &ai, &set);
    VkDescriptorBufferInfo bvs{vsc.buffer, 0, 4096};
    VkDescriptorBufferInfo bps{psc.buffer, 0, 4096};
    VkDescriptorImageInfo img{m_samplers.get(linear ? 0x11u : 0u), sview,
                              VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w[3] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr, &bvs, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr, &bps, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1,
         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &img, nullptr, nullptr}};
    vkUpdateDescriptorSets(m_vk->device(), 3, w, 0, nullptr);

    VkPipeline p = make_blit_pipeline_for(dst.format(), rp, false);
    vkCmdBindPipeline(cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, p);
    vkCmdBindDescriptorSets(cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines.layout(), 0, 1, &set,
                            2, (uint32_t[]){(uint32_t)vsc.offset, (uint32_t)psc.offset});
    vkCmdBindVertexBuffers(cmd(), 0, 1, &a.buffer, &a.offset);
    vkCmdDraw(cmd(), 6, 1, 0, 0);
    vkCmdEndRenderPass(cmd());
    return D3D_OK;
}

HRESULT NrDevice::blit_surfaces_fill(SurfCore &dst, const RECT *rect_in, const float color[4]) {
    if (!dst.img()) return D3DERR_INVALIDCALL;
    if (!m_blit_vs) {
        std::vector<uint32_t> vs, fs, cfs;
        gen_blit_shaders(vs, fs, cfs);
        m_blit_vs = module_for(vs, 0xB1C70);
        m_blit_fs = module_for(fs, 0xB1C71);
        m_clear_fs = module_for(cfs, 0xB1C72);
    }
    if (!m_dummy_view) create_dummy_texture();
    uint32_t dw = dst.width(), dh = dst.height();
    RECT fd = {0, 0, (LONG)dw, (LONG)dh};
    const RECT *rect = rect_in ? rect_in : &fd;

    VkImageView dview = dst.tex ? dst.tex->view_rt : dst.view_rt;
    if (!dview) return D3DERR_INVALIDCALL;
    transition_image(dst, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

    end_pass();
    PassKey pk{};
    pk.color_fmt = dst.format();
    pk.color_load = VK_ATTACHMENT_LOAD_OP_LOAD;
    pk.color_store = VK_ATTACHMENT_STORE_OP_STORE;
    VkRenderPass rp = m_passes.get(pk);
    if (!rp) return D3DERR_INVALIDCALL;
    VkFramebuffer fb = m_framebuffers.get_by_view(dview, VK_NULL_HANDLE, rp, dw, dh);
    if (!fb) return D3DERR_INVALIDCALL;

    VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    bi.renderPass = rp;
    bi.framebuffer = fb;
    bi.renderArea = {{0, 0}, {dw, dh}};
    vkCmdBeginRenderPass(cmd(), &bi, VK_SUBPASS_CONTENTS_INLINE);
    VkViewport vp{0, 0, (float)dw, (float)dh, 0, 1};
    vkCmdSetViewport(cmd(), 0, 1, &vp);
    VkRect2D sc{{0, 0}, {dw, dh}};
    vkCmdSetScissor(cmd(), 0, 1, &sc);

    StagingRing::Alloc a;
    blit_quad_verts(ring(), &fd, rect, dw, dh, dw, dh, &a);
    StagingRing::Alloc vsc, psc;
    if (!ring().alloc(4096, 256, &vsc) || !ring().alloc(4096, 256, &psc)) {
        partial_flush_and_wait();
        if (!ring().alloc(4096, 256, &vsc) || !ring().alloc(4096, 256, &psc))
            return D3DERR_OUTOFVIDEOMEMORY;
    }
    memset(vsc.mapped, 0, 4096);
    memset(psc.mapped, 0, 4096);
    memcpy((float *)psc.mapped + 192 * 4, color, 16);

    VkDescriptorSet set = VK_NULL_HANDLE;
    VkDescriptorPool pool = m_frames[m_started_slot].desc_pool();
    VkDescriptorSetLayout sl = m_pipelines.set_layout();
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &sl;
    vkAllocateDescriptorSets(m_vk->device(), &ai, &set);
    VkDescriptorBufferInfo bvs{vsc.buffer, 0, 4096};
    VkDescriptorBufferInfo bps{psc.buffer, 0, 4096};
    VkDescriptorImageInfo img{m_samplers.get(0), m_dummy_view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    VkWriteDescriptorSet w[3] = {
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 0, 0, 1,
         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr, &bvs, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 1, 0, 1,
         VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, nullptr, &bps, nullptr},
        {VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, set, 2, 0, 1,
         VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, &img, nullptr, nullptr}};
    vkUpdateDescriptorSets(m_vk->device(), 3, w, 0, nullptr);

    VkPipeline p = make_blit_pipeline_for(dst.format(), rp, true);
    vkCmdBindPipeline(cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, p);
    vkCmdBindDescriptorSets(cmd(), VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelines.layout(), 0, 1, &set,
                            2, (uint32_t[]){(uint32_t)vsc.offset, (uint32_t)psc.offset});
    vkCmdBindVertexBuffers(cmd(), 0, 1, &a.buffer, &a.offset);
    vkCmdDraw(cmd(), 6, 1, 0, 0);
    vkCmdEndRenderPass(cmd());
    return D3D_OK;
}

} // namespace nvr
