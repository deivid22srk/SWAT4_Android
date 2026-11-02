#include "nvr_format.h"
#include <algorithm>

namespace nvr {

static const FormatInfo kFormats[] = {
    // d3dfmt                                  vk                                  bpb bw bh comp d s  name
    { D3DFMT_UNKNOWN,                          VK_FORMAT_UNDEFINED,                0, 1, 1, 0, 0, 0, "UNKNOWN" },
    { D3DFMT_X4R4G4B4,                         VK_FORMAT_B4G4R4A4_UNORM_PACK16,    2, 1, 1, 4, 0, 0, "X4R4G4B4" },
    { D3DFMT_A4R4G4B4,                         VK_FORMAT_B4G4R4A4_UNORM_PACK16,    2, 1, 1, 4, 0, 0, "A4R4G4B4" },
    { D3DFMT_R5G6B5,                           VK_FORMAT_B5G6R5_UNORM_PACK16,      2, 1, 1, 3, 0, 0, "R5G6B5" },
    { D3DFMT_X1R5G5B5,                         VK_FORMAT_A1R5G5B5_UNORM_PACK16,    2, 1, 1, 3, 0, 0, "X1R5G5B5" },
    { D3DFMT_A1R5G5B5,                         VK_FORMAT_A1R5G5B5_UNORM_PACK16,    2, 1, 1, 4, 0, 0, "A1R5G5B5" },
    { D3DFMT_A8,                               VK_FORMAT_R8_UNORM,                 1, 1, 1, 1, 0, 0, "A8" },
    { D3DFMT_L8,                               VK_FORMAT_R8_UNORM,                 1, 1, 1, 1, 0, 0, "L8" },
    { D3DFMT_A8L8,                             VK_FORMAT_R8G8_UNORM,               2, 1, 1, 2, 0, 0, "A8L8" },
    { D3DFMT_L16,                              VK_FORMAT_R16_UNORM,                2, 1, 1, 1, 0, 0, "L16" },
    { D3DFMT_A4L4,                             VK_FORMAT_R4G4_UNORM_PACK8,         1, 1, 1, 2, 0, 0, "A4L4" },
    { D3DFMT_X8R8G8B8,                         VK_FORMAT_B8G8R8A8_UNORM,           4, 1, 1, 4, 0, 0, "X8R8G8B8" },
    { D3DFMT_A8R8G8B8,                         VK_FORMAT_B8G8R8A8_UNORM,           4, 1, 1, 4, 0, 0, "A8R8G8B8" },
    { D3DFMT_X8B8G8R8,                         VK_FORMAT_R8G8B8A8_UNORM,           4, 1, 1, 4, 0, 0, "X8B8G8R8" },
    { D3DFMT_A8B8G8R8,                         VK_FORMAT_R8G8B8A8_UNORM,           4, 1, 1, 4, 0, 0, "A8B8G8R8" },
    { D3DFMT_A2R10G10B10,                      VK_FORMAT_A2B10G10R10_UNORM_PACK32, 4, 1, 1, 4, 0, 0, "A2R10G10B10" },
    { D3DFMT_A2B10G10R10,                      VK_FORMAT_A2R10G10B10_UNORM_PACK32, 4, 1, 1, 4, 0, 0, "A2B10G10R10" },
    { D3DFMT_A16B16G16R16,                     VK_FORMAT_R16G16B16A16_UNORM,       8, 1, 1, 4, 0, 0, "A16B16G16R16" },
    { D3DFMT_A16B16G16R16F,                    VK_FORMAT_R16G16B16A16_SFLOAT,      8, 1, 1, 4, 0, 0, "A16B16G16R16F" },
    { D3DFMT_A32B32G32R32F,                    VK_FORMAT_R32G32B32A32_SFLOAT,     16, 1, 1, 4, 0, 0, "A32B32G32R32F" },
    { D3DFMT_G16R16,                           VK_FORMAT_R16G16_UNORM,             4, 1, 1, 2, 0, 0, "G16R16" },
    { D3DFMT_G16R16F,                          VK_FORMAT_R16G16_SFLOAT,            4, 1, 1, 2, 0, 0, "G16R16F" },
    { D3DFMT_G32R32F,                          VK_FORMAT_R32G32_SFLOAT,            8, 1, 1, 2, 0, 0, "G32R32F" },
    { D3DFMT_R16F,                             VK_FORMAT_R16_SFLOAT,               2, 1, 1, 1, 0, 0, "R16F" },
    { D3DFMT_R32F,                             VK_FORMAT_R32_SFLOAT,               4, 1, 1, 1, 0, 0, "R32F" },
    // normal maps / bump (UE2)
    { D3DFMT_V8U8,                             VK_FORMAT_R8G8_SNORM,               2, 1, 1, 2, 0, 0, "V8U8" },
    { D3DFMT_Q8W8V8U8,                         VK_FORMAT_R8G8B8A8_SNORM,           4, 1, 1, 4, 0, 0, "Q8W8V8U8" },
    { D3DFMT_X8L8V8U8,                         VK_FORMAT_R8G8B8A8_SNORM,           4, 1, 1, 4, 0, 0, "X8L8V8U8" },
    { D3DFMT_V16U16,                           VK_FORMAT_R16G16_SNORM,             4, 1, 1, 2, 0, 0, "V16U16" },
    // block compressed
    { D3DFMT_DXT1,                             VK_FORMAT_BC1_RGBA_UNORM_BLOCK,     8, 4, 4, 4, 0, 0, "DXT1" },
    { D3DFMT_DXT2,                             VK_FORMAT_BC2_UNORM_BLOCK,         16, 4, 4, 4, 0, 0, "DXT2" },
    { D3DFMT_DXT3,                             VK_FORMAT_BC2_UNORM_BLOCK,         16, 4, 4, 4, 0, 0, "DXT3" },
    { D3DFMT_DXT4,                             VK_FORMAT_BC3_UNORM_BLOCK,         16, 4, 4, 4, 0, 0, "DXT4" },
    { D3DFMT_DXT5,                             VK_FORMAT_BC3_UNORM_BLOCK,         16, 4, 4, 4, 0, 0, "DXT5" },
    { MAKEFOURCC('A','T','I','1'),             VK_FORMAT_BC4_UNORM_BLOCK,          8, 4, 4, 1, 0, 0, "ATI1/BC4" },
    { MAKEFOURCC('B','C','4','U'),             VK_FORMAT_BC4_UNORM_BLOCK,          8, 4, 4, 1, 0, 0, "BC4U" },
    { MAKEFOURCC('A','T','I','2'),             VK_FORMAT_BC5_UNORM_BLOCK,         16, 4, 4, 2, 0, 0, "ATI2/BC5" },
    { MAKEFOURCC('B','C','5','U'),             VK_FORMAT_BC5_UNORM_BLOCK,         16, 4, 4, 2, 0, 0, "BC5U" },
    // depth/stencil
    { D3DFMT_D16,                              VK_FORMAT_D16_UNORM,                2, 1, 1, 1, 1, 0, "D16" },
    { D3DFMT_D24X8,                            VK_FORMAT_X8_D24_UNORM_PACK32,      4, 1, 1, 1, 1, 0, "D24X8" },
    { D3DFMT_D24S8,                            VK_FORMAT_D24_UNORM_S8_UINT,        4, 1, 1, 1, 1, 1, "D24S8" },
    { D3DFMT_D32,                              VK_FORMAT_X8_D24_UNORM_PACK32,      4, 1, 1, 1, 1, 0, "D32" },
    { D3DFMT_D32F_LOCKABLE,                    VK_FORMAT_D32_SFLOAT,               4, 1, 1, 1, 1, 0, "D32F" },
    { D3DFMT_D32_LOCKABLE,                     VK_FORMAT_D32_SFLOAT,               4, 1, 1, 1, 1, 0, "D32" },
    { D3DFMT_S8_LOCKABLE,                      VK_FORMAT_S8_UINT,                  1, 1, 1, 1, 0, 1, "S8" },
};

static const FormatInfo kUnknown = { D3DFMT_UNKNOWN, VK_FORMAT_UNDEFINED, 0, 1, 1, 0, 0, 0, "UNKNOWN" };

const FormatInfo &format_info(uint32_t d3dfmt) {
    for (size_t i = 0; i < sizeof(kFormats) / sizeof(kFormats[0]); i++)
        if (kFormats[i].d3d == d3dfmt) return kFormats[i];
    return kUnknown;
}

const FormatInfo &format_info_vk(VkFormat vk) {
    if (vk == VK_FORMAT_UNDEFINED) return kUnknown;
    for (size_t i = 0; i < sizeof(kFormats) / sizeof(kFormats[0]); i++)
        if (kFormats[i].vk == vk) return kFormats[i];
    return kUnknown;
}

FormatSupport query_format_support(VkPhysicalDevice pdev, VkFormat vk) {
    FormatSupport out{};
    if (vk == VK_FORMAT_UNDEFINED) return out;
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(pdev, vk, &fp);
    out.sample = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) != 0;
    out.color_rt = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) != 0;
    out.depth_rt = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
    out.blit_src = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_SRC_BIT) != 0;
    out.blit_dst = (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT) != 0;
    return out;
}

