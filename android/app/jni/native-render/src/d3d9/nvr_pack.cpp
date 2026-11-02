// O caminho quente: draw_common (state -> pipeline -> bind -> draw),
// build_pipeline (estado D3D9 -> VkGraphicsPipeline), empacotadores de UBO
// FFF, blit de present (escala + gamma ramp + pre-rotation) e helpers.
#include "nvr_device.h"
#include "nvr_com_impl.h"
#include <algorithm>
#include <cmath>
#include <SDL2/SDL.h>

namespace nvr {

// ------------------------------------------------------------ utilidades
UINT primitive_vertex_count(D3DPRIMITIVETYPE prim, UINT count) {
    switch (prim) {
    case D3DPT_POINTLIST: return count;
    case D3DPT_LINELIST: return count * 2;
    case D3DPT_LINESTRIP: return count + 1;
    case D3DPT_TRIANGLELIST: return count * 3;
    case D3DPT_TRIANGLESTRIP: return count + 2;
    case D3DPT_TRIANGLEFAN: return count + 2;
    default: return 0;
    }
}

VkPrimitiveTopology primitive_topology(D3DPRIMITIVETYPE prim) {
    switch (prim) {
    case D3DPT_POINTLIST: return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    case D3DPT_LINELIST: return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case D3DPT_LINESTRIP: return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case D3DPT_TRIANGLELIST: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case D3DPT_TRIANGLESTRIP: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case D3DPT_TRIANGLEFAN: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN;   // exige feature
    default: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    }
}

void mat3_inverse_transpose(const float m[16], float out[9]) {
    // Para normais: n' = IT * n com IT = inversa-transposta do upper-3x3 de
    // m (row-major 4x4). IT = cofatora/det (a inversa ja transposta), o que
    // dispensa uma segunda transposicao. Linhas de out sao as linhas de IT
    // (o shader faz dot(n, linha) para cada componente).
    float a = m[0], b = m[1], c = m[2];
    float d = m[4], e = m[5], f = m[6];
    float g = m[8], h = m[9], i = m[10];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    if (fabsf(det) < 1e-12f) {
        out[0] = 1; out[1] = 0; out[2] = 0;
        out[3] = 0; out[4] = 1; out[5] = 0;
        out[6] = 0; out[7] = 0; out[8] = 1;
        return;
    }
    float inv = 1.0f / det;
    out[0] = (e * i - f * h) * inv;
    out[1] = (f * g - d * i) * inv;
    out[2] = (d * h - e * g) * inv;
    out[3] = (c * h - b * i) * inv;
    out[4] = (a * i - c * g) * inv;
    out[5] = (c * d - a * f) * inv;
    out[6] = (b * f - c * e) * inv;
    out[7] = (a * h - b * g) * inv;
    out[8] = (a * e - b * d) * inv;
}

// ------------------------------------------------------- UBO packing FFF
void pack_fff_vs_ubo(float *dst, uint32_t vec4_count, const FffVsCfg &cfg, bool lights_view_space) {
    if (vec4_count < kFffVsEnd) return;
    auto put = [&](uint32_t slot, const float *v) { memcpy(dst + slot * 4, v, 16); };
    auto putmat = [&](uint32_t slot, const float *m) {
        for (int r = 0; r < 4; r++) put(slot + r, m + r * 4);
    };
    putmat(kFffVsWorld, cfg.world);
    putmat(kFffVsView, cfg.view);
    putmat(kFffVsProj, cfg.proj);
    float d[4] = {cfg.mat.Diffuse.r, cfg.mat.Diffuse.g, cfg.mat.Diffuse.b, cfg.mat.Diffuse.a};
    put(kFffVsMatDiff, d);
    float a[4] = {cfg.mat.Ambient.r, cfg.mat.Ambient.g, cfg.mat.Ambient.b, cfg.mat.Ambient.a};
    put(kFffVsMatAmb, a);
    float s[4] = {cfg.mat.Specular.r, cfg.mat.Specular.g, cfg.mat.Specular.b, cfg.mat.Power};
    put(kFffVsMatSpec, s);
    float e[4] = {cfg.mat.Emissive.r, cfg.mat.Emissive.g, cfg.mat.Emissive.b, 0};
    put(kFffVsMatEmis, e);
    put(kFffVsGlobalAmb, cfg.ambient);
    // luzes -> view space (D3D9 transforma lights com a view no SetLight;
    // aqui fazemos no pack para respeitar SetTransform posterior)
    float vm[16];
    memcpy(vm, cfg.view, 64);
    for (int i = 0; i < 8; i++) {
        uint32_t base = kFffVsLights + i * 6;
        const FffLightCfg &l = cfg.lights[i];
        float pos[4] = {l.position[0], l.position[1], l.position[2], l.range};
        float dir[4] = {l.direction[0], l.direction[1], l.direction[2], l.theta};
        if (lights_view_space && l.type != D3DLIGHT_DIRECTIONAL) {
            float *p = pos;
            float x = p[0], y = p[1], z = p[2];
            float r[4] = {x * vm[0] + y * vm[4] + z * vm[8] + vm[12],
                          x * vm[1] + y * vm[5] + z * vm[9] + vm[13],
                          x * vm[2] + y * vm[6] + z * vm[10] + vm[14], l.range};
            memcpy(pos, r, 16);
            float *dd = dir;
            float dx = dd[0], dy = dd[1], dz = dd[2];
            float r2[3] = {dx * vm[0] + dy * vm[4] + dz * vm[8],
                           dx * vm[1] + dy * vm[5] + dz * vm[9],
                           dx * vm[2] + dy * vm[6] + dz * vm[10]};
            dir[0] = r2[0]; dir[1] = r2[1]; dir[2] = r2[2];
        } else if (lights_view_space) {
            float dx = dir[0], dy = dir[1], dz = dir[2];
            dir[0] = dx * vm[0] + dy * vm[4] + dz * vm[8];
            dir[1] = dx * vm[1] + dy * vm[5] + dz * vm[9];
            dir[2] = dx * vm[2] + dy * vm[6] + dz * vm[10];
        }
        put(base, pos);
        put(base + 1, dir);
        float di[4] = {l.diffuse[0], l.diffuse[1], l.diffuse[2], l.phi};
        put(base + 2, di);
        float sp[4] = {l.specular[0], l.specular[1], l.specular[2], l.falloff};
        put(base + 3, sp);
        float at[4] = {l.atten0, l.atten1, l.atten2, (float)l.type};
        put(base + 4, at);
        float am[4] = {l.ambient[0], l.ambient[1], l.ambient[2], 0};
        put(base + 5, am);
    }
    float fog[4] = {cfg.fog_start, cfg.fog_end, cfg.fog_density, (float)cfg.fog_vmode};
    put(kFffVsFog, fog);
    for (int i = 0; i < 8; i++) {
        float tci[4] = {(float)(int32_t)cfg.stage[i].coord_index, (float)(int32_t)cfg.stage[i].ttff,
                        cfg.stage[i].has_texmat ? 1.0f : 0.0f, 0};
        put(kFffVsTci + i, tci);
        if (cfg.stage[i].has_texmat)
            putmat(kFffVsTexMat + i * 4, cfg.stage[i].texmat);
        else {
            float ident[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
            putmat(kFffVsTexMat + i * 4, ident);
        }
    }
    float it[4] = {cfg.world_view_it[0], cfg.world_view_it[1], cfg.world_view_it[2], 0};
    put(kFffVsWvIt, it);
    float it1[4] = {cfg.world_view_it[3], cfg.world_view_it[4], cfg.world_view_it[5], 0};
    put(kFffVsWvIt + 1, it1);
    float it2[4] = {cfg.world_view_it[6], cfg.world_view_it[7], cfg.world_view_it[8], 0};
    put(kFffVsWvIt + 2, it2);
    float misc[4] = {cfg.color_vertex ? 1.0f : 0.0f, cfg.specular_enable ? 1.0f : 0.0f,
                     cfg.normalize_normals ? 1.0f : 0.0f, cfg.local_viewer ? 1.0f : 0.0f};
    put(kFffVsMisc, misc);
    float src[4] = {(float)cfg.diffuse_src, (float)cfg.ambient_src, (float)cfg.specular_src,
                    (float)cfg.emissive_src};
    put(kFffVsMatSrc, src);
    float rangefog[4] = {0, 0, 0, 0};
    put(kFffVsFogColor, rangefog);   // .z usado? range_fog flag em fog color .w:
    float rf[4] = {0, 0, 0, cfg.range_fog ? 1.0f : 0.0f};
    put(kFffVsFogColor, rf);
    (void)rangefog;
}

void pack_fff_ps_ubo(float *dst, uint32_t vec4_count, const FffPsCfg &cfg, const DeviceState &st) {
    if (vec4_count < kFffPsSys + 1) return;
    auto put = [&](uint32_t slot, const float *v) { memcpy(dst + slot * 4, v, 16); };
    for (int i = 0; i < 8; i++) {
        float tf[4] = {cfg.stage[i].tfactor[0], cfg.stage[i].tfactor[1], cfg.stage[i].tfactor[2],
                       cfg.stage[i].tfactor[3]};
        put(kFffPsTfactor + i, tf);
    }
    float fc[4] = {(float)((st.rs[D3DRS_FOGCOLOR] >> 16) & 0xFF) / 255.0f,
                   (float)((st.rs[D3DRS_FOGCOLOR] >> 8) & 0xFF) / 255.0f,
                   (float)(st.rs[D3DRS_FOGCOLOR] & 0xFF) / 255.0f, 0};
    put(kFffPsFogColor, fc);
    float fp[4] = {cfg.fog_start, cfg.fog_end,
                   (float)((int32_t)(cfg.fog_tmode & 0xFF) | ((int32_t)(cfg.fog_vmode & 0xFF) << 8)),
                   cfg.fog_density};
    put(kFffPsFog, fp);
    for (int i = 0; i < 8; i++) {
        float ca[4] = {(float)(int32_t)cfg.stage[i].color_arg0, (float)(int32_t)cfg.stage[i].color_arg1,
                       (float)(int32_t)cfg.stage[i].color_op, (float)(int32_t)cfg.stage[i].color_arg2};
        put(kFffPsStages + 2 * i, ca);
        float cb[4] = {(float)(int32_t)cfg.stage[i].alpha_arg0, (float)(int32_t)cfg.stage[i].alpha_arg1,
                       (float)(int32_t)cfg.stage[i].alpha_op, (float)(int32_t)cfg.stage[i].alpha_arg2};
        put(kFffPsStages + 2 * i + 1, cb);
    }
    float sys[4] = {(float)(st.rs[D3DRS_ALPHAREF] & 0xFF) / 255.0f, (float)st.rs[D3DRS_ALPHAFUNC], 0, 0};
    put(kFffPsSys, sys);
}

// wrappers usados por nvr_draw.cpp
void pack_fff_vs_ubo(float *dst, uint32_t vec4_count) {
    FffVsCfg cfg{};
    FffPsCfg ps{};
    NrDevice *dev = g_current_device();
    std::vector<D3DVERTEXELEMENT9> elems;
    decl_to_elements(dev->state(), elems);
    uint8_t comps[32];
    input_comps_from_elements(elems, comps);
    dev->build_fff_cfg(cfg, ps, comps);
    pack_fff_vs_ubo(dst, vec4_count, cfg, true);
}
void pack_fff_ps_ubo(float *dst, uint32_t vec4_count) {
    FffVsCfg cfg{};
    FffPsCfg ps{};
    NrDevice *dev = g_current_device();
    std::vector<D3DVERTEXELEMENT9> elems;
    decl_to_elements(dev->state(), elems);
    uint8_t comps[32];
    input_comps_from_elements(elems, comps);
    dev->build_fff_cfg(cfg, ps, comps);
    pack_fff_ps_ubo(dst, vec4_count, ps, dev->state());
}

} // namespace nvr
