#include "nvr_vk.h"

namespace nvr {

static VKAPI_ATTR VkBool32 VKAPI_CALL vk_debug_cb(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT *data, void *) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
        NVR_ERROR("vulkan: %s", data->pMessage ? data->pMessage : "(sem mensagem)");
    else if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
        NVR_WARN("vulkan: %s", data->pMessage ? data->pMessage : "(sem mensagem)");
    else
        NVR_INFO("vulkan: %s", data->pMessage ? data->pMessage : "(sem mensagem)");
    return VK_FALSE;
}

bool VkCore::init(bool want_validation) {
    // SDL ja carregou a libvulkan (platform_init chama SDL_Vulkan_LoadLibrary);
    // por seguranca, tente de novo (no-op se ja carregada).
    if (!SDL_Vulkan_LoadLibrary(nullptr))
        NVR_WARN("SDL_Vulkan_LoadLibrary falhou: %s", SDL_GetError());

    uint32_t next_count = 0;
    SDL_Vulkan_GetInstanceExtensions(nullptr, &next_count, nullptr);
    std::vector<const char *> exts(next_count + 2);
    SDL_Vulkan_GetInstanceExtensions(nullptr, &next_count, exts.data());
    exts.resize(next_count);

    VkApplicationInfo ai{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    ai.pApplicationName = "SWAT4 native-render";
    ai.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.pEngineName = "native-render";
    ai.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    ai.apiVersion = VK_API_VERSION_1_1;   // baseline do projeto (Android 9)

    std::vector<const char *> layers;
    if (want_validation) {
        uint32_t nl = 0;
        vkEnumerateInstanceLayerProperties(&nl, nullptr);
        std::vector<VkLayerProperties> avail(nl);
        vkEnumerateInstanceLayerProperties(&nl, avail.data());
        for (auto &l : avail)
            if (!strcmp(l.layerName, "VK_LAYER_KHRONOS_validation")) {
                layers.push_back("VK_LAYER_KHRONOS_validation");
                break;
            }
        if (layers.empty()) NVR_WARN("validacao pedida mas VK_LAYER_KHRONOS_validation ausente");
        // extensao de debug utils (para o callback acima)
        uint32_t ne = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &ne, nullptr);
        std::vector<VkExtensionProperties> ae(ne);
        vkEnumerateInstanceExtensionProperties(nullptr, &ne, ae.data());
        for (auto &e : ae)
            if (!strcmp(e.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
                exts.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
                break;
            }
    }

    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &ai;
    ici.enabledExtensionCount = (uint32_t)exts.size();
    ici.ppEnabledExtensionNames = exts.data();
    ici.enabledLayerCount = (uint32_t)layers.size();
    ici.ppEnabledLayerNames = layers.data();
    if (vkCreateInstance(&ici, nullptr, &m_instance) != VK_SUCCESS) {
        NVR_ERROR("vkCreateInstance falhou (extensoes SDL: %zu, layers: %zu)", exts.size(), layers.size());
        return false;
    }
    NVR_INFO("VkInstance ok (%zu extensoes, %zu layers)", exts.size(), layers.size());

    if (want_validation && !layers.empty()) {
        VkDebugUtilsMessengerCreateInfoEXT di{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        di.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                             VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        di.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                         VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        di.pfnUserCallback = vk_debug_cb;
        auto fn = (PFN_vkCreateDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT");
        if (fn) fn(m_instance, &di, nullptr, &m_messenger);
    }

    if (!pick_physical()) return false;
    if (!create_logical(want_validation)) return false;

    VmaAllocatorCreateInfo vci{};
    vci.physicalDevice = m_pdev;
    vci.device = m_dev;
    vci.instance = m_instance;
    vci.vulkanApiVersion = VK_API_VERSION_1_1;
    if (vmaCreateAllocator(&vci, &m_vma) != VK_SUCCESS) {
        NVR_ERROR("vmaCreateAllocator falhou");
        return false;
    }
    NVR_INFO("VMA ok; GPU: %s (vendor %04x, driver %u)", m_props.deviceName,
             m_props.vendorID, m_props.driverVersion);
    return true;
}

bool VkCore::pick_physical() {
    uint32_t n = 0;
    if (vkEnumeratePhysicalDevices(m_instance, &n, nullptr) != VK_SUCCESS || !n) {
        NVR_ERROR("nenhum physical device Vulkan");
        return false;
    }
    std::vector<VkPhysicalDevice> devs(n);
    if (vkEnumeratePhysicalDevices(m_instance, &n, devs.data()) != VK_SUCCESS) return false;
    m_pdev = devs[0];
    // preferencia: discrete > integrated > outro (mobile tem 1; desktop test usa lavapipe)
    int best = -1;
    for (auto d : devs) {
        VkPhysicalDeviceProperties p{};
        vkGetPhysicalDeviceProperties(d, &p);
        int score = 0;
        if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score = 3;
        else if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU) score = 2;
        else score = 1;
        if (score > best) { best = score; m_pdev = d; }
    }
    vkGetPhysicalDeviceProperties(m_pdev, &m_props);
    VkPhysicalDeviceFeatures feat{};
    vkGetPhysicalDeviceFeatures(m_pdev, &feat);
    VkPhysicalDeviceMemoryProperties mem{};
    vkGetPhysicalDeviceMemoryProperties(m_pdev, &mem);

    // formatos comprimidos
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(m_pdev, VK_FORMAT_BC1_RGBA_UNORM_BLOCK, &fp);
    m_has_bc = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0 &&
               feat.textureCompressionBC;
    vkGetPhysicalDeviceFormatProperties(m_pdev, VK_FORMAT_ASTC_4x4_UNORM_BLOCK, &fp);
    m_has_astc = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0 &&
                 feat.textureCompressionASTC_LDR;
    m_has_vertex_stores = feat.vertexPipelineStoresAndAtomics;

    // sample counts para color e depth
    VkImageFormatProperties ip{};
    if (vkGetPhysicalDeviceImageFormatProperties(m_pdev, VK_FORMAT_B8G8R8A8_UNORM, VK_IMAGE_TYPE_2D,
                                                 VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
                                                 0, &ip) == VK_SUCCESS)
        m_ms_color = ip.sampleCounts;
    if (vkGetPhysicalDeviceImageFormatProperties(m_pdev, VK_FORMAT_D24_UNORM_S8_UINT, VK_IMAGE_TYPE_2D,
                                                 VK_IMAGE_TILING_OPTIMAL,
                                                 VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, 0, &ip) == VK_SUCCESS)
        m_ms_depth = ip.sampleCounts;
    else
        m_ms_depth = 1;

    NVR_INFO("GPU: %s | BC:%d ASTC:%d vtxStores:%d | heaps:%u", m_props.deviceName,
             (int)m_has_bc, (int)m_has_astc, (int)m_has_vertex_stores, mem.memoryHeapCount);
    return true;
}

bool VkCore::create_logical(bool want_validation) {
    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_pdev, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(m_pdev, &qn, qf.data());
    int gfx = -1;
    for (uint32_t i = 0; i < qn; i++)
        if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { gfx = (int)i; break; }
    if (gfx < 0) { NVR_ERROR("sem fila com GRAPHICS"); return false; }
    m_gq.family = (uint32_t)gfx;

    float prio = 1.0f;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    qci.queueFamilyIndex = m_gq.family;
    qci.queueCount = 1;
    qci.pQueuePriorities = &prio;

    m_dev_exts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    // portabilidade: alguns drivers Android antigos exigem nada alem de swapchain
    VkPhysicalDeviceFeatures feat{};
    vkGetPhysicalDeviceFeatures(m_pdev, &feat);
    VkPhysicalDeviceFeatures enabled{};
    if (feat.samplerAnisotropy) enabled.samplerAnisotropy = VK_TRUE;
    if (feat.textureCompressionBC) enabled.textureCompressionBC = VK_TRUE;
    if (feat.textureCompressionASTC_LDR) enabled.textureCompressionASTC_LDR = VK_TRUE;
    if (feat.vertexPipelineStoresAndAtomics) enabled.vertexPipelineStoresAndAtomics = VK_TRUE;
    if (feat.fragmentStoresAndAtomics) enabled.fragmentStoresAndAtomics = VK_TRUE;
    if (feat.fullDrawIndexUint32) enabled.fullDrawIndexUint32 = VK_TRUE;
    if (feat.fillModeNonSolid) enabled.fillModeNonSolid = VK_TRUE;
    if (feat.depthClamp) enabled.depthClamp = VK_TRUE;
    if (feat.independentBlend) enabled.independentBlend = VK_TRUE;
    VkPhysicalDeviceFeatures2 f2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    f2.features = enabled;

    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    dci.pNext = &f2;
    dci.queueCreateInfoCount = 1;
    dci.pQueueCreateInfos = &qci;
    dci.enabledExtensionCount = (uint32_t)m_dev_exts.size();
    dci.ppEnabledExtensionNames = m_dev_exts.data();
    dci.pEnabledFeatures = nullptr;   // via pNext (features2)
    if (vkCreateDevice(m_pdev, &dci, nullptr, &m_dev) != VK_SUCCESS) {
        NVR_ERROR("vkCreateDevice falhou");
        return false;
    }
    vkGetDeviceQueue(m_dev, m_gq.family, 0, &m_gq.queue);
    vkGetPhysicalDeviceProperties(m_pdev, &m_props);
    VkPhysicalDeviceProperties2 p2{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
    p2.properties = m_props;
    vkGetPhysicalDeviceProperties2(m_pdev, &p2);
    m_props = p2.properties;
    m_limits = m_props.limits;
    (void)want_validation;
    return true;
}

uint32_t VkCore::present_family(SDL_Window *win) {
    if (m_pq_family != 0xFFFFFFFFu) return m_pq_family;
    VkSurfaceKHR surf = VK_NULL_HANDLE;
    if (!SDL_Vulkan_CreateSurface(win, m_instance, &surf)) {
        NVR_ERROR("SDL_Vulkan_CreateSurface falhou: %s", SDL_GetError());
        return m_gq.family;   // melhor esforco; o create device vai falhar depois
    }
    uint32_t qn = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_pdev, &qn, nullptr);
    std::vector<VkQueueFamilyProperties> qf(qn);
    vkGetPhysicalDeviceQueueFamilyProperties(m_pdev, &qn, qf.data());
    m_pq_family = m_gq.family;
    for (uint32_t i = 0; i < qn; i++) {
        VkBool32 ok = VK_FALSE;
        if (vkGetPhysicalDeviceSurfaceSupportKHR(m_pdev, i, surf, &ok) == VK_SUCCESS && ok &&
            (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)) {
            m_pq_family = i;
            break;
        }
    }
    vkDestroySurfaceKHR(m_instance, surf, nullptr);
    return m_pq_family;
}

VkQueue VkCore::present_queue(SDL_Window *win) {
    present_family(win);
    if (m_pq == VK_NULL_HANDLE) vkGetDeviceQueue(m_dev, m_pq_family, 0, &m_pq);
    return m_pq;
}

void VkCore::shutdown() {
    if (m_vma) { vmaDestroyAllocator(m_vma); m_vma = VK_NULL_HANDLE; }
    if (m_dev) { vkDestroyDevice(m_dev, nullptr); m_dev = VK_NULL_HANDLE; }
    if (m_messenger) {
        auto fn = (PFN_vkDestroyDebugUtilsMessengerEXT)vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT");
        if (fn) fn(m_instance, m_messenger, nullptr);
        m_messenger = VK_NULL_HANDLE;
    }
    if (m_instance) { vkDestroyInstance(m_instance, nullptr); m_instance = VK_NULL_HANDLE; }
}

// ------------------------------------------------------------- StagingRing
bool StagingRing::init(VkCore *core, VkDeviceSize bytes) {
    m_core = core;
    m_size = bytes;
    m_head = 0;
    VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = bytes;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    m_alloc = mem_alloc_buffer(core, bci, VMA_MEMORY_USAGE_AUTO_PREFER_HOST, &m_buffer);
    if (!m_alloc) return false;
    mem_map(core, m_alloc, &m_mapped);
    return m_mapped != nullptr;
}

void StagingRing::shutdown() {
    if (!m_core) return;
    if (m_buffer) { vkDestroyBuffer(m_core->device(), m_buffer, nullptr); m_buffer = VK_NULL_HANDLE; }
    if (m_alloc) { mem_free(m_core, m_alloc); m_alloc = VK_NULL_HANDLE; }
    m_core = nullptr;
}

bool StagingRing::alloc(VkDeviceSize size, VkDeviceSize align, Alloc *out) {
    if (!size) return false;
    VkDeviceSize a = (align ? align : 1);
    VkDeviceSize head = (m_head + a - 1) / a * a;
    if (head + size > m_size) return false;   // frame cheio: caller deve aguardar/frame novo
    out->buffer = m_buffer;
    out->offset = head;
    out->size = size;
    out->mapped = (char *)m_mapped + head;
    m_head = head + size;
    return true;
}

void StagingRing::reset() { m_head = 0; }

// ---------------------------------------------------------------- FrameCtx
bool FrameCtx::init(VkCore *core, uint32_t index) {
    m_core = core;
    m_index = index;
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pci.queueFamilyIndex = core->graphics_queue().family;
    if (vkCreateCommandPool(core->device(), &pci, nullptr, &m_pool) != VK_SUCCESS) return false;
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = m_pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(core->device(), &cai, &m_cb) != VK_SUCCESS) return false;
    VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;   // primeiro frame nao espera
    if (vkCreateFence(core->device(), &fci, nullptr, &m_fence) != VK_SUCCESS) return false;
    VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    if (vkCreateSemaphore(core->device(), &sci, nullptr, &m_render_done) != VK_SUCCESS) return false;
    if (vkCreateSemaphore(core->device(), &sci, nullptr, &m_acquire) != VK_SUCCESS) return false;
    VkDeviceSize ring = (VkDeviceSize)Config::get().get_int("staging_mb", 24) * 1024 * 1024;
    if (!m_ring.init(core, ring)) { NVR_ERROR("staging ring %llu MB falhou", (unsigned long long)(ring >> 20)); return false; }
    return true;
}

void FrameCtx::shutdown() {
    VkDevice dev = m_core ? m_core->device() : VK_NULL_HANDLE;
    if (!dev) return;
    if (m_acquire) { vkDestroySemaphore(dev, m_acquire, nullptr); m_acquire = VK_NULL_HANDLE; }
    if (m_render_done) { vkDestroySemaphore(dev, m_render_done, nullptr); m_render_done = VK_NULL_HANDLE; }
    if (m_fence) { vkDestroyFence(dev, m_fence, nullptr); m_fence = VK_NULL_HANDLE; }
    for (auto p : m_desc_pools) vkDestroyDescriptorPool(dev, p, nullptr);
    m_desc_pools.clear();
    m_ring.shutdown();
    if (m_pool) { vkDestroyCommandPool(dev, m_pool, nullptr); m_pool = VK_NULL_HANDLE; }
    m_cb = VK_NULL_HANDLE;
}

void FrameCtx::wait_fence() {
    if (m_fence) vkWaitForFences(m_core->device(), 1, &m_fence, VK_TRUE, UINT64_MAX);
}

void FrameCtx::reset_pool() {
    if (m_fence) vkResetFences(m_core->device(), 1, &m_fence);
    m_ring.reset();
    // desc pools do frame anterior sao destruidos (sets do frame morrem juntos)
    for (auto p : m_desc_pools) vkDestroyDescriptorPool(m_core->device(), p, nullptr);
    m_desc_pools.clear();
}

VkCommandBuffer FrameCtx::begin() {
    vkResetCommandBuffer(m_cb, 0);
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(m_cb, &bi);
    return m_cb;
}

void FrameCtx::end() { vkEndCommandBuffer(m_cb); }

VkDescriptorPool FrameCtx::desc_pool() {
    // cria um pool novo quando o anterior encheu (sets de um frame morrem juntos)
    VkDescriptorPoolSize sizes[2] = {
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 256},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 256 * 18},
    };
    VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    pi.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    pi.maxSets = 256;
    pi.poolSizeCount = 2;
    pi.pPoolSizes = sizes;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(m_core->device(), &pi, nullptr, &pool) == VK_SUCCESS)
        m_desc_pools.push_back(pool);
    return pool;
}