VkFormat choose_depth_format(VkPhysicalDevice pdev, uint32_t d3dfmt, bool *stencil_out) {
    auto supported = [&](VkFormat f) {
        VkFormatProperties fp{};
        vkGetPhysicalDeviceFormatProperties(pdev, f, &fp);
        return (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0;
    };
    bool stencil = false;
    VkFormat vk = VK_FORMAT_UNDEFINED;
    switch (d3dfmt) {
    case D3DFMT_D24S8:
        // Preferencia: D24S8 exato -> D32S8 -> D16S8 (shadow volumes precisam de stencil).
        if (supported(VK_FORMAT_D24_UNORM_S8_UINT)) vk = VK_FORMAT_D24_UNORM_S8_UINT;
        else if (supported(VK_FORMAT_D32_SFLOAT_S8_UINT)) vk = VK_FORMAT_D32_SFLOAT_S8_UINT;
        else if (supported(VK_FORMAT_D16_UNORM_S8_UINT)) vk = VK_FORMAT_D16_UNORM_S8_UINT;
        else if (supported(VK_FORMAT_D32_SFLOAT)) vk = VK_FORMAT_D32_SFLOAT;
        else if (supported(VK_FORMAT_D16_UNORM)) vk = VK_FORMAT_D16_UNORM;
        stencil = true;
        break;
    case D3DFMT_D24X8:
    case D3DFMT_D32:
        if (supported(VK_FORMAT_X8_D24_UNORM_PACK32)) vk = VK_FORMAT_X8_D24_UNORM_PACK32;
        else if (supported(VK_FORMAT_D32_SFLOAT)) vk = VK_FORMAT_D32_SFLOAT;
        else if (supported(VK_FORMAT_D16_UNORM)) vk = VK_FORMAT_D16_UNORM;
        break;
    case D3DFMT_D32F_LOCKABLE:
    case D3DFMT_D32_LOCKABLE:
        if (supported(VK_FORMAT_D32_SFLOAT)) vk = VK_FORMAT_D32_SFLOAT;
        else if (supported(VK_FORMAT_X8_D24_UNORM_PACK32)) vk = VK_FORMAT_X8_D24_UNORM_PACK32;
        break;
    case D3DFMT_D16:
        if (supported(VK_FORMAT_D16_UNORM)) vk = VK_FORMAT_D16_UNORM;
        else if (supported(VK_FORMAT_X8_D24_UNORM_PACK32)) vk = VK_FORMAT_X8_D24_UNORM_PACK32;
        break;
    case D3DFMT_S8_LOCKABLE:
        if (supported(VK_FORMAT_S8_UINT)) { vk = VK_FORMAT_S8_UINT; stencil = true; }
        break;
    default: {
        const FormatInfo &fi = format_info(d3dfmt);
        if (fi.depth && supported(fi.vk)) { vk = fi.vk; stencil = fi.stencil; }
        break;
    }
    }
    if (stencil_out) *stencil_out = stencil && vk != VK_FORMAT_UNDEFINED;
    return vk;
}

VkFormat choose_swapchain_format(VkPhysicalDevice pdev, VkSurfaceKHR surf) {
    uint32_t n = 0;
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(pdev, surf, &n, nullptr) != VK_SUCCESS || !n)
        return VK_FORMAT_B8G8R8A8_UNORM;
    std::vector<VkSurfaceFormatKHR> fmts(n);
    if (vkGetPhysicalDeviceSurfaceFormatsKHR(pdev, surf, &n, fmts.data()) != VK_SUCCESS)
        return VK_FORMAT_B8G8R8A8_UNORM;
    if (n == 1 && fmts[0].format == VK_FORMAT_UNDEFINED) return VK_FORMAT_B8G8R8A8_UNORM;
    // X8R8G8B8 do jogo e BGRA em memoria: BGRA8 UNORM preserva os canais.
    for (auto &f : fmts)
        if (f.format == VK_FORMAT_B8G8R8A8_UNORM) return VK_FORMAT_B8G8R8A8_UNORM;
    for (auto &f : fmts)
        if (f.format == VK_FORMAT_R8G8B8A8_UNORM) return VK_FORMAT_R8G8B8A8_UNORM;
    // fallback: sRGB (aplica gamma 2.2 no presente; melhor que falhar)
    for (auto &f : fmts)
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB) return VK_FORMAT_B8G8R8A8_SRGB;
    return fmts[0].format;
}

} // namespace nvr
