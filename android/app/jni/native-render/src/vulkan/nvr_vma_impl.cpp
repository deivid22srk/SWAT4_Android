// Unico ponto de implementacao do VMA (VulkanMemoryAllocator 3.x, Apache-2.0,
// vendored em thirdparty/vma). Nao e codigo do renderizador: e a biblioteca
// de alocacao escolhida pela analise (ver ANALISE.md 5.2).
#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 1
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 0
#include "vk_mem_alloc.h"