// --------------------------------------------------------------- Swapchain
bool Swapchain::init(VkCore *core, SDL_Window *window) {
    m_core = core;
    m_window = window;
    if (!create_surface()) return false;
    return recreate();
}

bool Swapchain::create_surface() {
    if (m_surface) return true;
    if (!SDL_Vulkan_CreateSurface(m_window, m_core->instance(), &m_surface)) {
        NVR_ERROR("SDL_Vulkan_CreateSurface falhou: %s", SDL_GetError());
        return false;
    }
    return true;
}

void Swapchain::destroy_swapchain_only() {
    VkDevice dev = m_core->device();
    for (auto v : m_views) vkDestroyImageView(dev, v, nullptr);
    m_views.clear();
    m_images.clear();
    if (m_sc) { vkDestroySwapchainKHR(dev, m_sc, nullptr); m_sc = VK_NULL_HANDLE; }
}

void Swapchain::shutdown() {
    if (!m_core) return;
    destroy_swapchain_only();
    if (m_surface) { vkDestroySurfaceKHR(m_core->instance(), m_surface, nullptr); m_surface = VK_NULL_HANDLE; }
}

bool Swapchain::recreate() {
    if (!create_surface()) return false;
    VkPhysicalDevice pdev = m_core->physical();
    VkSurfaceCapabilitiesKHR caps{};
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(pdev, m_surface, &caps) != VK_SUCCESS) {
        NVR_WARN("SurfaceCapabilities falhou no recreate");
        return false;
    }
    m_format = choose_swapchain_format(pdev, m_surface);
    m_transform = caps.currentTransform;

    uint32_t want = caps.minImageCount + 1;
    if (caps.maxImageCount && want > caps.maxImageCount) want = caps.maxImageCount;
    m_min_images = want;

    VkExtent2D ext = caps.currentExtent;
    if (ext.width == 0xFFFFFFFFu) {
        int w = 0, h = 0;
        SDL_Vulkan_GetDrawableSize(m_window, &w, &h);
        ext.width = (uint32_t)std::max(w, 1);
        ext.height = (uint32_t)std::max(h, 1);
        ext.width = std::min(std::max(ext.width, caps.minImageExtent.width), caps.maxImageExtent.width);
        ext.height = std::min(std::max(ext.height, caps.minImageExtent.height), caps.maxImageExtent.height);
    }
    m_extent = ext;

    uint32_t pqf = m_core->present_family(m_window);
    uint32_t fams[2] = {m_core->graphics_queue().family, pqf};
    VkSwapchainCreateInfoKHR sci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    sci.surface = m_surface;
    sci.minImageCount = want;
    sci.imageFormat = m_format;
    sci.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    sci.imageExtent = m_extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    sci.imageSharingMode = (fams[0] == fams[1]) ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
    sci.queueFamilyIndexCount = (fams[0] == fams[1]) ? 0u : 2u;
    sci.pQueueFamilyIndices = fams;
    sci.preTransform = m_transform;             // pre-rotation: GPU/compositor cuida
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = VK_PRESENT_MODE_FIFO_KHR; // vsync (a bridge força INTERVAL_ONE)
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = VK_NULL_HANDLE;          // destruido logo depois

    VkSwapchainKHR old = m_sc;
    sci.oldSwapchain = old;
    VkSwapchainKHR neu = VK_NULL_HANDLE;
    if (vkCreateSwapchainKHR(m_core->device(), &sci, nullptr, &neu) != VK_SUCCESS) {
        NVR_ERROR("vkCreateSwapchainKHR falhou (%ux%u fmt=%d)", ext.width, ext.height, (int)m_format);
        return false;
    }
    destroy_swapchain_only();
    m_sc = neu;

    uint32_t n = 0;
    vkGetSwapchainImagesKHR(m_core->device(), m_sc, &n, nullptr);
    m_images.resize(n);
    vkGetSwapchainImagesKHR(m_core->device(), m_sc, &n, m_images.data());
    m_views.resize(n);
    for (uint32_t i = 0; i < n; i++) {
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = m_images[i];
        vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
        vi.format = m_format;
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        if (vkCreateImageView(m_core->device(), &vi, nullptr, &m_views[i]) != VK_SUCCESS) {
            NVR_ERROR("swapchain image view %u falhou", i);
            return false;
        }
    }
    m_gen++;
    NVR_INFO("swapchain: %u imagens %ux%u fmt=%d preTransform=%d (gen %u)", n, ext.width, ext.height,
             (int)m_format, (int)m_transform, m_gen);
    return true;
}

