// Tradutor bytecode D3D9 (SM2/SM3) -> SPIR-V e pipelines fixed-function.
// Toda a semantica vem do spec D3D9; registradores viram variaveis Private
// (evita Phi e permite subrotinas via OpFunctionCall).
#pragma once
#include "nvr_spirv.h"
#include "nvr_dx9bc.h"

namespace nvr {

// Mapeamento fixo usage/index -> location (compartilhado com o builder de
// vertex input do device). POSITION vira builtin Position no VS (sem location).
constexpr uint32_t kLocPosition = 0;
constexpr uint32_t kLocBlendWeight = 1;
constexpr uint32_t kLocBlendIndices = 2;
constexpr uint32_t kLocNormal = 3;
constexpr uint32_t kLocPointSize = 4;
constexpr uint32_t kLocTexcoord0 = 8;      // 8..15
constexpr uint32_t kLocTangent = 16;
constexpr uint32_t kLocBinormal = 17;
constexpr uint32_t kLocTessfactor = 18;
constexpr uint32_t kLocPositionT = 19;
constexpr uint32_t kLocColor0 = 20;
constexpr uint32_t kLocColor1 = 21;
constexpr uint32_t kLocFog = 22;
constexpr uint32_t kLocDepth = 23;
constexpr uint32_t kLocSample = 24;

// sentinela: output de position do VS vai para o builtin (sem location)
constexpr uint32_t kLocBuiltinPosition = 0xFE;

// locations 32+ ficam reservados para usages indexados incomuns
constexpr uint32_t kLocPositionIdxBase = 32;   // POSITION1..3 -> 33..35
constexpr uint32_t kLocNormalIdxBase = 36;     // NORMAL1..3 -> 37..39

uint32_t usage_to_location(uint32_t usage, uint32_t index);

struct XlateOut {
    std::vector<uint32_t> spirv;
    uint32_t max_const = 0;
    uint32_t sampler_mask = 0;
    bool uses_ib = false;          // usa constantes int/bool (layout variante B)
    std::string error;
};

struct VsXlateIn {
    const BcShader *sh = nullptr;
    const uint8_t *in_comps = nullptr;   // por location: 0..4 componentes (0 = ausente)
    bool flat_shade = false;
};

bool translate_vs(const VsXlateIn &in, XlateOut &out);

struct PsXlateIn {
    const BcShader *sh = nullptr;
    bool alpha_test = false;
    uint32_t alpha_cmp = 4;    // D3DCMPFUNC (mapeado pelo device)
};

bool translate_ps(const PsXlateIn &in, XlateOut &out);

} // namespace nvr
