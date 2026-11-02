// Objetos GPU do device: cache de render passes, cache de pipelines
// (com VkPipelineCache persistente em disco + worker assincrono), samplers,
// descriptor sets e o pipeline de blit/gamma do Present.
#pragma once
#include "nvr_vk.h"
#include <thread>
#include <condition_variable>
#include <deque>

namespace nvr {

// ------------------------------------------------------------ RenderPass
struct PassKey {
    uint32_t color_fmt;      // VkFormat
    uint32_t ds_fmt;         // VkFormat ou 0
    uint32_t color_load;     // VkAttachmentLoadOp
    uint32_t color_store;
    uint32_t ds_load;
    uint32_t ds_store;
    uint32_t sample_count;
    uint64_t hash() const {
        uint32_t h = fnv1a(this, sizeof *this);
        return ((uint64_t)h << 32) | fnv1a(this, sizeof *this, 0x9e3779b9u);
    }
    bool operator==(const PassKey &o) const { return !memcmp(this, &o, sizeof *this); }
};

class PassCache {
public:
    void init(VkCore *core) { m_core = core; }
    void shutdown();
    VkRenderPass get(const PassKey &key);
private:
    VkCore *m_core = nullptr;
    std::unordered_map<uint64_t, VkRenderPass> m_map;
    std::unordered_map<uint64_t, PassKey> m_keys;
};

// ------------------------------------------------------------ SamplerCache
struct SamplerKey {
    uint32_t packed;   // (min|mip|mag|u|v|w|anis|lod_bias_idx|cmp?)
    bool operator==(const SamplerKey &o) const { return packed == o.packed; }
};
struct SamplerKeyHash { uint64_t operator()(const SamplerKey &k) const { return k.packed; } };

class SamplerCache {
public:
    void init(VkCore *core) { m_core = core; }
    void shutdown();
    VkSampler get(uint32_t packed);
private:
    VkCore *m_core = nullptr;
    std::unordered_map<uint32_t, VkSampler> m_map;
};

// ---------------------------------------------------------- PipelineCache
// Layout unico para todos os pipelines: os numeros de binding (kBind*) vivem
// em nvr_common.h porque o emissor SPIR-V (shader/) tambem os usa.

class PipelineCache {
public:
    bool init(VkCore *core, const char *disk_dir);
    void shutdown();

    VkPipelineLayout layout() const { return m_layout; }
    VkDescriptorSetLayout set_layout() const { return m_set_layout; }

    // Cria ou busca pipeline grafico. Bloqueia na primeira criacao (como um
    // driver real); o worker assincrono so PRE-compila chaves conhecidas.
    VkPipeline get_graphics(uint64_t key_hash, const std::function<VkPipeline(VkPipelineCache)> &build);
    VkPipeline get_compute(uint64_t key_hash, const std::function<VkPipeline(VkPipelineCache)> &build);

    // Prediccao: aquece no worker as chaves usadas no frame anterior.
    void warm_async(uint64_t key_hash, const std::function<VkPipeline(VkPipelineCache)> &build);
    void save_disk();      // chamado de tempos em tempos e no shutdown

