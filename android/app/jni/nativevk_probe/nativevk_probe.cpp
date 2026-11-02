#include <jni.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <cstdint>
#include <sstream>
#include <string>

namespace {

std::string resultName(VkResult result) {
    switch (result) {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    default: return "VkResult(" + std::to_string(static_cast<int>(result)) + ")";
    }
}

std::string versionString(uint32_t version) {
    std::ostringstream out;
    out << VK_VERSION_MAJOR(version) << "."
        << VK_VERSION_MINOR(version) << "."
        << VK_VERSION_PATCH(version);
    return out.str();
}

void appendFeature(std::ostringstream& out, const char* name, VkBool32 enabled) {
    out << "  " << name << "=" << (enabled ? "yes" : "no") << "\n";
}

} // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_io_github_deivid22srk_swat4_GameActivity_nativeVulkanReport(
        JNIEnv* env, jobject /* thiz */) {
    std::ostringstream out;
    out << "SWAT4 Android native Vulkan capability probe\n"
        << "mode=diagnostic-only; no renderer/device queues are created\n";

    uint32_t loaderVersion = VK_API_VERSION_1_0;
    const VkResult versionResult = vkEnumerateInstanceVersion(&loaderVersion);
    if (versionResult != VK_SUCCESS) {
        out << "loader_version_query=" << resultName(versionResult) << "\n";
        loaderVersion = VK_API_VERSION_1_0;
    } else {
        out << "loader_api=" << versionString(loaderVersion) << "\n";
    }

    const uint32_t requestedApi = std::min(loaderVersion, VK_API_VERSION_1_1);
    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.pApplicationName = "SWAT4 Android Vulkan Probe";
    appInfo.applicationVersion = 1;
    appInfo.pEngineName = "SWAT4 Android";
    appInfo.engineVersion = 1;
    appInfo.apiVersion = requestedApi;

    VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    instanceInfo.pApplicationInfo = &appInfo;

    VkInstance instance = VK_NULL_HANDLE;
    const VkResult createResult = vkCreateInstance(&instanceInfo, nullptr, &instance);
    if (createResult != VK_SUCCESS) {
        out << "instance_create=" << resultName(createResult) << "\n"
            << "probe_complete=no\n";
        return env->NewStringUTF(out.str().c_str());
    }

    uint32_t physicalCount = 0;
    VkResult result = vkEnumeratePhysicalDevices(instance, &physicalCount, nullptr);
    if (result != VK_SUCCESS || physicalCount == 0) {
        out << "physical_devices=" << (result == VK_SUCCESS ? "0" : resultName(result)) << "\n"
            << "probe_complete=no\n";
        vkDestroyInstance(instance, nullptr);
        return env->NewStringUTF(out.str().c_str());
    }

    VkPhysicalDevice devices[16]{};
    const uint32_t capacity = std::min(physicalCount, 16u);
    uint32_t enumeratedCount = capacity;
    result = vkEnumeratePhysicalDevices(instance, &enumeratedCount, devices);
    if (result != VK_SUCCESS) {
        out << "physical_device_enumeration=" << resultName(result) << "\n"
            << "probe_complete=no\n";
        vkDestroyInstance(instance, nullptr);
        return env->NewStringUTF(out.str().c_str());
    }

    out << "physical_devices=" << physicalCount << "\n";
    for (uint32_t i = 0; i < enumeratedCount; ++i) {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(devices[i], &properties);
        VkPhysicalDeviceFeatures features{};
        vkGetPhysicalDeviceFeatures(devices[i], &features);
        VkPhysicalDeviceMemoryProperties memory{};
        vkGetPhysicalDeviceMemoryProperties(devices[i], &memory);

        out << "\n[device " << i << "]\n"
            << "name=" << properties.deviceName << "\n"
            << "vendor_id=0x" << std::hex << properties.vendorID
            << " device_id=0x" << properties.deviceID << std::dec << "\n"
            << "device_type=" << static_cast<int>(properties.deviceType) << "\n"
            << "api=" << versionString(properties.apiVersion) << "\n"
            << "driver_version=" << properties.driverVersion << "\n"
            << "max_image_2d=" << properties.limits.maxImageDimension2D << "\n";

        uint32_t queueCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &queueCount, nullptr);
        VkQueueFamilyProperties queues[32]{};
        const uint32_t queueCapacity = std::min(queueCount, 32u);
        uint32_t enumeratedQueueCount = queueCapacity;
        vkGetPhysicalDeviceQueueFamilyProperties(devices[i], &enumeratedQueueCount, queues);
        bool graphicsQueue = false;
        for (uint32_t q = 0; q < enumeratedQueueCount; ++q) {
            const bool graphics = (queues[q].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            graphicsQueue = graphicsQueue || graphics;
            out << "queue[" << q << "].flags=0x" << std::hex
                << queues[q].queueFlags << std::dec
                << " count=" << queues[q].queueCount
                << " graphics=" << (graphics ? "yes" : "no") << "\n";
        }
        out << "graphics_queue=" << (graphicsQueue ? "yes" : "no") << "\n";

        VkDeviceSize largestDeviceLocalHeap = 0;
        for (uint32_t h = 0; h < memory.memoryHeapCount; ++h) {
            if ((memory.memoryHeaps[h].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) != 0)
                largestDeviceLocalHeap = std::max(largestDeviceLocalHeap, memory.memoryHeaps[h].size);
        }
        out << "memory_heaps=" << memory.memoryHeapCount
            << " largest_device_local_mb=" << (largestDeviceLocalHeap / (1024 * 1024)) << "\n";

        out << "features:\n";
        appendFeature(out, "geometryShader", features.geometryShader);
        appendFeature(out, "tessellationShader", features.tessellationShader);
        appendFeature(out, "samplerAnisotropy", features.samplerAnisotropy);
        appendFeature(out, "textureCompressionBC", features.textureCompressionBC);
        appendFeature(out, "shaderStorageImageExtendedFormats", features.shaderStorageImageExtendedFormats);
        appendFeature(out, "shaderFloat64", features.shaderFloat64);
        appendFeature(out, "multiDrawIndirect", features.multiDrawIndirect);
    }

    out << "\nprobe_complete=yes\n"
        << "note=This probe does not validate D3D9 feature coverage or render a frame.\n";
    vkDestroyInstance(instance, nullptr);
    return env->NewStringUTF(out.str().c_str());
}
