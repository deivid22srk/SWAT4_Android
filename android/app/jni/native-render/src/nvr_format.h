// Tabelas D3DFORMAT <-> VkFormat e utilidades de formato.
#pragma once
#include "nvr_common.h"
#include <vulkan/vulkan.h>

namespace nvr {

struct FormatInfo {
    uint32_t d3d;               // D3DFORMAT
    VkFormat vk;                // VK_FORMAT_UNDEFINED = sem mapeamento
    uint32_t bytes_per_block;   // 1 para formatos empacotados; 8/16 para BC
    uint32_t block_width;       // 1; 4 para BC
    uint32_t block_height;
    uint8_t  components;
    bool     depth;
    bool     stencil;
    const char *name;
};

// Retorna a linha da tabela (sempre valida; vk==UNDEFINED quando sem mapa).
const FormatInfo &format_info(uint32_t d3dfmt);

// Lookup reverso (VkFormat -> linha). Usado pelos caminhos de transfer
// (upload/readback) que recebem VkFormat direto.
const FormatInfo &format_info_vk(VkFormat vk);

// Formatos de cor/depth que o device declara suportar (respeitando features).
struct FormatSupport {
    bool color_rt;      // COLOR_ATTACHMENT
    bool depth_rt;      // DEPTH_STENCIL_ATTACHMENT
    bool sample;
    bool blit_src, blit_dst;
};

FormatSupport query_format_support(VkPhysicalDevice pdev, VkFormat vk);

// Escolhe um VkFormat de depth/stencil para o D3DFORMAT pedido, respeitando o
// que o device tem de verdade. stencil_out diz se o escolhido tem stencil.
VkFormat choose_depth_format(VkPhysicalDevice pdev, uint32_t d3dfmt, bool *stencil_out);

// Formato de cor da swapchain preferido (UNORM; fallback sRGB se so houver).
VkFormat choose_swapchain_format(VkPhysicalDevice pdev, VkSurfaceKHR surf);

} // namespace nvr