    uint64_t stat_hits() const { return m_hits; }
    uint64_t stat_creates() const { return m_creates; }

private:
    void worker_loop();
    VkCore *m_core = nullptr;
    VkPipelineCache m_cache = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_set_layout = VK_NULL_HANDLE;
    std::mutex m_mutex;
    std::unordered_map<uint64_t, VkPipeline> m_map;
    std::unordered_map<uint64_t, std::function<VkPipeline(VkPipelineCache)>> m_builders;
    std::atomic<uint64_t> m_hits{0}, m_creates{0};
    // worker
    std::thread m_worker;
    std::mutex m_wake_mutex;
    std::condition_variable m_wake;
    std::deque<uint64_t> m_queue;
    std::atomic<bool> m_quit{false};
    std::string m_disk_dir;
    std::string m_disk_path;
};

// --------------------------------------------------------- FramebufferCache
// Framebuffers por (view rt, view ds, render pass). As views pertencem aos
// objetos de recurso; a chave e por ponteiro de view (estavel por objeto).
struct SurfaceKey {
    void *surf = nullptr;
    uint32_t level = 0, layer = 0;
    bool operator==(const SurfaceKey &o) const { return surf == o.surf && level == o.level && layer == o.layer; }
};
struct SurfaceKeyHash {
    uint64_t operator()(const SurfaceKey &k) const {
        return fnv1a_mix(fnv1a_mix((uint32_t)(uintptr_t)k.surf, k.level * 2654435761u), k.layer);
    }
};
class FramebufferCache {
public:
    void init(VkCore *core) { m_core = core; }
    void shutdown();
    // forma generica (usada pelo blit do present, sem depender de NrSurface)
    VkFramebuffer get_by_view(VkImageView rt_view, VkImageView ds_view, VkRenderPass rp,
                              uint32_t w, uint32_t h);
    // forma da camada d3d9 (NrSurface de RT/DS); keys = ponteiros dos wrappers
    VkFramebuffer get(void *rt_surf, void *ds_surf, VkRenderPass rp, VkCore &vk);
    void forget_surface(void *surf);   // surf destruido: limpa framebuffers
private:
    VkFramebuffer create(VkImageView rt, VkImageView ds, VkRenderPass rp, uint32_t w, uint32_t h);
    struct Entry { VkFramebuffer fb; void *rt, *ds; };
    VkCore *m_core = nullptr;
    std::unordered_map<uint64_t, Entry> m_map;
    std::vector<std::pair<uint64_t, void *>> m_owners;
};

// ------------------------------------------------------------ DescriptorSets
// Um cache por frame: chave = 16 texturas + 16 samplers (handles). O dono
// (NrDevice) chama clear_frame() quando o pool do frame e destruido.
class DescAllocator {
public:
    void init(VkCore *core, PipelineCache *pipelines);
    void shutdown();
    void clear_frame() { m_sets.clear(); }   // sets morrem com o pool do frame
    // aloca/pega do cache do frame; o pool pertence ao frame (reset em conjunto)
    VkDescriptorSet get(VkDescriptorPool pool, uint64_t tex_hash,
                        const std::function<void(VkDescriptorSet)> &fill);
private:
    VkCore *m_core = nullptr;
    PipelineCache *m_pipe = nullptr;
    std::unordered_map<uint64_t, VkDescriptorSet> m_sets;   // limpo por frame
};

// ------------------------------------------------------------- Image utils
void image_barrier(VkCommandBuffer cb, VkImage img, VkImageAspectFlags aspect,
                   VkImageLayout from, VkImageLayout to,
                   uint32_t base_level, uint32_t level_count, uint32_t layer_count,
                   VkAccessFlags src = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                   VkAccessFlags dst = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT,
                   uint32_t src_family = VK_QUEUE_FAMILY_IGNORED,
                   uint32_t dst_family = VK_QUEUE_FAMILY_IGNORED);

void record_mipgen(VkCommandBuffer cb, VkCore *core, VkImage img, VkFormat fmt,
                   uint32_t width, uint32_t height, uint32_t depth_or_layers,
                   bool is_3d, uint32_t levels, VkImageLayout *inout_layout);

// Converte e copia um retangulo de memoria linear (d3d pitch) para um image
// subresource via staging ring (formato final = VkFormat destino).
bool upload_image_data(VkCore *core, VkCommandBuffer cb, StagingRing &ring,
                       VkImage img, VkFormat fmt, uint32_t w, uint32_t h, uint32_t d,
                       uint32_t level, uint32_t layer,
                       const void *data, uint32_t src_row_pitch, uint32_t src_slice_pitch,
                       VkImageLayout *inout_layout);

// Le um subresource para memoria linear (flip vertical opcional).
bool readback_image(VkCore *core, VkImage img, VkFormat fmt, uint32_t w, uint32_t h,
                    uint32_t level, uint32_t layer, VkImageLayout layout,
                    void *dst, uint32_t dst_row_pitch, bool flip_y);

} // namespace nvr
