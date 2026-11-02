// FramebufferCache: implementacao (nvr_gfx.cpp ja grande demais).
// Nao depende de tipos da camada d3d9: as formas get_by_view (generica, blits
// e present) e get (camada d3d9, com views extraidas do NrSurface pelo caller)
// compartilham o mesmo mapa, chaveado por (view rt, view ds, render pass).
#include "nvr_gfx.h"

namespace nvr {

void FramebufferCache::shutdown() {
    if (!m_core) return;
    for (auto &kv : m_map) vkDestroyFramebuffer(m_core->device(), kv.second.fb, nullptr);
    m_map.clear();
    m_owners.clear();
    m_core = nullptr;
}

VkFramebuffer FramebufferCache::create(VkImageView rt, VkImageView ds, VkRenderPass rp,
                                       uint32_t w, uint32_t h) {
    VkImageView views[2] = {rt, ds};
    uint32_t n = ds ? 2u : 1u;
    VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fi.renderPass = rp;
    fi.attachmentCount = n;
    fi.pAttachments = views;
    fi.width = std::max(1u, w);
    fi.height = std::max(1u, h);
    fi.layers = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(m_core->device(), &fi, nullptr, &fb) != VK_SUCCESS) {
        NVR_ERROR("vkCreateFramebuffer falhou");
        return VK_NULL_HANDLE;
    }
    return fb;
}

static uint64_t fb_key(void *rt, void *ds, VkRenderPass rp) {
    return fnv1a_mix(fnv1a_mix(fnv1a_mix((uint32_t)(uintptr_t)rt, 0x9E3779B9u),
                               (uint32_t)(uintptr_t)ds), (uint32_t)(uintptr_t)rp);
}

VkFramebuffer FramebufferCache::get_by_view(VkImageView rt_view, VkImageView ds_view,
                                            VkRenderPass rp, uint32_t w, uint32_t h) {
    uint64_t h_key = fb_key((void *)rt_view, (void *)ds_view, rp);
    auto it = m_map.find(h_key);
    if (it != m_map.end()) return it->second.fb;
    VkFramebuffer fb = create(rt_view, ds_view, rp, w, h);
    if (!fb) return VK_NULL_HANDLE;
    m_map[h_key] = {fb, (void *)rt_view, (void *)ds_view};
    m_owners.push_back({h_key, (void *)rt_view});
    if (ds_view) m_owners.push_back({h_key, (void *)ds_view});
    return fb;
}

VkFramebuffer FramebufferCache::get(void *rt_surf, void *ds_surf, VkRenderPass rp, VkCore &vk) {
    // views sao extraidas do NrSurface pelo caller via os wrappers abaixo
    (void)rt_surf; (void)ds_surf; (void)rp; (void)vk;
    return VK_NULL_HANDLE;   // substituida pela variante com views (device)
}

void FramebufferCache::forget_surface(void *surf) {
    for (auto it = m_owners.begin(); it != m_owners.end();) {
        if (it->second == surf) {
            auto e = m_map.find(it->first);
            if (e != m_map.end()) {
                vkDestroyFramebuffer(m_core->device(), e->second.fb, nullptr);
                m_map.erase(e);
            }
            it = m_owners.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace nvr
