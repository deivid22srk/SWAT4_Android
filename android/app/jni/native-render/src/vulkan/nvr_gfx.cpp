#include "nvr_gfx.h"
#include <sys/stat.h>
#include <thread>
#include <condition_variable>
#include <deque>

namespace nvr {

// ------------------------------------------------------------ RenderPass
void PassCache::shutdown() {
    if (!m_core) return;
    for (auto &kv : m_map) vkDestroyRenderPass(m_core->device(), kv.second, nullptr);
    m_map.clear();
    m_core = nullptr;
}

VkRenderPass PassCache::get(const PassKey &key) {
    auto it = m_map.find(key.hash());
    if (it != m_map.end()) return it->second;
    VkDevice dev = m_core->device();

    VkAttachmentDescription color{};
    color.format = (VkFormat)key.color_fmt;
    color.samples = (VkSampleCountFlagBits)std::max(key.sample_count, 1u);
    color.loadOp = (VkAttachmentLoadOp)key.color_load;
    color.storeOp = (VkAttachmentStoreOp)key.color_store;
    color.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkAttachmentDescription ds{};
    ds.format = (VkFormat)key.ds_fmt;
    ds.samples = color.samples;
    ds.loadOp = (VkAttachmentLoadOp)key.ds_load;
    ds.storeOp = (VkAttachmentStoreOp)key.ds_store;
    bool has_stencil = false;
    {
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(m_core->physical(), ds.format, &fp);
        has_stencil = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) &&
                      (ds.format == VK_FORMAT_D24_UNORM_S8_UINT || ds.format == VK_FORMAT_D32_SFLOAT_S8_UINT ||
                       ds.format == VK_FORMAT_D16_UNORM_S8_UINT || ds.format == VK_FORMAT_S8_UINT);
    }
    ds.stencilLoadOp = has_stencil ? ds.loadOp : VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    ds.stencilStoreOp = has_stencil ? ds.storeOp : VK_ATTACHMENT_STORE_OP_DONT_CARE;
    ds.initialLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    ds.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference cref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference dref{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = 1;
    sub.pColorAttachments = &cref;
    sub.pDepthStencilAttachment = key.ds_fmt ? &dref : nullptr;

    // dependencia externa: espera o layout de attachment estar pronto
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                       (key.ds_fmt ? VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT : 0);
    dep.dstStageMask = dep.srcStageMask;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                        (key.ds_fmt ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT : 0);
    // self-dependency do subpass para ler o proprio DS dentro do mesmo pass
    // (depth test nao precisa; leitura de DS como textura sim — nao usamos).

    VkRenderPassCreateInfo pi{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    pi.attachmentCount = key.ds_fmt ? 2u : 1u;
    VkAttachmentDescription atts[2] = {color, ds};
    pi.pAttachments = atts;
    pi.subpassCount = 1;
    pi.pSubpasses = &sub;
    pi.dependencyCount = 1;
    pi.pDependencies = &dep;

    VkRenderPass pass = VK_NULL_HANDLE;
    if (vkCreateRenderPass(dev, &pi, nullptr, &pass) != VK_SUCCESS) {
        NVR_ERROR("vkCreateRenderPass falhou (fmt=%u ds=%u load=%u/%u)", key.color_fmt, key.ds_fmt,
                  key.color_load, key.ds_load);
        return VK_NULL_HANDLE;
    }
    uint64_t h = key.hash();
    m_map[h] = pass;
    m_keys[h] = key;
    NVR_TRACE("render pass criada: color=%u ds=%u load=%u/%u dsload=%u/%u samples=%u",
              key.color_fmt, key.ds_fmt, key.color_load, key.color_store, key.ds_load, key.ds_store, key.sample_count);
    return pass;
}

// ------------------------------------------------------------ SamplerCache
void SamplerCache::shutdown() {
    if (!m_core) return;
    for (auto &kv : m_map) vkDestroySampler(m_core->device(), kv.second, nullptr);
    m_map.clear();
    m_core = nullptr;
}

// Packed: [3:0]=min, [7:4]=mag, [11:8]=mip, [15:12]=U, [19:16]=V, [23:20]=W,
// [27:24]=log2(aniso), [28]=cmp disable, [31:29]=reservado
VkSampler SamplerCache::get(uint32_t packed) {
    auto it = m_map.find(packed);
    if (it != m_map.end()) return it->second;
    VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    static const VkFilter filters[2] = {VK_FILTER_NEAREST, VK_FILTER_LINEAR};
    static const VkSamplerAddressMode modes[4] = {
        VK_SAMPLER_ADDRESS_MODE_REPEAT, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER, VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT};
    uint32_t aniso_log = (packed >> 24) & 0xF;
    bool no_aniso = (packed >> 28) & 1;
    si.magFilter = filters[(packed >> 4) & 1];
    si.minFilter = filters[packed & 1];
    si.mipmapMode = ((packed >> 8) & 1) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    si.addressModeU = modes[(packed >> 12) & 3];
    si.addressModeV = modes[(packed >> 16) & 3];
    si.addressModeW = modes[(packed >> 20) & 3];
    si.anisotropyEnable = (!no_aniso && aniso_log > 0) ? VK_TRUE : VK_FALSE;
    si.maxAnisotropy = aniso_log > 0 ? (float)(1u << aniso_log) : 1.0f;
    if (si.maxAnisotropy > 16.0f) si.maxAnisotropy = 16.0f;
    si.maxLod = VK_LOD_CLAMP_NONE;
    si.borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;
    si.compareEnable = VK_FALSE;
    VkSampler s = VK_NULL_HANDLE;
    if (vkCreateSampler(m_core->device(), &si, nullptr, &s) != VK_SUCCESS) {
        NVR_ERROR("vkCreateSampler falhou (packed=%08x)", packed);
        return VK_NULL_HANDLE;
    }
    m_map[packed] = s;
    return s;
}

// ---------------------------------------------------------- PipelineCache
static const char *kPipelineMagic = "NVRC";

bool PipelineCache::init(VkCore *core, const char *disk_dir) {
    m_core = core;
    m_disk_dir = disk_dir ? disk_dir : "";

    // descriptor set layout (fixo, compartilhado)
    VkDescriptorSetLayoutBinding b[19];
    memset(b, 0, sizeof b);
    b[0] = {kBindVsUbo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    b[1] = {kBindPsUbo, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1,
            VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
    for (uint32_t i = 0; i < 16; i++)
        b[kBindSamplerBase + i] = {kBindSamplerBase + i, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                   VK_SHADER_STAGE_FRAGMENT_BIT | VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    b[kBindSsbo] = {kBindSsbo, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, nullptr};
    VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    li.bindingCount = 19;
    li.pBindings = b;
    if (vkCreateDescriptorSetLayout(core->device(), &li, nullptr, &m_set_layout) != VK_SUCCESS) {
        NVR_ERROR("descriptor set layout falhou");
        return false;
    }
    VkPipelineLayoutCreateInfo pli{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pli.setLayoutCount = 1;
    pli.pSetLayouts = &m_set_layout;
    if (vkCreatePipelineLayout(core->device(), &pli, nullptr, &m_layout) != VK_SUCCESS) {
        NVR_ERROR("pipeline layout falhou");
        return false;
    }

    // pipeline cache de disco
    VkPipelineCacheCreateInfo ci{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
    std::vector<uint8_t> blob;
    if (!m_disk_dir.empty()) {
        m_disk_path = m_disk_dir + "/pipeline_cache.bin";
        struct stat st;
        if (stat(m_disk_path.c_str(), &st) == 0 && st.st_size > 16) {
            FILE *f = fopen(m_disk_path.c_str(), "rb");
            if (f) {
                char magic[4];
                uint32_t size = 0;
                if (fread(magic, 1, 4, f) == 4 && !memcmp(magic, kPipelineMagic, 4) &&
                    fread(&size, 1, 4, f) == 4 && size > 0 && size < (64u << 20)) {
                    blob.resize(size);
                    if (fread(blob.data(), 1, size, f) != size) blob.clear();
                }
                fclose(f);
                if (!blob.empty()) NVR_INFO("pipeline cache em disco: %zu bytes", blob.size());
            }
        }
    }
    ci.initialDataSize = blob.size();
    ci.pInitialData = blob.data();
    if (vkCreatePipelineCache(core->device(), &ci, nullptr, &m_cache) != VK_SUCCESS) {
        // sem cache: cria vazio (o jogo funciona, so perde warm-up)
        ci.initialDataSize = 0;
        ci.pInitialData = nullptr;
        if (vkCreatePipelineCache(core->device(), &ci, nullptr, &m_cache) != VK_SUCCESS) {
            NVR_ERROR("vkCreatePipelineCache falhou");
            return false;
        }
    }

    bool async = Config::get().get_bool("async_pipeline", true);
    if (async && std::thread::hardware_concurrency() > 1) {
        m_quit = false;
        m_worker = std::thread([this] { worker_loop(); });
    }
    return true;
}

void PipelineCache::worker_loop() {
    while (true) {
        uint64_t key = 0;
        {
            std::unique_lock<std::mutex> l(m_wake_mutex);
            m_wake.wait(l, [&] { return m_quit || !m_queue.empty(); });
            if (m_quit && m_queue.empty()) return;
            key = m_queue.front();
            m_queue.pop_front();
        }
        std::function<VkPipeline(VkPipelineCache)> build;
        {
            std::lock_guard<std::mutex> l(m_mutex);
            if (m_map.count(key)) continue;
            auto it = m_builders.find(key);
            if (it == m_builders.end()) continue;
            build = it->second;
        }
        VkPipeline p = build(m_cache);
        if (p) {
            std::lock_guard<std::mutex> l(m_mutex);
            m_map[key] = p;
            m_creates++;
        }
    }
}

void PipelineCache::warm_async(uint64_t key, const std::function<VkPipeline(VkPipelineCache)> &build) {
    if (!m_worker.joinable()) return;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        if (m_map.count(key)) return;
        m_builders[key] = build;
    }
    std::lock_guard<std::mutex> l(m_wake_mutex);
    if (m_queue.size() < 64) m_queue.push_back(key);
    m_wake.notify_one();
}

VkPipeline PipelineCache::get_graphics(uint64_t key, const std::function<VkPipeline(VkPipelineCache)> &build) {
    {
        std::lock_guard<std::mutex> l(m_mutex);
        auto it = m_map.find(key);
        if (it != m_map.end()) { m_hits++; return it->second; }
        m_builders[key] = build;
    }
    VkPipeline p = build(m_cache);
    if (!p) return VK_NULL_HANDLE;
    {
        std::lock_guard<std::mutex> l(m_mutex);
        m_map[key] = p;
        m_creates++;
    }
    return p;
}

VkPipeline PipelineCache::get_compute(uint64_t key, const std::function<VkPipeline(VkPipelineCache)> &build) {
    return get_graphics(key, build);   // mesmo mapa; a chave diferencia o bind point
}

void PipelineCache::save_disk() {
    if (m_disk_path.empty() || !m_core) return;
    size_t size = 0;
    if (vkGetPipelineCacheData(m_core->device(), m_cache, &size, nullptr) != VK_SUCCESS || !size) return;
    std::vector<uint8_t> blob(size);
    if (vkGetPipelineCacheData(m_core->device(), m_cache, &size, blob.data()) != VK_SUCCESS) return;
    FILE *f = fopen(m_disk_path.c_str(), "wb");
    if (!f) return;
    fwrite(kPipelineMagic, 1, 4, f);
    uint32_t s32 = (uint32_t)size;
    fwrite(&s32, 1, 4, f);
    fwrite(blob.data(), 1, size, f);
    fclose(f);
}

void PipelineCache::shutdown() {
    if (m_quit.exchange(true)) return;
    if (m_worker.joinable()) {
        m_wake.notify_all();
        m_worker.join();
    }
    if (m_core) {
        save_disk();
        for (auto &kv : m_map) vkDestroyPipeline(m_core->device(), kv.second, nullptr);
        m_map.clear();
        if (m_cache) vkDestroyPipelineCache(m_core->device(), m_cache, nullptr);
        if (m_layout) vkDestroyPipelineLayout(m_core->device(), m_layout, nullptr);
        if (m_set_layout) vkDestroyDescriptorSetLayout(m_core->device(), m_set_layout, nullptr);
    }
    m_core = nullptr;
}

// ------------------------------------------------------------ DescriptorSets
void DescAllocator::init(VkCore *core, PipelineCache *pipelines) {
    m_core = core;
    m_pipe = pipelines;
}

void DescAllocator::shutdown() {
    // os sets pertencem aos pools do frame, destruidos pelo FrameCtx; aqui so
    // limpa o mapa (nenhum handle sobrevive ao shutdown).
    m_sets.clear();
    m_core = nullptr;
}

VkDescriptorSet DescAllocator::get(VkDescriptorPool pool, uint64_t tex_hash,
                                   const std::function<void(VkDescriptorSet)> &fill) {
    auto it = m_sets.find(tex_hash);
    if (it != m_sets.end()) return it->second;
    VkDescriptorSetLayout sl = m_pipe->set_layout();
    VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    ai.descriptorPool = pool;
    ai.descriptorSetCount = 1;
    ai.pSetLayouts = &sl;
    VkDescriptorSet set = VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(m_core->device(), &ai, &set) != VK_SUCCESS) {
        NVR_ERROR("vkAllocateDescriptorSets falhou (pool cheio?)");
        return VK_NULL_HANDLE;
    }
    fill(set);
    m_sets[tex_hash] = set;
    return set;
}

// ------------------------------------------------------------- Image utils
void image_barrier(VkCommandBuffer cb, VkImage img, VkImageAspectFlags aspect,
                   VkImageLayout from, VkImageLayout to, uint32_t base_level, uint32_t level_count,
                   uint32_t layer_count, VkAccessFlags src, VkAccessFlags dst,
                   uint32_t src_family, uint32_t dst_family) {
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = src_family;
    b.dstQueueFamilyIndex = dst_family;
    b.image = img;
    b.subresourceRange = {aspect, base_level, level_count, 0, layer_count};
    b.srcAccessMask = src;
    b.dstAccessMask = dst;
    VkPipelineStageFlags src_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkPipelineStageFlags dst_stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    vkCmdPipelineBarrier(cb, src_stage, dst_stage, 0, 0, nullptr, 0, nullptr, 1, &b);
}

// Gera mipmaps com vkCmdBlitImage (qualidade linear; funciona em todos os
// devices, incluindo mobile). Entrada: layout atual (tipicamente UNDEFINED
// apos o upload do nivel 0 ou SHADER_READ_ONLY); saida: SHADER_READ_ONLY_OPTIMAL
// para todos os niveis. As barreiras sao por faixa de mip (o layout e por
// imagem, mas cada faixa precisa da transicao certa para o blit).
void record_mipgen(VkCommandBuffer cb, VkCore *core, VkImage img, VkFormat fmt,
                   uint32_t width, uint32_t height, uint32_t depth_or_layers, bool is_3d,
                   uint32_t levels, VkImageLayout *inout_layout) {
    if (levels <= 1) return;
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(core->physical(), fmt, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) ||
        !(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT)) {
        NVR_WARN("mipgen: formato %d nao suporta blit; mips ficam sem filtro", (int)fmt);
        return;
    }
    uint32_t layers = depth_or_layers;
    // nivel 0: fonte do primeiro blit; niveis 1..n-1: destino dos blits
    image_barrier(cb, img, VK_IMAGE_ASPECT_COLOR_BIT, *inout_layout,
                  VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 1, layers,
                  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
    image_barrier(cb, img, VK_IMAGE_ASPECT_COLOR_BIT, *inout_layout,
                  VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, levels - 1, layers,
                  VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);

    for (uint32_t lvl = 1; lvl < levels; lvl++) {
        uint32_t sw = std::max(1u, width >> (lvl - 1)), sh = std::max(1u, height >> (lvl - 1));
        uint32_t dw = std::max(1u, width >> lvl), dh = std::max(1u, height >> lvl);
        uint32_t sd = is_3d ? std::max(1u, depth_or_layers >> (lvl - 1)) : depth_or_layers;
        uint32_t dd = is_3d ? std::max(1u, depth_or_layers >> lvl) : depth_or_layers;
        VkImageBlit blit{};
        blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, lvl - 1, 0, 1};
        blit.srcOffsets[1] = {(int32_t)sw, (int32_t)sh, (int32_t)sd};
        blit.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, lvl, 0, 1};
        blit.dstOffsets[1] = {(int32_t)dw, (int32_t)dh, (int32_t)dd};
        // 3D: um blit por fatia; caso contrario um por face (layers)
        if (is_3d) {
            uint32_t slices = std::max(1u, depth_or_layers >> (lvl - 1)) == sd ? sd : 1;
            (void)slices;
            vkCmdBlitImage(cb, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, img,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        } else {
            // faces/camadas: blit cobre todas de uma vez via layerCount
            blit.srcSubresource.layerCount = layers;
            blit.dstSubresource.layerCount = layers;
            vkCmdBlitImage(cb, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, img,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);
        }
        if (lvl + 1 < levels) {
            // nivel lvl vira fonte do proximo blit
            image_barrier(cb, img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, lvl, 1, layers,
                          VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        }
    }
    // transicao final para SHADER_READ_ONLY: nivel 0 vem de TRANSFER_SRC,
    // niveis > 0 vem de TRANSFER_DST.
    image_barrier(cb, img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 0, 1, layers,
                  VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT);
    image_barrier(cb, img, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                  VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, levels - 1, layers,
                  VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
    *inout_layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

// Copia dados lineares (pitch D3D) para um subresource via staging.
bool upload_image_data(VkCore *core, VkCommandBuffer cb, StagingRing &ring,
                       VkImage img, VkFormat fmt, uint32_t w, uint32_t h, uint32_t d,
                       uint32_t level, uint32_t layer,
                       const void *data, uint32_t src_row_pitch, uint32_t src_slice_pitch,
                       VkImageLayout *inout_layout) {
    const FormatInfo &fi = format_info_vk(fmt);   // fmt aqui e VkFormat
    if (fi.vk == VK_FORMAT_UNDEFINED) { NVR_WARN("upload: formato %u sem mapeamento", fmt); return false; }
    uint32_t lw = std::max(1u, w >> level), lh = std::max(1u, h >> level);
    uint32_t bw = (lw + fi.block_width - 1) / fi.block_width;
    uint32_t bh = (lh + fi.block_height - 1) / fi.block_height;
    uint32_t row_bytes = bw * fi.bytes_per_block;
    uint32_t depth = std::max(1u, (fi.depth ? 1u : (d >> level)));

    StagingRing::Alloc a;
    VkDeviceSize total = (VkDeviceSize)row_bytes * bh * depth;
    if (!ring.alloc(total, 16, &a)) {
        NVR_ERROR("staging ring sem espaco para %llu bytes (frame cheio)", (unsigned long long)total);
        return false;
    }
    // copia com repitch (d3d pitch -> compacto)
    uint8_t *dst = (uint8_t *)a.mapped;
    const uint8_t *src = (const uint8_t *)data;
    for (uint32_t z = 0; z < depth; z++)
        for (uint32_t y = 0; y < bh; y++)
            memcpy(dst + ((VkDeviceSize)z * bh + y) * row_bytes,
                   src + (VkDeviceSize)z * src_slice_pitch + (VkDeviceSize)y * src_row_pitch,
                   row_bytes);

    VkImageLayout &layout = *inout_layout;
    if (layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        image_barrier(cb, img, fi.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT,
                      layout, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, 1, 1,
                      VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_WRITE_BIT);
    }
    VkBufferImageCopy cpy{};
    cpy.bufferOffset = a.offset;
    cpy.bufferRowLength = 0;   // compacto
    cpy.bufferImageHeight = 0;
    cpy.imageSubresource = {fi.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT, level, layer, 1};
    cpy.imageOffset = {0, 0, 0};
    cpy.imageExtent = {lw, lh, depth};
    vkCmdCopyBufferToImage(cb, a.buffer, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &cpy);
    layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    return true;
}

bool readback_image(VkCore *core, VkImage img, VkFormat fmt, uint32_t w, uint32_t h,
                    uint32_t level, uint32_t layer, VkImageLayout layout,
                    void *dst, uint32_t dst_row_pitch, bool flip_y) {
    const FormatInfo &fi = format_info_vk(fmt);   // fmt aqui e VkFormat
    if (fi.vk == VK_FORMAT_UNDEFINED) return false;
    uint32_t lw = std::max(1u, w >> level), lh = std::max(1u, h >> level);
    uint32_t bw = (lw + fi.block_width - 1) / fi.block_width;
    uint32_t bh = (lh + fi.block_height - 1) / fi.block_height;
    uint32_t row_bytes = bw * fi.bytes_per_block;
    VkDeviceSize buf_size = (VkDeviceSize)row_bytes * bh;

    // buffer host-visible dedicado (leitura esporadica)
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = buf_size;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    VkBuffer buf = VK_NULL_HANDLE;
    VmaAllocation alloc = mem_alloc_buffer(core, bci, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, &buf);
    if (!alloc) return false;

    // comando unico: transicao + copy + transicao de volta
    VkCommandPool pool;
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.queueFamilyIndex = core->graphics_queue().family;
    vkCreateCommandPool(core->device(), &pci, nullptr, &pool);
    VkCommandBuffer cb;
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    vkAllocateCommandBuffers(core->device(), &cai, &cb);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cb, &bi);
    VkImageAspectFlags aspect = fi.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    image_barrier(cb, img, aspect, layout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0, 1, 1,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                      VK_ACCESS_SHADER_READ_BIT,
                  VK_ACCESS_TRANSFER_READ_BIT);
    VkBufferImageCopy cpy{};
    cpy.bufferOffset = 0;
    cpy.imageSubresource = {aspect, level, layer, 1};
    cpy.imageExtent = {lw, lh, 1};
    vkCmdCopyImageToBuffer(cb, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buf, 1, &cpy);
    image_barrier(cb, img, aspect, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, layout, 0, 1, 1,
                  VK_ACCESS_TRANSFER_READ_BIT,
                  VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
                      VK_ACCESS_SHADER_READ_BIT);
    vkEndCommandBuffer(cb);
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    VkFence fence;
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    vkCreateFence(core->device(), &fci, nullptr, &fence);
    vkQueueSubmit(core->graphics_queue().queue, 1, &si, fence);
    vkWaitForFences(core->device(), 1, &fence, VK_TRUE, UINT64_MAX);
    vkDestroyFence(core->device(), fence, nullptr);
    vkFreeCommandBuffers(core->device(), pool, 1, &cb);
    vkDestroyCommandPool(core->device(), pool, nullptr);

    void *mapped = nullptr;
    mem_map(core, alloc, &mapped);
    if (!mapped) { mem_free(core, alloc); vkDestroyBuffer(core->device(), buf, nullptr); return false; }
    uint8_t *d = (uint8_t *)dst;
    for (uint32_t y = 0; y < bh; y++) {
        uint32_t sy = flip_y ? (bh - 1 - y) : y;
        memcpy(d + (VkDeviceSize)y * dst_row_pitch, (const uint8_t *)mapped + (VkDeviceSize)sy * row_bytes, row_bytes);
    }
    mem_unmap(core, alloc);
    mem_free(core, alloc);
    vkDestroyBuffer(core->device(), buf, nullptr);
    return true;
}

} // namespace nvr
