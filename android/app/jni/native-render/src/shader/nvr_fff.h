// Pipelines fixed-function D3D9 -> SPIR-V (VS com T&L completo, PS com a
// cadeia TextureStageState). Usa o mesmo emissor SPIR-V e os MESMOS bindings
// dos shaders SM3: os slots uniformes FFF sao preenchidos pelo device apenas
// quando nenhum shader de jogo esta ativo.
// Semantica: spec D3D9 (lighting view-space, atten, spot cone, fog vertex/
// table, TCI, D3DTTFF, ops de TSS completos).
#pragma once
#include "nvr_compile.h"
#include <d3d9.h>

namespace nvr {

// ------------------------------ configuracoes espelhando o estado do device
struct FffLightCfg {
    bool enabled = false;
    uint32_t type = D3DLIGHT_DIRECTIONAL;
    float diffuse[4] = {0, 0, 0, 0}, specular[4] = {0, 0, 0, 0}, ambient[4] = {0, 0, 0, 0};
    float position[4] = {0, 0, 0, 0}, direction[4] = {0, 0, 1, 0};
    float range = 0, falloff = 0, atten0 = 1, atten1 = 0, atten2 = 0;
    float theta = 0, phi = 0;
};

struct FffTexStage {
    uint32_t coord_index = 0;      // D3DTSS_TEXCOORDINDEX
    uint32_t ttff = 0;             // D3DTTFF_*
    bool has_texmat = false;
    float texmat[16] = {0};        // row-major D3DMATRIX
    // PS
    uint32_t color_arg0 = 0, color_arg1 = 0, color_arg2 = 0, color_op = 0;
    uint32_t alpha_arg0 = 0, alpha_arg1 = 0, alpha_arg2 = 0, alpha_op = 0;
    float tfactor[4] = {0, 0, 0, 1};
};

struct FffVsCfg {
    float world[16], view[16], proj[16];          // row-major (D3DMATRIX)
    float world_view_it[9];                        // inversa-transposta 3x3 (world*view)
    D3DMATERIAL9 mat{};
    FffLightCfg lights[8];
    float ambient[4] = {0, 0, 0, 0};
    uint32_t fog_vmode = 0, fog_tmode = 0;
    float fog_start = 0, fog_end = 1, fog_density = 1;
    bool color_vertex = false, normalize_normals = false, specular_enable = false;
    bool local_viewer = false, range_fog = false;
    uint32_t diffuse_src = 1, ambient_src = 1, specular_src = 2, emissive_src = 1; // D3DMCS
    FffTexStage stage[8];
    const uint8_t *in_comps = nullptr;
};

struct FffPsCfg {
    FffTexStage stage[8];
    uint32_t active_stages = 0;    // ate o primeiro DISABLE (ou 0 = sem textura)
    uint32_t fog_tmode = 0;
    uint32_t fog_vmode = 0;        // vertex fog ativo (o fator vem do VS)
    float fog_start = 0, fog_end = 1, fog_density = 1;
    bool alpha_test = false;
    uint32_t alpha_cmp = 4;
};

bool generate_fff_vs(const FffVsCfg &cfg, XlateOut &out);
bool generate_fff_ps(const FffPsCfg &cfg, XlateOut &out);

// Slots uniformes (vec4) usados pelo FFF dentro dos UBOs compartilhados.
constexpr uint32_t kFffVsWorld = 0;       // [0..3]
constexpr uint32_t kFffVsView = 4;        // [4..7]
constexpr uint32_t kFffVsProj = 8;        // [8..11]
constexpr uint32_t kFffVsMatDiff = 12;
constexpr uint32_t kFffVsMatAmb = 13;
constexpr uint32_t kFffVsMatSpec = 14;    // + power em .w
constexpr uint32_t kFffVsMatEmis = 15;
constexpr uint32_t kFffVsGlobalAmb = 16;
constexpr uint32_t kFffVsFog = 17;        // (start, end, density, vmode)
constexpr uint32_t kFffVsFogColor = 18;   // .w = range_fog
constexpr uint32_t kFffVsMisc = 19;       // (color_vertex, spec_en, norm_norm, local_viewer)
constexpr uint32_t kFffVsMatSrc = 20;     // (diffuse_src, ambient_src, specular_src, emissive_src)
constexpr uint32_t kFffVsTci = 21;        // 8 x (coord_index, ttff, has_texmat, 0) -> [21..28]
constexpr uint32_t kFffVsLights = 32;     // 8 x 6: pos(.w=range), dir(.w=theta),
                                          //         diff(.w=phi), spec(.w=falloff),
                                          //         atten(.w=type), amb -> [32..79]
constexpr uint32_t kFffVsTexMat = 80;     // 8 x 4 (linhas da D3DTS_TEXTUREi) -> [80..111]
constexpr uint32_t kFffVsWvIt = 112;      // 3 slots: inverse-transpose(world*view) -> [112..114]
constexpr uint32_t kFffVsEnd = 115;

constexpr uint32_t kFffPsTfactor = 0;     // [0..7]
constexpr uint32_t kFffPsFogColor = 8;
constexpr uint32_t kFffPsFog = 9;         // (start, end, tmode|vmode<<8, density)
constexpr uint32_t kFffPsStages = 10;     // 8 x 2: color=(a0,a1,op,a2) alpha=(a0,a1,op,a2)
                                          //        -> [10..25]
constexpr uint32_t kFffPsSys = 26;        // .x = alphaRef, .y = alphaCmp

} // namespace nvr
