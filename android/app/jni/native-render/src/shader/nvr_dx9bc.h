// Parser do bytecode D3D9 (SM 2.0/3.0, vertex e pixel) — formato de tokens
// do d3d9types.h. Produz um IR simples consumido pelo tradutor SPIR-V.
#pragma once
#include "../nvr_common.h"

namespace nvr {

enum class BcReg : uint32_t {
    Temp = 0, Input = 1, Const = 2, AddrTex = 3, RastOut = 4, AttrOut = 5, Output = 6,
    ConstInt = 7, ColorOut = 8, DepthOut = 9, Sampler = 10, Const2 = 11, Const3 = 12,
    Const4 = 13, ConstBool = 14, Loop = 15, TempF16 = 16, Misc = 17, Label = 18,
    Predicate = 19
};

enum BcOp : uint32_t {
    B_DEF = 1, B_NOP = 0, B_MOV = 2, B_ADD = 3, B_SUB = 4, B_MAD = 5, B_MUL = 6, B_RCP = 7, B_RSQ = 8,
    B_DP3 = 9, B_DP4 = 10, B_MIN = 11, B_MAX = 12, B_SLT = 13, B_SGE = 14, B_EXP = 15,
    B_LOG = 16, B_LIT = 17, B_DST = 18, B_LRP = 19, B_FRC = 20, B_M4x4 = 21, B_M4x3 = 22,
    B_M3x4 = 23, B_M3x3 = 24, B_M3x2 = 25, B_CALL = 26, B_CALLNZ = 27, B_LOOP = 28,
    B_RET = 29, B_ENDLOOP = 30, B_LABEL = 31, B_DCL = 32, B_POW = 33, B_CRS = 34,
    B_SGN = 35, B_ABS = 36, B_NRM = 37, B_SINCOS = 38, B_REP = 39, B_ENDREP = 40,
    B_IF = 41, B_IFC = 42, B_ELSE = 43, B_ENDIF = 44, B_BREAK = 45, B_BREAKC = 46,
    B_MOVA = 47, B_DEFB = 48, B_DEFI = 49, B_TEXCOORD = 64, B_TEXKILL = 65, B_TEXLD = 66,
    B_CND = 81, B_CMP = 82, B_DP2ADD = 83, B_TEXLDD = 85, B_SETP = 86, B_TEXLDL = 87,
    B_BREAKP = 88, B_DSX = 89, B_DSY = 90, B_COMMENT = 0xFFFE, B_END = 0xFFFF
};

// modificadores de source (D3DSPSM_*)
enum BcSrcMod : uint32_t {
    SM_NONE = 0, SM_NEG = 1 << 5, SM_BIAS = 2 << 5, SM_BIASNEG = 3 << 5, SM_SIGN = 4 << 5,
    SM_SIGNNEG = 5 << 5, SM_COMP = 6 << 5, SM_X2 = 7 << 5, SM_X2NEG = 8 << 5, SM_DZ = 9 << 5,
    SM_DW = 10 << 5, SM_ABS = 11 << 5, SM_ABSNEG = 12 << 5, SM_NOT = 13 << 5
};

// modificadores de destination (D3DSPDM_*)
enum BcDstMod : uint32_t { DM_NONE = 0, DM_SATURATE = 1 << 4, DM_PARTIAL = 2 << 4, DM_CENTROID = 4 << 4 };

struct BcOperand {
    BcReg type = BcReg::Temp;
    uint32_t num = 0;
    uint32_t writemask = 0xF;      // dest
    uint8_t swizzle[4] = {0, 1, 2, 3};
    uint32_t srcmod = SM_NONE;
    uint32_t destmod = DM_NONE;
    uint32_t ctrl = 0;             // bits 16-19 brutos (comparacao em SETP/BREAKC/IFC)
    bool relative = false;         // c[aL.x + offset]
};

struct BcInstr {
    uint32_t op = 0;
    std::vector<BcOperand> dests, srcs;
    // comparacao do token de instrucao (bits 16-18, D3DSPC_*) para SETP/IFC/BREAKC
    uint32_t cmp = 0;
    // payloads
    uint32_t defb = 0;             // DEFB
    int32_t defi[4] = {0, 0, 0, 0};
    float deff[4] = {0, 0, 0, 0};
    // DCL
    uint32_t dcl_usage = 0;        // D3DDECLUSAGE (input/output dcl)
    uint32_t dcl_usage_index = 0;
    uint32_t dcl_sampler_type = 0; // D3DSTT_* (2D/CUBE/VOLUME)
    bool dcl_centroid = false;
    // TEXLD: 0=normal, 1=proj (D3DSI_TEXLD_PROJECT), 2=bias (D3DSI_TEXLD_BIAS)
    uint32_t texld_mode = 0;
    // SINCOS
    uint32_t sincos_mode = 0;
};

struct BcShader {
    bool is_vs = true;
    uint32_t major = 3, minor = 0;
    std::vector<BcInstr> instrs;
    // maxima faixa de constantes float usadas (para o UBO)
    uint32_t max_const = 0;
    uint32_t sampler_mask = 0;     // bit i = sampler si usado
    uint32_t input_mask = 0;       // declaracoes de input (vs) — usage indexado
    uint32_t output_mask = 0;
    bool uses_vface = false, uses_vpos = false;
    bool uses_aL = false;
    bool uses_p0 = false;
    bool uses_ib = false;          // usa registradores i#/b# (UBO de int/bool)
    bool dynamic_sampler = false;  // s[aL] — nao suportado (erro explicito)
};

// Faz o parse completo. Retorna false com erro em `error`.
bool bc_parse(const uint32_t *tokens, size_t token_count, BcShader &out, std::string &error);

const char *bc_op_name(uint32_t op);

} // namespace nvr
