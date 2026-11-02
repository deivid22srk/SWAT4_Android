#include "nvr_dx9bc.h"
#include <algorithm>

namespace nvr {

static constexpr uint32_t kOpcodeMask = 0x0000FFFF;
static constexpr uint32_t kInstrFlag = 0x80000000u;

const char *bc_op_name(uint32_t op) {
    switch (op) {
    case B_NOP: return "nop"; case B_MOV: return "mov"; case B_ADD: return "add";
    case B_SUB: return "sub"; case B_MAD: return "mad"; case B_MUL: return "mul";
    case B_RCP: return "rcp"; case B_RSQ: return "rsq"; case B_DP3: return "dp3";
    case B_DP4: return "dp4"; case B_MIN: return "min"; case B_MAX: return "max";
    case B_SLT: return "slt"; case B_SGE: return "sge"; case B_EXP: return "exp";
    case B_LOG: return "log"; case B_LIT: return "lit"; case B_DST: return "dst";
    case B_LRP: return "lrp"; case B_FRC: return "frc"; case B_M4x4: return "m4x4";
    case B_M4x3: return "m4x3"; case B_M3x4: return "m3x4"; case B_M3x3: return "m3x3";
    case B_M3x2: return "m3x2"; case B_CALL: return "call"; case B_CALLNZ: return "callnz";
    case B_LOOP: return "loop"; case B_RET: return "ret"; case B_ENDLOOP: return "endloop";
    case B_LABEL: return "label"; case B_DCL: return "dcl"; case B_POW: return "pow";
    case B_CRS: return "crs"; case B_SGN: return "sgn"; case B_ABS: return "abs";
    case B_NRM: return "nrm"; case B_SINCOS: return "sincos"; case B_REP: return "rep";
    case B_ENDREP: return "endrep"; case B_IF: return "if"; case B_IFC: return "ifc";
    case B_ELSE: return "else"; case B_ENDIF: return "endif"; case B_BREAK: return "break";
    case B_BREAKC: return "breakc"; case B_MOVA: return "mova"; case B_DEFB: return "defb";
    case B_DEFI: return "defi"; case B_TEXCOORD: return "texcoord"; case B_TEXKILL: return "texkill";
    case B_TEXLD: return "texld"; case B_CND: return "cnd"; case B_CMP: return "cmp";
    case B_DP2ADD: return "dp2add"; case B_TEXLDD: return "texldd"; case B_SETP: return "setp";
    case B_TEXLDL: return "texldl"; case B_BREAKP: return "breakp"; case B_DSX: return "dsx";
    case B_DSY: return "dsy"; case B_COMMENT: return "comment"; case B_END: return "end";
    default: return "?";
    }
}

namespace {

struct OpSig { uint32_t dest; uint32_t src; };

OpSig op_sig(uint32_t op) {
    switch (op) {
    // ALU basica
    case B_DEF: return {1, 0};
    case B_MOV: return {1, 1};
    case B_ADD: case B_SUB: case B_MUL: case B_DP3: case B_DP4: case B_MIN: case B_MAX:
    case B_SLT: case B_SGE: case B_CRS: case B_POW: return {1, 2};
    case B_MAD: case B_LRP: case B_CND: case B_CMP: return {1, 3};
    case B_RCP: case B_RSQ: case B_EXP: case B_LOG: case B_LIT: case B_FRC: case B_ABS:
    case B_NRM: case B_MOVA: case B_DSX: case B_DSY: return {1, 1};
    case B_DST: return {1, 2};
    case B_M4x4: case B_M4x3: case B_M3x4: case B_M3x3: case B_M3x2: return {1, 2};
    case B_SGN: case B_SETP: return {1, 2};
    case B_DP2ADD: return {1, 3};            // dest = dot2(s0,s1) + s2.x
    case B_SINCOS: return {1, 1};            // as 2 consts dummies sao lidas a parte
    case B_TEXLD: case B_TEXLDL: return {1, 2};
    case B_TEXLDD: return {1, 4};
    case B_TEXKILL: return {1, 0};
    case B_TEXCOORD: return {1, 0};
    // fluxo (loop: dest=aL + src=i#/c#; breakp: dest=p#.mascara)
    case B_CALL: case B_LABEL: case B_REP: case B_IF: return {0, 1};
    case B_LOOP: return {1, 1};
    case B_BREAKP: return {1, 0};
    case B_CALLNZ: case B_IFC: case B_BREAKC: return {0, 2};
    case B_RET: case B_ENDLOOP: case B_ENDREP: case B_ELSE: case B_ENDIF: case B_BREAK:
    case B_NOP: return {0, 0};
    default: return {0xFFFFFFFFu, 0xFFFFFFFFu};   // desconhecido
    }
}

BcReg map_reg_type(uint32_t t) {
    switch (t) {
    case 0: return BcReg::Temp;
    case 1: return BcReg::Input;
    case 2: case 11: case 12: case 13: return BcReg::Const;
    case 3: return BcReg::AddrTex;
    case 4: return BcReg::RastOut;
    case 5: return BcReg::AttrOut;
    case 6: return BcReg::Output;
    case 7: return BcReg::ConstInt;
    case 8: return BcReg::ColorOut;
    case 9: return BcReg::DepthOut;
    case 10: return BcReg::Sampler;
    case 14: return BcReg::ConstBool;
    case 15: return BcReg::Loop;
    case 16: return BcReg::TempF16;
    case 17: return BcReg::Misc;
    case 18: return BcReg::Label;
    case 19: return BcReg::Predicate;
    default: return BcReg::Temp;
    }
}

bool parse_operand(const uint32_t *&p, const uint32_t *end, BcOperand &o, bool is_dest,
                   BcShader &sh, std::string &err) {
    if (p >= end) { err = "bytecode truncado (operando)"; return false; }
    uint32_t tok = *p++;
    // tipo do registrador: 5 bits = (bits 28-30) | (bits 11-12) << 3
    // (D3DSP_REGTYPE_SHIFT/SHIFT2; PREDICATE/MISC/LABEL/TEMPF16 usam o par alto)
    o.type = map_reg_type(((tok >> 28) & 0x7) | (((tok >> 11) & 0x3) << 3));
    o.num = tok & 0x7FF;
    o.ctrl = (tok >> 16) & 0xF;
    if (is_dest) {
        uint32_t wm = (tok >> 16) & 0xF;
        o.writemask = wm ? wm : 0xF;
        o.destmod = ((tok >> 20) & 0xF) << 4;    // D3DSPDM_* (bit 4 setado no enum)
        o.srcmod = SM_NONE;
    } else {
        o.swizzle[0] = (tok >> 16) & 3;
        o.swizzle[1] = (tok >> 18) & 3;
        o.swizzle[2] = (tok >> 20) & 3;
        o.swizzle[3] = (tok >> 22) & 3;
        o.srcmod = ((tok >> 24) & 0xF) << 5;     // D3DSP_SRCMOD_SHIFT = 24
        o.destmod = DM_NONE;
    }
    if ((tok >> 13) & 1) {                        // D3DSHADER_ADDRMODE_RELATIVE
        o.relative = true;
        BcOperand rel;
        if (!parse_operand(p, end, rel, false, sh, err)) return false;
        if (rel.type != BcReg::AddrTex && rel.type != BcReg::Loop) {
            err = "endereco relativo sem aL";
            return false;
        }
        sh.uses_aL = true;
        if (!sh.is_vs && o.type == BcReg::Sampler) sh.dynamic_sampler = true;
    }
    return true;
}

} // namespace

bool bc_parse(const uint32_t *tokens, size_t token_count, BcShader &out, std::string &error) {
    if (token_count < 2) { error = "shader vazio"; return false; }
    uint32_t version = tokens[0];
    if ((version & 0xFFFF0000u) == 0xFFFE0000u) out.is_vs = true;
    else if ((version & 0xFFFF0000u) == 0xFFFF0000u) out.is_vs = false;
    else { error = "token de versao invalido"; return false; }
    out.major = (version >> 8) & 0xFF;
    out.minor = version & 0xFF;
    if (out.major < 2) { error = "shader SM1.x fora do escopo (o jogo usa SM3)"; return false; }
    if (out.major > 3) { error = "shader acima de SM3"; return false; }

    const uint32_t *p = tokens + 2;
    const uint32_t *end = tokens + token_count;
    uint32_t declared_len = tokens[1];
    if (declared_len && declared_len <= token_count - 2) end = tokens + 2 + declared_len;

    while (p < end) {
        uint32_t tok = *p;
        if ((tok & 0xFFFF0000u) == 0xFFFE0000u) { p += 1 + (tok & 0x7FFFFFFFu); continue; }
        if (tok == 0x0000FFFFu) break;
        if (!(tok & kInstrFlag)) {
            char buf[64];
            snprintf(buf, sizeof buf, " (token %08x no indice %zu)", tok,
                     (size_t)(p - tokens));
            error = std::string("token inesperado (nao-instrucao)") + buf;
            return false;
        }

        BcInstr ins;
        ins.op = tok & kOpcodeMask;
        if (ins.op == B_TEXLD)
            ins.texld_mode = (tok >> 16) & 0x3;   // D3DSI_TEXLD_PROJECT / _BIAS
        if (ins.op == B_SETP || ins.op == B_IFC || ins.op == B_BREAKC)
            ins.cmp = (tok >> 16) & 0x7;          // D3DSPC_* no token de instrucao
        p++;

        if (ins.op == B_DCL) {
            if (p >= end) { error = "DCL truncado"; return false; }
            uint32_t dcl_tok = *p++;
            BcOperand o;
            if (!parse_operand(p, end, o, true, out, error)) return false;
            if (o.type == BcReg::Sampler) {
                ins.dcl_sampler_type = (dcl_tok >> 27) & 0xF;   // D3DSTT_2D=2, CUBE=3, VOLUME=4
                out.sampler_mask |= 1u << o.num;
            } else if (o.type == BcReg::Input || o.type == BcReg::Output) {
                // D3DSP_DCL_USAGE_SHIFT = 0 (bits 0-3); USAGEINDEX_SHIFT = 16
                ins.dcl_usage = (dcl_tok >> 0) & 0xF;
                ins.dcl_usage_index = (dcl_tok >> 16) & 0xF;
                ins.dcl_centroid = (dcl_tok >> 20) & 1;
                if (o.type == BcReg::Input) out.input_mask |= 1u << o.num;
                else out.output_mask |= 1u << o.num;
            } else if (o.type == BcReg::Misc) {
                if (o.num == 0) out.uses_vpos = true;
                if (o.num == 1) out.uses_vface = true;
            } else if (o.type == BcReg::DepthOut) {
                out.output_mask |= 1u << 0;
            } else {
                error = std::string("dcl em registrador inesperado (") + bc_op_name(ins.op) + ")";
                return false;
            }
            ins.dests.push_back(o);
            out.instrs.push_back(ins);
            continue;
        }
        if (ins.op == B_DEF) {
            BcOperand o;
            if (!parse_operand(p, end, o, true, out, error)) return false;
            ins.dests.push_back(o);
            for (int i = 0; i < 4 && p < end; i++) {
                uint32_t bits = *p++;
                float f; memcpy(&f, &bits, 4);
                ins.deff[i] = f;
            }
            out.max_const = std::max(out.max_const, o.num + 1);
            out.instrs.push_back(ins);
            continue;
        }
        if (ins.op == B_DEFI) {
            BcOperand o;
            if (!parse_operand(p, end, o, true, out, error)) return false;
            ins.dests.push_back(o);
            for (int i = 0; i < 4 && p < end; i++) ins.defi[i] = (int32_t)*p++;
            out.max_const = std::max(out.max_const, o.num + 1);
            out.instrs.push_back(ins);
            continue;
        }
        if (ins.op == B_DEFB) {
            BcOperand o;
            if (!parse_operand(p, end, o, true, out, error)) return false;
            ins.dests.push_back(o);
            ins.defb = *p++;
            out.instrs.push_back(ins);
            continue;
        }
        if (ins.op == B_SINCOS) {
            BcOperand d;
            if (!parse_operand(p, end, d, true, out, error)) return false;
            ins.dests.push_back(d);
            BcOperand s0;
            if (!parse_operand(p, end, s0, false, out, error)) return false;
            ins.srcs.push_back(s0);
            BcOperand c1, c2;   // constantes dummies do spec (c# / c#+1)
            if (!parse_operand(p, end, c1, false, out, error)) return false;
            if (!parse_operand(p, end, c2, false, out, error)) return false;
            ins.sincos_mode = c1.num;   // 0 = sincos completo, 1 = cos, 2 = sin
            if (c1.type == BcReg::Const) out.max_const = std::max(out.max_const, c1.num + 1);
            out.instrs.push_back(ins);
            continue;
        }

        OpSig sig = op_sig(ins.op);
        if (sig.dest == 0xFFFFFFFFu) {
            error = std::string("opcode nao suportado: ") + bc_op_name(ins.op) +
                    " (0x" + std::to_string(ins.op) + ")";
            return false;
        }
        for (uint32_t i = 0; i < sig.dest && p < end; i++) {
            BcOperand o;
            if (!parse_operand(p, end, o, true, out, error)) return false;
            ins.dests.push_back(o);
        }
        for (uint32_t i = 0; i < sig.src && p < end; i++) {
            BcOperand o;
            if (!parse_operand(p, end, o, false, out, error)) return false;
            if (o.type == BcReg::Const)
                out.max_const = std::max(out.max_const, o.num + 1);
            if (o.type == BcReg::Sampler) {
                out.sampler_mask |= 1u << o.num;
                if (o.relative) out.dynamic_sampler = true;
            }
            if (o.type == BcReg::ConstInt || o.type == BcReg::ConstBool) out.uses_ib = true;
            ins.srcs.push_back(o);
        }
        out.instrs.push_back(ins);
    }
    return true;
}

} // namespace nvr
