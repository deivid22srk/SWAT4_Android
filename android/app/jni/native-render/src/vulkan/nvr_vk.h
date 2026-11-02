// Backend Vulkan do native-render: core de device, frames in flight,
// swapchain, staging ring, caches de pipeline e alocacao via VMA.
//
// Decisoes (ver docs/native-render/ANALISE.md):
//  - Linka contra libvulkan direto (igual ao dxvk no NDK); SDL2 so cria a
//    VkSurfaceKHR (mesma via que o dxvk usa no backend wsi/sdl2).
//  - Vulkan 1.1 baseline; extensions opcionais ativadas por consulta.
//  - Render passes (nao dynamic rendering): com LOAD/STORE ops escolhidos
//    por estado real do frame, adequados a GPUs tile-based.
//  - 2 frames in flight (config), staging ring persistente por frame.
//  - Fluxo do frame: os comandos do jogo sao gravados no CB do frame
//    conforme chegam; o Present adquire a imagem da swapchain, grava o blit
//    final (escala + gamma), submete UMA vez e faz present. Get* / queries
//    com leitura fazem um submit parcial e esperam (caminho raro).
#pragma once
#include "../nvr_common.h"
#include "../nvr_format.h"
#include <vulkan/vulkan.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_vulkan.h>
#include "vk_mem_alloc.h"

namespace nvr {

// ---------------------------------------------------------------- VkCore
struct QueuePair {
    uint32_t family = 0;
    VkQueue queue = VK_NULL_HANDLE;
};

class VkCore {
public:
    bool init(bool want_validation);
    void shutdown();

    VkInstance instance() const { return m_instance; }
    VkPhysicalDevice physical() const { return m_pdev; }
    VkDevice device() const { return m_dev; }
    VmaAllocator allocator() const { return m_vma; }
    const QueuePair &graphics_queue() const { return m_gq; }
    uint32_t present_family(SDL_Window *win);   // consulta (e memoiza) a fila de present
    VkQueue present_queue(SDL_Window *win);
    const VkPhysicalDeviceLimits &limits() const { return m_limits; }
    const VkPhysicalDeviceProperties &props() const { return m_props; }

    // suporte consultado no boot
    bool has_bc() const { return m_has_bc; }
    bool has_astc() const { return m_has_astc; }
    bool has_vertex_stores() const { return m_has_vertex_stores; }
    uint32_t max_sample_counts_color() const { return m_ms_color; }
    uint32_t max_sample_counts_depth() const { return m_ms_depth; }

private:
    bool pick_physical();
    bool create_logical(bool want_validation);

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_messenger = VK_NULL_HANDLE;
    VkPhysicalDevice m_pdev = VK_NULL_HANDLE;
    VkDevice m_dev = VK_NULL_HANDLE;
    VmaAllocator m_vma = VK_NULL_HANDLE;
    QueuePair m_gq;
    uint32_t m_pq_family = 0xFFFFFFFFu;
    VkQueue m_pq = VK_NULL_HANDLE;
    VkPhysicalDeviceLimits m_limits{};
    VkPhysicalDeviceProperties m_props{};
    std::vector<const char *> m_dev_exts;
    bool m_has_bc = false;
    bool m_has_astc = false;
    bool m_has_vertex_stores = false;
    uint32_t m_ms_color = 1, m_ms_depth = 1;
};

// ------------------------------------------------------------- StagingRing
class StagingRing {
public:
    struct Alloc { VkBuffer buffer; VkDeviceSize offset; VkDeviceSize size; void *mapped; };
    bool init(VkCore *core, VkDeviceSize bytes);
    void shutdown();
    bool alloc(VkDeviceSize size, VkDeviceSize align, Alloc *out);
    void reset();
private:
    VkCore *m_core = nullptr;
    VkBuffer m_buffer = VK_NULL_HANDLE;
    VmaAllocation m_alloc = VK_NULL_HANDLE;
    void *m_mapped = nullptr;
    VkDeviceSize m_size = 0, m_head = 0;
};

// ---------------------------------------------------------------- FrameCtx
class FrameCtx {
public:
    bool init(VkCore *core, uint32_t index);
    void shutdown();
    void wait_fence();
    void reset_pool();                       // apos wait_fence
    VkCommandBuffer begin();                 // reset+begin do CB
    void end();                              // fecha (submetido pelo device)
    VkCommandBuffer cb() const { return m_cb; }
    StagingRing &ring() { return m_ring; }
    VkSemaphore render_done() const { return m_render_done; }
    VkSemaphore acquire_sem() const { return m_acquire; }
    VkFence fence_handle() const { return m_fence; }
    VkDescriptorPool desc_pool();
    uint32_t index() const { return m_index; }
private:
    VkCore *m_core = nullptr;
    uint32_t m_index = 0;
    VkCommandPool m_pool = VK_NULL_HANDLE;
    VkCommandBuffer m_cb = VK_NULL_HANDLE;
    VkFence m_fence = VK_NULL_HANDLE;
    VkSemaphore m_render_done = VK_NULL_HANDLE;
    VkSemaphore m_acquire = VK_NULL_HANDLE;
    StagingRing m_ring;
    std::vector<VkDescriptorPool> m_desc_pools;
};

// --------------------------------------------------------------- Swapchain
class Swapchain {
public:
    bool init(VkCore *core, SDL_Window *window);
    void shutdown();
    bool recreate();                          // resize/rotacao/suboptimal
    uint32_t image_count() const { return (uint32_t)m_views.size(); }
    VkImageView view(uint32_t i) const { return m_views[i]; }
    VkImage image(uint32_t i) const { return m_images[i]; }
    VkFormat format() const { return m_format; }
    VkExtent2D extent() const { return m_extent; }
    VkSurfaceTransformFlagBitsKHR transform() const { return m_transform; }
    VkSwapchainKHR handle() const { return m_sc; }
    VkSurfaceKHR surface() const { return m_surface; }
    uint32_t generation() const { return m_gen; }   // incrementa a cada recreate
    // Acquire: retorna image index; em caso de OUT_OF_DATE recria e tenta
    // de novo (max 2x). result = VK_SUCCESS ou erro real. O semaforo (do frame
    // corrente) e sinalizado pelo acquire e esperado no submit do frame.
    VkResult acquire(VkSemaphore sem, uint32_t *image_out);
    VkResult present(VkQueue q, VkSemaphore wait, uint32_t image_index);
    bool valid() const { return m_sc != VK_NULL_HANDLE; }
private:
    bool create_surface();
    void destroy_swapchain_only();
    VkCore *m_core = nullptr;
    SDL_Window *m_window = nullptr;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSwapchainKHR m_sc = VK_NULL_HANDLE;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_views;
    VkFormat m_format = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D m_extent{};
    VkSurfaceTransformFlagBitsKHR m_transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    uint32_t m_min_images = 3;
    uint32_t m_gen = 1;
};

// ------------------------------------------------------- helpers de memoria
VmaAllocation mem_alloc_image(VkCore *core, const VkImageCreateInfo &ici, VkImage *img);
VmaAllocation mem_alloc_buffer(VkCore *core, const VkBufferCreateInfo &bci,
                               VmaMemoryUsage usage, VkBuffer *buf);
void mem_free(VkCore *core, VmaAllocation a);
void mem_free_image(VkCore *core, VkImage img, VmaAllocation a);
void mem_map(VkCore *core, VmaAllocation a, void **out);
void mem_unmap(VkCore *core, VmaAllocation a);
void mem_name(VkCore *core, VmaAllocation a, const char *name);

} // namespace nvr