VkResult Swapchain::acquire(VkSemaphore sem, uint32_t *image_out) {
    for (int attempt = 0; attempt < 2; attempt++) {
        VkResult r = vkAcquireNextImageKHR(m_core->device(), m_sc, 500ull * 1000 * 1000 /*0.5s*/,
                                           sem, VK_NULL_HANDLE, image_out);
        if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) return VK_SUCCESS;
        if (r == VK_ERROR_OUT_OF_DATE_KHR) {
            if (!recreate()) return VK_ERROR_SURFACE_LOST_KHR;
            continue;
        }
        return r;
    }
    return VK_ERROR_OUT_OF_DATE_KHR;
}

VkResult Swapchain::present(VkQueue q, VkSemaphore wait, uint32_t image_index) {
    VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    pi.swapchainCount = 1;
    pi.pSwapchains = &m_sc;
    pi.pImageIndices = &image_index;
    pi.waitSemaphoreCount = 1;
    pi.pWaitSemaphores = &wait;
    VkResult r = vkQueuePresentKHR(q, &pi);
    if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR)
        recreate();   // recria para o proximo frame
    return r;
}

// ------------------------------------------------------- helpers de memoria
VmaAllocation mem_alloc_image(VkCore *core, const VkImageCreateInfo &ici, VkImage *img) {
    VmaAllocationCreateInfo aci{};
    aci.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
    VmaAllocation a = VK_NULL_HANDLE;
    if (vmaCreateImage(core->allocator(), &ici, &aci, img, &a, nullptr) != VK_SUCCESS) return VK_NULL_HANDLE;
    return a;
}

VmaAllocation mem_alloc_buffer(VkCore *core, const VkBufferCreateInfo &bci, VmaMemoryUsage usage, VkBuffer *buf) {
    VmaAllocationCreateInfo aci{};
    aci.usage = usage;
    VmaAllocation a = VK_NULL_HANDLE;
    if (vmaCreateBuffer(core->allocator(), &bci, &aci, buf, &a, nullptr) != VK_SUCCESS) return VK_NULL_HANDLE;
    return a;
}

void mem_free(VkCore *core, VmaAllocation a) { vmaFreeMemory(core->allocator(), a); }
void mem_free_image(VkCore *core, VkImage img, VmaAllocation a) { vmaDestroyImage(core->allocator(), img, a); }
void mem_map(VkCore *core, VmaAllocation a, void **out) {
    if (vmaMapMemory(core->allocator(), a, out) != VK_SUCCESS) *out = nullptr;
}
void mem_unmap(VkCore *core, VmaAllocation a) { vmaUnmapMemory(core->allocator(), a); }
void mem_name(VkCore *core, VmaAllocation a, const char *name) { vmaSetAllocationName(core->allocator(), a, name); }

} // namespace nvr
