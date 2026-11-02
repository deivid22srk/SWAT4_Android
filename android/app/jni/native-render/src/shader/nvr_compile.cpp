// Tradutor bytecode D3D9 SM2/SM3 -> SPIR-V. Contrato em nvr_compile.h.
//
// Decisoes (documentadas em docs/native-render/ANALISE.md 5.3):
//  - registradores temp/a0/aL/p0 = variaveis Private: permite loops/if/labels
//    sem construcao de Phi e subrotinas D3D9 via OpFunctionCall;
//  - subrotinas (labels) emitidas em pos-ordem DFS a partir do main (SPIR-V
//    nao aceita referencia a frente);
//  - DEF/DEFI/DEFB viram constantes do modulo (shadow dos registradores);
//  - alpha test D3D9 (pos-PS) injetado no fim do pixel shader;
//  - texkill = OpKill estruturado (selection merge + kill block).
#include "nvr_compile.h"
#include <algorithm>
#include <set>

namespace nvr {

namespace {

struct Ctx {
    SpirvBuilder b;
    const BcShader &sh;
    bool is_vs;
    const uint8_t *in_comps;
    bool flat_shade;
    bool alpha_test;
    uint32_t alpha_cmp;
    std::string error;

    uint32_t t_void = 0, t_f32 = 0, t_i32 = 0, t_u32 = 0, t_bool = 0;
    uint32_t t_v2 = 0, t_v3 = 0, t_v4 = 0, t_ivec4 = 0, t_bvec4 = 0;
    uint32_t t_simg2d = 0, t_simgcube = 0, t_simgvol = 0, t_f4res = 0;
    uint32_t t_fn_void = 0;
    uint32_t vs_ubo = 0, ps_ubo = 0, ib_ubo = 0;
    uint32_t t_v4_uniform_ptr = 0;

    uint32_t temp_var[64] = {};
    bool temp_used[64] = {};
    uint32_t al_var = 0, p0_var = 0;
    uint32_t in_var[32] = {};
    uint32_t out_var[32] = {};
    uint32_t out_pos = 0, out_depth = 0;
    uint32_t vpos_var = 0, vface_var = 0;
    uint32_t smp_var[16] = {};
    uint32_t smp_dim[16] = {};

    std::unordered_map<uint32_t, uint32_t> def_f, def_i, def_b;
    std::unordered_map<uint32_t, uint32_t> dcl_loc;

    std::vector<uint32_t> loop_merges;
    struct IfState { uint32_t merge, else_block; bool has_else; bool had_break; };
    std::vector<IfState> if_stack;
    std::unordered_map<uint32_t, uint32_t> label_fn;
    std::vector<uint32_t> call_order;   // subrotinas em pos-ordem DFS

    Ctx(const BcShader &s, bool vs, const uint8_t *ic, bool flat, bool at, uint32_t ac)
        : sh(s), is_vs(vs), in_comps(ic), flat_shade(flat), alpha_test(at), alpha_cmp(ac) {}

    bool fail(const std::string &e) { if (error.empty()) error = e; return false; }

    // ------------------------------------------------------------- setup
    void init() {
        b.capability(spv::CapabilityShader);
        b.ext_import("GLSL.std.450");
        t_void = b.type_void();
        t_f32 = b.type_float();
        t_i32 = b.type_int(true);
        t_u32 = b.type_int(false);
        t_bool = b.type_bool();
        t_v2 = b.type_vec(t_f32, 2);
        t_v3 = b.type_vec(t_f32, 3);
        t_v4 = b.type_vec(t_f32, 4);
        t_ivec4 = b.type_vec(t_i32, 4);
        t_bvec4 = b.type_vec(t_bool, 4);
        t_f4res = b.type_sampled_image(
            b.type_image(t_f32, spv::Dim2D, false, false, false, 1, spv::ImageFormatUnknown));
        t_simg2d = b.type_sampled_image(
            b.type_image(t_f32, spv::Dim2D, false, false, false, 1, spv::ImageFormatUnknown));
        t_simgcube = b.type_sampled_image(
            b.type_image(t_f32, spv::DimCube, false, false, false, 1, spv::ImageFormatUnknown));
        t_simgvol = b.type_sampled_image(
            b.type_image(t_f32, spv::Dim3D, false, false, false, 1, spv::ImageFormatUnknown));
        t_fn_void = b.type_function(t_void, {});
        t_v4_uniform_ptr = b.type_pointer(spv::StorageClassUniform, t_v4);
    }

    void start_module() {
        b.start(is_vs ? spv::ExecutionModelVertex : spv::ExecutionModelFragment, "main");
        b.memory_model(spv::AddressingModelLogical, spv::MemoryModelGLSL450);
        if (!is_vs) b.execution_mode(spv::ExecutionModeOriginUpperLeft);
    }

    void declare_ubos() {
        uint32_t arr = b.type_array_ubo(t_v4, b.const_u32(256), 16);
        uint32_t blk = b.type_struct({arr});
        b.member_decorate(blk, 0, spv::DecorationOffset, 0);
        b.decorate(blk, spv::DecorationBlock);
        vs_ubo = b.variable(spv::StorageClassUniform, blk);
        b.decorate(vs_ubo, spv::DecorationDescriptorSet, {0});
        b.decorate(vs_ubo, spv::DecorationBinding, {kBindVsUbo});
        b.name(vs_ubo, "vs_consts");

        uint32_t arrp = b.type_array_ubo(t_v4, b.const_u32(224), 16);
        uint32_t blkp = b.type_struct({arrp, t_v4});
        b.member_decorate(blkp, 0, spv::DecorationOffset, 0);
        b.member_decorate(blkp, 1, spv::DecorationOffset, 224 * 16);
        b.decorate(blkp, spv::DecorationBlock);
        ps_ubo = b.variable(spv::StorageClassUniform, blkp);
        b.decorate(ps_ubo, spv::DecorationDescriptorSet, {0});
        b.decorate(ps_ubo, spv::DecorationBinding, {kBindPsUbo});
        b.name(ps_ubo, "ps_consts");
    }

    void ensure_ib_ubo() {
        if (ib_ubo) return;
        uint32_t ai = b.type_array_ubo(t_ivec4, b.const_u32(16), 16);
        uint32_t bu = b.type_array_ubo(t_u32, b.const_u32(16), 16);   // std140: 16
        uint32_t blki = b.type_struct({ai, bu});
        b.member_decorate(blki, 0, spv::DecorationOffset, 0);
        b.member_decorate(blki, 1, spv::DecorationOffset, 256);
        b.decorate(blki, spv::DecorationBlock);
        ib_ubo = b.variable(spv::StorageClassUniform, blki);
        b.decorate(ib_ubo, spv::DecorationDescriptorSet, {0});
        b.decorate(ib_ubo, spv::DecorationBinding, {kBindIbUbo});
    }

    // ------------------------------------------------------- registradores
    uint32_t temp(uint32_t idx) {
        if (idx >= 64) { fail("temp acima de r63"); return 0; }
        if (!temp_used[idx]) {
            temp_used[idx] = true;
            temp_var[idx] = b.variable(spv::StorageClassPrivate, t_v4, b.const_null(t_v4));
        }
        return temp_var[idx];
    }
    uint32_t addr_reg() {
        if (!al_var) al_var = b.variable(spv::StorageClassPrivate, t_ivec4, b.const_null(t_ivec4));
        return al_var;
    }
    uint32_t pred_reg() {
        if (!p0_var) p0_var = b.variable(spv::StorageClassPrivate, t_bvec4, b.const_null(t_bvec4));
        return p0_var;
    }
    uint32_t input_var(uint32_t loc) {
        if (in_var[loc]) return in_var[loc];
        uint8_t comps = in_comps ? in_comps[loc] : (is_vs ? 0 : 4);
        if (!comps) return 0;
        uint32_t vt = b.type_vec(t_f32, comps);
        uint32_t v = b.variable(spv::StorageClassInput, vt);
        b.decorate(v, spv::DecorationLocation, {loc});
        b.add_interface_var(spv::StorageClassInput, v);
        in_var[loc] = v;
        return v;
    }
    uint32_t input_as_v4(uint32_t loc) {
        uint32_t v = input_var(loc);
        if (!v) return b.const_null(t_v4);
        uint8_t comps = in_comps ? in_comps[loc] : 4;
        if (comps == 4) return b.load(t_v4, v);
        uint32_t val = b.load(b.type_vec(t_f32, comps), v);
        std::vector<uint32_t> parts{val};
        for (uint32_t i = comps; i < 4; i++) parts.push_back(b.const_f32(0.0f));
        return b.composite_construct(t_v4, parts);
    }
    uint32_t output_var(uint32_t loc) {
        if (out_var[loc]) return out_var[loc];
        uint32_t v = b.variable(spv::StorageClassOutput, t_v4);
        b.decorate(v, spv::DecorationLocation, {loc});
        if (flat_shade) b.decorate(v, spv::DecorationFlat);
        b.add_interface_var(spv::StorageClassOutput, v);
        out_var[loc] = v;
        return v;
    }
    uint32_t out_position() {
        if (!out_pos) {
            out_pos = b.variable(spv::StorageClassOutput, t_v4);
            b.decorate(out_pos, spv::DecorationBuiltIn, {(uint32_t)spv::BuiltInPosition});
            b.add_interface_var(spv::StorageClassOutput, out_pos);
        }
        return out_pos;
    }
    uint32_t out_depth_var() {
        if (!out_depth) {
            out_depth = b.variable(spv::StorageClassOutput, t_f32);
            b.decorate(out_depth, spv::DecorationBuiltIn, {(uint32_t)spv::BuiltInFragDepth});
            b.add_interface_var(spv::StorageClassOutput, out_depth);
        }
        return out_depth;
    }
    uint32_t frag_coord() {
        if (!vpos_var) {
            vpos_var = b.variable(spv::StorageClassInput, t_v4);
            b.decorate(vpos_var, spv::DecorationBuiltIn, {(uint32_t)spv::BuiltInFragCoord});
            b.add_interface_var(spv::StorageClassInput, vpos_var);
        }
        return b.load(t_v4, vpos_var);
    }
    uint32_t front_face_v4() {
        if (!vface_var) {
            vface_var = b.variable(spv::StorageClassInput, t_bool);
            b.decorate(vface_var, spv::DecorationBuiltIn, {(uint32_t)spv::BuiltInFrontFacing});
            b.add_interface_var(spv::StorageClassInput, vface_var);
        }
        uint32_t fb = b.load(t_bool, vface_var);
        uint32_t f = b.select(t_f32, fb, b.const_f32(1.0f), b.const_f32(-1.0f));
        return b.composite_construct(t_v4, {f, f, f, f});
    }
    uint32_t sampler_var(uint32_t idx) {
        if (idx >= 16) { fail("sampler acima de s15"); return 0; }
        if (!smp_var[idx]) {
            // smp_dim guarda o valor D3DSTT do dcl: 2=2D, 3=CUBE, 4=VOLUME
            uint32_t st = smp_dim[idx] == 3 ? t_simgcube
                          : smp_dim[idx] == 4 ? t_simgvol : t_simg2d;
            uint32_t v = b.variable(spv::StorageClassUniformConstant, st);
            b.decorate(v, spv::DecorationDescriptorSet, {0});
            b.decorate(v, spv::DecorationBinding, {kBindSamplerBase + idx});
            smp_var[idx] = v;
        }
        return smp_var[idx];
    }

    // --------------------------------------------------------- utilidades
    uint32_t splat(float f) {
        uint32_t c = b.const_f32(f);
        return b.composite_construct(t_v4, {c, c, c, c});
    }
    uint32_t broadcast(uint32_t v, uint32_t comp) {
        uint32_t s = b.composite_extract(t_f32, v, {comp});
        return b.composite_construct(t_v4, {s, s, s, s});
    }
    uint32_t shuffle4(uint32_t v, const BcOperand &o) {
        return b.vector_shuffle(t_v4, v, v, {o.swizzle[0], o.swizzle[1], o.swizzle[2], o.swizzle[3]});
    }
    uint32_t src_mod(uint32_t v, const BcOperand &o) {
        switch (o.srcmod >> 5) {
        case 0: return v;
        case 1: return b.fneg(t_v4, v);
        case 2: return b.fsub(t_v4, v, splat(0.5f));
        case 3: return b.fneg(t_v4, b.fsub(t_v4, v, splat(0.5f)));
        case 4: return b.ext(t_v4, G450_FSign, {v});
        case 5: return b.fneg(t_v4, b.ext(t_v4, G450_FSign, {v}));
        case 6: return b.fsub(t_v4, splat(1.0f), v);
        case 7: return b.fmul(t_v4, v, splat(2.0f));
        case 8: return b.fneg(t_v4, b.fmul(t_v4, v, splat(2.0f)));
        case 9: return b.fdiv(t_v4, v, broadcast(v, 2));
        case 10: return b.fdiv(t_v4, v, broadcast(v, 3));
        case 11: return b.ext(t_v4, G450_FAbs, {v});
        case 12: return b.fneg(t_v4, b.ext(t_v4, G450_FAbs, {v}));
        default: fail("modificador de source desconhecido"); return v;
        }
    }
    // type: 0 = vec4, 1 = bvec4, 2 = ivec4
    void store_masked(uint32_t var, uint32_t value, const BcOperand &o, uint32_t vtype = 0) {
        uint32_t wm = o.writemask;
        if (vtype == 0 && o.destmod == DM_SATURATE)
            value = b.ext(t_v4, G450_FClamp, {value, splat(0.0f), splat(1.0f)});
        if (wm == 0xF) { b.store(var, value); return; }
        uint32_t t = vtype == 1 ? t_bvec4 : vtype == 2 ? t_ivec4 : t_v4;
        uint32_t old = b.load(t, var);
        std::vector<uint32_t> idx;
        for (uint32_t c = 0; c < 4; c++) idx.push_back((wm >> c) & 1 ? c : 4 + c);
        uint32_t merged = b.vector_shuffle(t, value, old, idx);
        b.store(var, merged);
    }

    uint32_t load_const_reg(const BcOperand &o) {
        if (!o.relative) {
            auto it = def_f.find(o.num);
            if (it != def_f.end()) return it->second;
        }
        uint32_t idx = o.relative
            ? b.op(spv::OpIAdd, t_i32,
                   {b.composite_extract(t_i32, b.load(t_ivec4, addr_reg()), {0}),
                    b.const_i32((int32_t)o.num)})
            : b.const_i32((int32_t)o.num);
        uint32_t ptr = b.access_chain(t_v4_uniform_ptr, vs_ubo, {b.const_i32(0), idx});
        return b.load(t_v4, ptr);
    }

    uint32_t in_loc_of(const BcOperand &o) {
        auto it = dcl_loc.find(o.num);
        return it != dcl_loc.end() ? it->second : o.num;
    }
    uint32_t out_loc_of(const BcOperand &o) {
        auto it = dcl_loc.find(o.num);
        return it != dcl_loc.end() ? it->second : 0xFFFFFFFFu;
    }

    uint32_t load_src(const BcOperand &o) {
        switch (o.type) {
        case BcReg::Temp:
            if (o.num >= 64) { fail("temp > r63"); return b.const_null(t_v4); }
            return b.load(t_v4, temp(o.num));
        case BcReg::Const:
            return load_const_reg(o);
        case BcReg::Input:
            return input_as_v4(in_loc_of(o));
        case BcReg::AddrTex:
            if (is_vs) return b.op(spv::OpConvertSToF, t_v4, b.load(t_ivec4, addr_reg()));
            fail("registrador TEX (SM1.x) em shader SM2+"); return b.const_null(t_v4);
        case BcReg::RastOut: case BcReg::AttrOut: case BcReg::ColorOut: case BcReg::Output: {
            if (!is_vs) { fail("output lido no PS"); return b.const_null(t_v4); }
            uint32_t loc = out_loc_of(o);
            if (loc == 0xFFFFFFFFu) return b.const_null(t_v4);
            return b.load(t_v4, output_var(loc));
        }
        case BcReg::Misc:
            return o.num == 0 ? frag_coord() : front_face_v4();
        case BcReg::ConstInt: {
            ensure_ib_ubo();
            uint32_t ptr = b.access_chain(b.type_pointer(spv::StorageClassUniform, t_ivec4),
                                          ib_ubo, {b.const_i32(0), b.const_i32((int32_t)o.num)});
            return b.op(spv::OpConvertSToF, t_v4, b.load(t_ivec4, ptr));
        }
        case BcReg::ConstBool: {
            ensure_ib_ubo();
            uint32_t ptr = b.access_chain(b.type_pointer(spv::StorageClassUniform, t_u32),
                                          ib_ubo, {b.const_i32(1), b.const_i32((int32_t)o.num)});
            uint32_t uv = b.load(t_u32, ptr);
            uint32_t f = b.select(t_f32, b.op(spv::OpINotEqual, t_bool, {uv, b.const_u32(0)}),
                                  b.const_f32(1.0f), b.const_f32(0.0f));
            return b.composite_construct(t_v4, {f, f, f, f});
        }
        case BcReg::Loop:
            return b.op(spv::OpConvertSToF, t_v4, b.load(t_ivec4, addr_reg()));
        case BcReg::Predicate: {
            uint32_t pv = b.load(t_bvec4, pred_reg());
            uint32_t any = b.op(spv::OpAny, t_bool, {pv});
            uint32_t f = b.select(t_f32, any, b.const_f32(1.0f), b.const_f32(0.0f));
            return b.composite_construct(t_v4, {f, f, f, f});
        }
        default:
            fail("registrador de source nao suportado (tipo " + std::to_string((uint32_t)o.type) +
                 " num " + std::to_string(o.num) + ")");
            return b.const_null(t_v4);
        }
    }

    uint32_t dest_var(const BcOperand &o, bool &is_bool) {
        is_bool = false;
        switch (o.type) {
        case BcReg::Temp: return temp(o.num);
        case BcReg::AddrTex: return is_vs ? addr_reg() : temp(o.num);
        case BcReg::RastOut:
            if (o.num == 0) return out_position();
            if (o.num == 1) return output_var(kLocFog);
            if (o.num == 2) return output_var(kLocPointSize);
            fail("rastout desconhecido"); return 0;
        case BcReg::ColorOut: return output_var(kLocColor0 + o.num);
        case BcReg::AttrOut: return output_var(kLocTexcoord0 + o.num);
        case BcReg::Output: {
            if (is_vs) {
                uint32_t loc = out_loc_of(o);
                if (loc == kLocBuiltinPosition) return out_position();
                if (loc == 0xFFFFFFFFu) { fail("output VS sem dcl"); return 0; }
                return output_var(loc);
            }
            return output_var(o.num);   // PS: oC# -> location #
        }
        case BcReg::DepthOut: return out_depth_var();
        case BcReg::Predicate: is_bool = true; return pred_reg();
        case BcReg::ConstInt: case BcReg::ConstBool:
            fail("DEFI/DEFB fora do lugar"); return 0;
        default:
            fail("registrador de dest nao suportado");
            return 0;
        }
    }

    // ------------------------------------------------------ comparacoes
    uint32_t compare_v4(uint32_t a, uint32_t c, uint32_t d3dspc) {
        switch (d3dspc) {
        case 1: return b.op(spv::OpFOrdLessThan, t_bvec4, {a, c});
        case 2: return b.op(spv::OpFOrdEqual, t_bvec4, {a, c});
        case 3: return b.op(spv::OpFOrdLessThanEqual, t_bvec4, {a, c});
        case 4: return b.op(spv::OpFOrdGreaterThan, t_bvec4, {a, c});
        case 5: return b.op(spv::OpFOrdNotEqual, t_bvec4, {a, c});
        case 6: return b.op(spv::OpFOrdGreaterThanEqual, t_bvec4, {a, c});
        case 7: return b.const_bool(true);
        case 0: return b.const_bool(false);
        default: return b.op(spv::OpFOrdLessThan, t_bvec4, {a, c});
        }
    }

    uint32_t bool_of_const(const BcOperand &o) {
        auto it = def_b.find(o.num);
        if (it != def_b.end()) return it->second;
        ensure_ib_ubo();
        uint32_t ptr = b.access_chain(b.type_pointer(spv::StorageClassUniform, t_u32),
                                      ib_ubo, {b.const_i32(1), b.const_i32((int32_t)o.num)});
        uint32_t uv = b.load(t_u32, ptr);
        return b.op(spv::OpINotEqual, t_bool, {uv, b.const_u32(0)});
    }

    // ------------------------------------------------------ instrucoes
    void emit_load(const BcInstr &ins) {
        uint32_t v = src_mod(shuffle4(load_src(ins.srcs[0]), ins.srcs[0]), ins.srcs[0]);
        bool ib;
        uint32_t d = dest_var(ins.dests[0], ib);
        if (is_vs && ins.dests[0].type == BcReg::AddrTex) {
            // mova: a0 recebe int truncado (semantica D3D9)
            uint32_t i4 = b.op(spv::OpConvertFToS, t_ivec4, v);
            store_masked(d, i4, ins.dests[0], 2);
        } else {
            store_masked(d, v, ins.dests[0]);
        }
    }
    void emit_binop(const BcInstr &ins, spv::Op op) {
        uint32_t a = src_mod(shuffle4(load_src(ins.srcs[0]), ins.srcs[0]), ins.srcs[0]);
        uint32_t c = src_mod(shuffle4(load_src(ins.srcs[1]), ins.srcs[1]), ins.srcs[1]);
        uint32_t v = b.op(op, t_v4, {a, c});
        bool ib;
        store_masked(dest_var(ins.dests[0], ib), v, ins.dests[0]);
    }
    void emit_mad(const BcInstr &ins) {
        uint32_t a = src_mod(shuffle4(load_src(ins.srcs[0]), ins.srcs[0]), ins.srcs[0]);
        uint32_t c = src_mod(shuffle4(load_src(ins.srcs[1]), ins.srcs[1]), ins.srcs[1]);
        uint32_t d3 = src_mod(shuffle4(load_src(ins.srcs[2]), ins.srcs[2]), ins.srcs[2]);
        uint32_t v = b.fma(t_v4, a, c, d3);
        bool ib;
        store_masked(dest_var(ins.dests[0], ib), v, ins.dests[0]);
    }
    void emit_dot(const BcInstr &ins, uint32_t ncomps) {
        uint32_t a = shuffle4(load_src(ins.srcs[0]), ins.srcs[0]);
        uint32_t c = shuffle4(load_src(ins.srcs[1]), ins.srcs[1]);
        uint32_t d;
        if (ncomps == 3) {
            uint32_t a3 = b.vector_shuffle(t_v3, a, a, {0, 1, 2});
            uint32_t c3 = b.vector_shuffle(t_v3, c, c, {0, 1, 2});
            d = b.composite_construct(t_v4, {b.dot(t_f32, a3, c3), b.dot(t_f32, a3, c3),
                                             b.dot(t_f32, a3, c3), b.dot(t_f32, a3, c3)});
        } else {
            // OpDot: resultado sempre escalar
            uint32_t dd = b.dot(t_f32, a, c);
            d = b.composite_construct(t_v4, {dd, dd, dd, dd});
        }
        bool ib;
        store_masked(dest_var(ins.dests[0], ib), d, ins.dests[0]);
    }
    void emit_scalar_fn(const BcInstr &ins, uint32_t gop) {
        uint32_t a = src_mod(shuffle4(load_src(ins.srcs[0]), ins.srcs[0]), ins.srcs[0]);
        uint32_t v = b.ext(t_v4, gop, {a});
        bool ib;
        store_masked(dest_var(ins.dests[0], ib), v, ins.dests[0]);
    }
    void emit_rcp(const BcInstr &ins) {
        uint32_t a = src_mod(shuffle4(load_src(ins.srcs[0]), ins.srcs[0]), ins.srcs[0]);
        bool ib;
        store_masked(dest_var(ins.dests[0], ib), b.fdiv(t_v4, splat(1.0f), a), ins.dests[0]);
    }
    void emit_rsq(const BcInstr &ins) {
        uint32_t a = src_mod(shuffle4(load_src(ins.srcs[0]), ins.srcs[0]), ins.srcs[0]);
        a = b.ext(t_v4, G450_FAbs, {a});
        bool ib;
        store_masked(dest_var(ins.dests[0], ib), b.ext(t_v4, G450_InverseSqrt, {a}), ins.dests[0]);
    }
    void emit_minmax(const BcInstr &ins, bool is_max) {
        uint32_t a = src_mod(shuffle4(load_src(ins.srcs[0]), ins.srcs[0]), ins.srcs[0]);
        uint32_t c = src_mod(shuffle4(load_src(ins.srcs[1]), ins.srcs[1]), ins.srcs[1]);
        bool ib;
        store_masked(dest_var(ins.dests[0], ib), b.ext(t_v4, is_max ? G450_FMax : G450_FMin, {a, c}),
                     ins.dests[0]);
    }
    void emit_cmp_sel(const BcInstr &ins) {
        // cmp dest, s0, s1, s2: dest = (s0 >= 0) ? s1 : s2
        uint32_t s0 = src_mod(shuffle4(load_src(ins.srcs[0]), ins.srcs[0]), ins.srcs[0]);
        uint32_t s1 = src_mod(shuffle4(load_src(ins.srcs[1]), ins.srcs[1]), ins.srcs[1]);
        uint32_t s2 = src_mod(shuffle4(load_src(ins.srcs[2]), ins.srcs[2]), ins.srcs[2]);
        uint32_t cond = b.op(spv::OpFOrdGreaterThanEqual, t_bvec4, {s0, splat(0.0f)});
        uint32_t v = b.select(t_v4, cond, s1, s2);
        bool ib;
        store_masked(dest_var(ins.dests[0], ib), v, ins.dests[0]);
    }
    void emit_cnd(const BcInstr &ins) {
        // cnd dest, s0, s1, s2: dest = (s2 >= 0.5) ? s0 : s1
        uint32_t s0 = src_mod(shuffle4(load_src(ins.srcs[0]), ins.srcs[0]), ins.srcs[0]);
        uint32_t s1 = src_mod(shuffle4(load_src(ins.srcs[1]), ins.srcs[1]), ins.srcs[1]);
        uint32_t s2 = src_mod(shuffle4(load_src(ins.srcs[2]), ins.srcs[2]), ins.srcs[2]);
        uint32_t cond = b.op(spv::OpFOrdGreaterThanEqual, t_bvec4, {s2, splat(0.5f)});
        uint32_t v = b.select(t_v4, cond, s0, s1);
        bool ib;
        store_masked(dest_var(ins.dests[0], ib), v, ins.dests[0]);
    }
    void emit_lrp(const BcInstr &ins) {
        uint32_t t = src_mod(shuffle4(load_src(ins.srcs[0]), ins.srcs[0]), ins.srcs[0]);
        uint32_t a = src_mod(shuffle4(load_src(ins.srcs[1]), ins.srcs[1]), ins.srcs[1]);
        uint32_t c = src_mod(shuffle4(load_src(ins.srcs[2]), ins.srcs[2]), ins.srcs[2]);
        bool ib;
        store_masked(dest_var(ins.dests[0], ib), b.ext(t_v4, G450_FMix, {a, c, t}), ins.dests[0]);
    }
    void emit_slt_sge(const BcInstr &ins, bool ge) {
        uint32_t a = src_mod(shuffle4(load_src(ins.srcs[0]), ins.srcs[0]), ins.srcs[0]);
        uint32_t c = src_mod(shuffle4(load_src(ins.srcs[1]), ins.srcs[1]), ins.srcs[1]);
        uint32_t cond = ge ? b.op(spv::OpFOrdGreaterThanEqual, t_bvec4, {a, c})
                           : b.op(spv::OpFOrdLessThan, t_bvec4, {a, c});
        uint32_t one = b.const_f32(1.0f), zero = b.const_f32(0.0f);
        bool ib;
        store_masked(dest_var(ins.dests[0], ib), b.select(t_v4, cond, one, zero), ins.dests[0]);
    }
    void emit_pow(const BcInstr &ins) {
        uint32_t a = src_mod(shuffle4(load_src(ins.srcs[0]), ins.srcs[0]), ins.srcs[0]);
        uint32_t c = src_mod(shuffle4(load_src(ins.srcs[1]), ins.srcs[1]), ins.srcs[1]);
        bool ib;
        store_masked(dest_var(ins.dests[0], ib),
                     b.ext(t_v4, G450_Pow, {b.ext(t_v4, G450_FAbs, {a}), c}), ins.dests[0]);
    }
    void emit_lit(const BcInstr &ins) {
        uint32_t s = shuffle4(load_src(ins.srcs[0]), ins.srcs[0]);
        uint32_t x = b.composite_extract(t_f32, s, {0});
        uint32_t y = b.composite_extract(t_f32, s, {1});
        uint32_t z = b.composite_extract(t_f32, s, {2});
        uint32_t zero = b.const_f32(0.0f), one = b.const_f32(1.0f);
        uint32_t zc = b.ext(t_f32, G450_FClamp, {z, b.const_f32(-128.0f), b.const_f32(128.0f)});
        uint32_t dx = b.op(spv::OpFOrdGreaterThan, t_bool, {x, zero});
        uint32_t dy = b.op(spv::OpFOrdGreaterThan, t_bool, {y, zero});
        uint32_t both = b.op(spv::OpLogicalAnd, t_bool, {dx, dy});
        uint32_t spec = b.select(t_f32, both, b.ext(t_f32, G450_Pow, {y, zc}), zero);
        uint32_t diff = b.ext(t_f32, G450_FMax, {x, zero});
        bool ib;
        store_masked(dest_var(ins.dests[0], ib),
                     b.composite_construct(t_v4, {one, diff, spec, one}), ins.dests[0]);
    }
    void emit_dst(const BcInstr &ins) {
        uint32_t a = shuffle4(load_src(ins.srcs[0]), ins.srcs[0]);
        uint32_t c = shuffle4(load_src(ins.srcs[1]), ins.srcs[1]);
        uint32_t v = b.composite_construct(t_v4, {
            b.const_f32(1.0f),
            b.fmul(t_f32, b.composite_extract(t_f32, a, {1}), b.composite_extract(t_f32, c, {1})),
            b.composite_extract(t_f32, a, {2}),
            b.composite_extract(t_f32, c, {3})});
        bool ib;
        store_masked(dest_var(ins.dests[0], ib), v, ins.dests[0]);
    }
    void emit_nrm(const BcInstr &ins) {
        uint32_t s = shuffle4(load_src(ins.srcs[0]), ins.srcs[0]);
        uint32_t s3 = b.vector_shuffle(t_v3, s, s, {0, 1, 2});
        uint32_t len = b.ext(t_f32, G450_InverseSqrt, {b.ext(t_f32, G450_FAbs, {b.dot(t_f32, s3, s3)})});
        uint32_t len3 = b.composite_construct(t_v3, {len, len, len});
        uint32_t n3 = b.fmul(t_v3, len3, s3);
        bool ib;
        store_masked(dest_var(ins.dests[0], ib),
                     b.composite_construct(t_v4, {n3, b.const_f32(0.0f)}), ins.dests[0]);
    }
    void emit_crs(const BcInstr &ins) {
        uint32_t a = shuffle4(load_src(ins.srcs[0]), ins.srcs[0]);
        uint32_t c = shuffle4(load_src(ins.srcs[1]), ins.srcs[1]);
        uint32_t m1 = b.fmul(t_v3, b.vector_shuffle(t_v3, a, a, {1, 2, 0}),
                             b.vector_shuffle(t_v3, c, c, {2, 0, 1}));
        uint32_t m2 = b.fmul(t_v3, b.vector_shuffle(t_v3, a, a, {2, 0, 1}),
                             b.vector_shuffle(t_v3, c, c, {1, 2, 0}));
        uint32_t cr = b.fsub(t_v3, m1, m2);
        bool ib;
        store_masked(dest_var(ins.dests[0], ib),
                     b.composite_construct(t_v4, {cr, b.const_f32(0.0f)}), ins.dests[0]);
    }
    void emit_sincos(const BcInstr &ins) {
        uint32_t s = shuffle4(load_src(ins.srcs[0]), ins.srcs[0]);
        uint32_t x = b.composite_extract(t_f32, s, {0});
        uint32_t sv = b.ext(t_f32, G450_Sin, {x});
        uint32_t cv = b.ext(t_f32, G450_Cos, {x});
        uint32_t zero = b.const_f32(0.0f);
        bool ib;
        store_masked(dest_var(ins.dests[0], ib),
                     b.composite_construct(t_v4, {sv, cv, zero, zero}), ins.dests[0]);
    }
    void emit_matrix(const BcInstr &ins) {
        uint32_t a = src_mod(shuffle4(load_src(ins.srcs[0]), ins.srcs[0]), ins.srcs[0]);
        const BcOperand &m = ins.srcs[1];
        uint32_t rows = (ins.op == B_M4x4 || ins.op == B_M3x4) ? 4 : (ins.op == B_M3x2 ? 2 : 3);
        uint32_t dots = (ins.op == B_M4x4 || ins.op == B_M4x3) ? 4 : 3;
        std::vector<uint32_t> comps;
        for (uint32_t r = 0; r < rows; r++) {
            uint32_t row = m.relative ? load_const_reg_rel(m.num + r) : load_const_reg({BcReg::Const, m.num + r});
            uint32_t av = dots == 4 ? a : b.vector_shuffle(t_v3, a, a, {0, 1, 2});
            uint32_t rv = dots == 4 ? row : b.vector_shuffle(t_v3, row, row, {0, 1, 2});
            comps.push_back(b.dot(t_f32, av, rv));
        }
        while (comps.size() < 4)
            comps.push_back(b.const_f32(comps.size() == 3 && rows == 3 ? 1.0f : 0.0f));
        bool ib;
        store_masked(dest_var(ins.dests[0], ib), b.composite_construct(t_v4, comps), ins.dests[0]);
    }
    uint32_t load_const_reg_rel(uint32_t num) {
        // c[aL + num] para matrizes relativas
        uint32_t idx = b.op(spv::OpIAdd, t_i32,
                            {b.composite_extract(t_i32, b.load(t_ivec4, addr_reg()), {0}),
                             b.const_i32((int32_t)num)});
        uint32_t ptr = b.access_chain(t_v4_uniform_ptr, vs_ubo, {b.const_i32(0), idx});
        return b.load(t_v4, ptr);
    }
    void emit_dp2add(const BcInstr &ins) {
        uint32_t a = shuffle4(load_src(ins.srcs[0]), ins.srcs[0]);
        uint32_t c = shuffle4(load_src(ins.srcs[1]), ins.srcs[1]);
        uint32_t s2 = shuffle4(load_src(ins.srcs[2]), ins.srcs[2]);
        uint32_t a2 = b.vector_shuffle(t_v2, a, a, {0, 1});
        uint32_t c2 = b.vector_shuffle(t_v2, c, c, {0, 1});
        uint32_t r = b.fadd(t_f32, b.dot(t_f32, a2, c2), b.composite_extract(t_f32, s2, {0}));
        uint32_t v = b.composite_construct(t_v4, {r, r, r, r});
        bool ib;
        store_masked(dest_var(ins.dests[0], ib), v, ins.dests[0]);
    }
    void emit_dsx_dsy(const BcInstr &ins, bool dy) {
        uint32_t a = shuffle4(load_src(ins.srcs[0]), ins.srcs[0]);
        bool ib;
        store_masked(dest_var(ins.dests[0], ib), b.op(dy ? spv::OpDPdy : spv::OpDPdx, t_v4, {a}),
                     ins.dests[0]);
    }
    void emit_setp(const BcInstr &ins) {
        uint32_t a = shuffle4(load_src(ins.srcs[0]), ins.srcs[0]);
        uint32_t c = shuffle4(load_src(ins.srcs[1]), ins.srcs[1]);
        store_masked(pred_reg(), compare_v4(a, c, ins.cmp), ins.dests[0], true);
    }

    // coordenada com o tamanho certo para a dimensao do sampler (2D/CUBE/3D)
    uint32_t tex_coords(uint32_t dim, uint32_t coords) {
        if (dim == 3 || dim == 4) return b.vector_shuffle(t_v3, coords, coords, {0, 1, 2});
        return b.vector_shuffle(t_v2, coords, coords, {0, 1});
    }

    // variavel -> valor (OpLoad do OpTypeSampledImage certo para a dimensao)
    uint32_t sampler_load(uint32_t idx) {
        uint32_t var = sampler_var(idx);
        uint32_t st = smp_dim[idx] == 3 ? t_simgcube : smp_dim[idx] == 4 ? t_simgvol : t_simg2d;
        return b.load(st, var);
    }

    void emit_texld(const BcInstr &ins) {
        uint32_t smp_idx = ins.op == B_TEXLDD ? 3 : 1;
        const BcOperand &smp_op = ins.srcs[smp_idx];
        uint32_t smp = sampler_load(smp_op.num);
        uint32_t coords = src_mod(shuffle4(load_src(ins.srcs[0]), ins.srcs[0]), ins.srcs[0]);
        uint32_t dim = smp_dim[smp_op.num];
        uint32_t v;
        if (ins.texld_mode == 1) {
            // proj: coordenada vec4 (u/v/q) — so 2D tem proj em D3D9
            v = b.image_sample_proj(t_v4, smp, coords);
        } else if (ins.texld_mode == 2) {
            uint32_t bias = b.composite_extract(t_f32, coords, {3});
            v = b.image_sample_bias(t_v4, smp, tex_coords(dim, coords), bias);
        } else if (ins.op == B_TEXLDL) {
            uint32_t lod = b.composite_extract(t_f32, coords, {3});
            v = b.image_sample_explicit_lod(t_v4, smp, tex_coords(dim, coords), lod);
        } else if (ins.op == B_TEXLDD) {
            uint32_t dx = shuffle4(load_src(ins.srcs[1]), ins.srcs[1]);
            uint32_t dy = shuffle4(load_src(ins.srcs[2]), ins.srcs[2]);
            v = b.image_sample_grad(t_v4, smp, tex_coords(dim, coords),
                                    tex_coords(dim, dx), tex_coords(dim, dy));
        } else {
            v = b.image_sample_implicit(t_v4, smp, tex_coords(dim, coords));
        }
        bool ib;
        store_masked(dest_var(ins.dests[0], ib), v, ins.dests[0]);
    }

    void emit_texkill(const BcInstr &ins) {
        uint32_t v = shuffle4(load_src(ins.dests[0]), ins.dests[0]);
        uint32_t zero = splat(0.0f);
        uint32_t lt = b.op(spv::OpFOrdLessThan, t_bvec4, {v, zero});
        uint32_t any3 = b.op(spv::OpAny, t_bool,
                             {b.vector_shuffle(t_bvec4, lt, lt, {0, 1, 2, 0})});
        uint32_t merge = b.label(), kill_block = b.label();
        b.selection_merge(merge);
        b.branch_cond(any3, kill_block, merge);
        b.label_bind(kill_block);
        b.kill();
        b.label_bind(merge);
    }

    // ------------------------------------------------------ fluxo
    uint32_t loop_source_f4(const BcOperand &o) {
        // count/inicio/step: i# (int) ou c# (float) ou DEFI/DEF shadow
        if (o.type == BcReg::ConstInt) {
            ensure_ib_ubo();
            uint32_t ptr = b.access_chain(b.type_pointer(spv::StorageClassUniform, t_ivec4),
                                          ib_ubo, {b.const_i32(0), b.const_i32((int32_t)o.num)});
            return b.op(spv::OpConvertSToF, t_v4, b.load(t_ivec4, ptr));
        }
        auto it = def_i.find(o.num);
        if (it != def_i.end()) return b.op(spv::OpConvertSToF, t_v4, it->second);
        return load_const_reg(o);
    }

    void emit_if(const BcInstr &ins, bool is_cmp) {
        uint32_t merge = b.label(), tt = b.label(), ff = b.label();
        uint32_t cond;
        if (!is_cmp) {
            cond = bool_of_const(ins.srcs[0]);
        } else {
            uint32_t a = shuffle4(load_src(ins.srcs[0]), ins.srcs[0]);
            uint32_t c = shuffle4(load_src(ins.srcs[1]), ins.srcs[1]);
            uint32_t condv = compare_v4(a, c, ins.cmp);   // comparacao do token de instrucao
            cond = b.op(spv::OpAny, t_bool, {condv});
        }
        b.selection_merge(merge);
        b.branch_cond(cond, tt, ff);
        b.label_bind(tt);
        if_stack.push_back({merge, ff, false, false});
    }
    void emit_else() {
        if (if_stack.empty()) { fail("ELSE sem IF"); return; }
        auto &p = if_stack.back();
        b.branch(p.merge);
        b.label_bind(p.else_block);
        p.has_else = true;
    }
    void emit_endif() {
        if (if_stack.empty()) { fail("ENDIF sem IF"); return; }
        auto p = if_stack.back();
        if_stack.pop_back();
        if (p.has_else) {
            // o corpo do else esta em emissao: termina ele
            b.branch(p.merge);
        } else {
            // sem else: o branch falso do IF aponta para um bloco vazio que
            // so vai para o merge (o alvo existe e e definido)
            b.branch(p.merge);          // termina o caminho verdadeiro
            b.label_bind(p.else_block);
            b.branch(p.merge);
        }
        b.label_bind(p.merge);
        // se a selection continha BREAK, checa o flag aqui (em cascata: o
        // target e o merge da selection de fora, ou o merge do loop).
        if (p.had_break && !loop_merges.empty()) {
            uint32_t brk = b.load(t_bool, brk_flag());
            uint32_t after = b.label();
            uint32_t target;
            if (!if_stack.empty()) {
                target = if_stack.back().merge;
                if_stack.back().had_break = true;
            } else {
                target = loop_merges.back();
            }
            b.selection_merge(after);
            b.branch_cond(brk, target, after);
            b.label_bind(after);
        }
    }

    // loop: src0 = i#/c# com x=count, y=inicio, z=step
    void emit_loop(const BcInstr &ins) {
        uint32_t merge = b.label(), cont = b.label(), header = b.label(), body = b.label(),
                 body_next = b.label();
        uint32_t iter = b.variable(spv::StorageClassPrivate, t_i32, b.const_i32(0));
        uint32_t srcf = loop_source_f4(ins.srcs[0]);
        uint32_t count = b.op(spv::OpConvertFToS, t_i32, b.composite_extract(t_f32, srcf, {0}));
        uint32_t start = b.op(spv::OpConvertFToS, t_i32, b.composite_extract(t_f32, srcf, {1}));
        uint32_t step = b.op(spv::OpConvertFToS, t_i32, b.composite_extract(t_f32, srcf, {2}));
        b.store(addr_reg(), b.composite_construct(t_ivec4, {start, b.const_i32(0), b.const_i32(0), b.const_i32(0)}));
        b.store(iter, b.const_i32(0));
        // flag de break deste loop (zero a cada entrada)
        uint32_t flag = b.variable(spv::StorageClassPrivate, t_bool, b.const_bool(false));
        b.store(flag, b.const_bool(false));

        b.branch(header);   // termina o bloco corrente (spec: bloco precisa de terminador)
        b.label_bind(header);
        b.loop_merge(merge, cont);
        b.branch(body);
        b.label_bind(body);
        uint32_t cond = b.op(spv::OpSLessThan, t_bool, {b.load(t_i32, iter), count});
        // a checagem usa selection merge PROPRIO; dele sai um branch simples
        // para o merge do loop (padrao dos compiladores para while)
        uint32_t check_merge = b.label();
        b.selection_merge(check_merge);
        b.branch_cond(cond, body_next, check_merge);
        b.label_bind(check_merge);
        b.branch(merge);
        b.label_bind(body_next);
        loop_merges.push_back(merge);
        loop_iters.push_back({iter, step, header, cont});
        brk_flags.push_back(flag);
    }
    struct LoopState { uint32_t iter, step, header, cont; };
    std::vector<LoopState> loop_iters;
    std::vector<uint32_t> brk_flags;

    uint32_t brk_flag() {
        // dentro de loop: flag do loop mais interno (BREAK so existe em loop);
        // emit_break/BREAKP ja falham antes de chegar aqui sem loop)
        if (brk_flags.empty()) return b.variable(spv::StorageClassPrivate, t_bool, b.const_bool(false));
        return brk_flags.back();
    }

    void emit_endloop() {
        if (loop_merges.empty()) { fail("ENDLOOP sem LOOP"); return; }
        uint32_t merge = loop_merges.back();
        auto st = loop_iters.back();
        loop_merges.pop_back();
        loop_iters.pop_back();
        if (!brk_flags.empty()) brk_flags.pop_back();
        b.branch(st.cont);
        b.label_bind(st.cont);
        // aL += step; iter++
        uint32_t al = b.load(t_ivec4, addr_reg());
        uint32_t alx = b.composite_extract(t_i32, al, {0});
        uint32_t nal = b.composite_construct(t_ivec4,
            {b.op(spv::OpIAdd, t_i32, {alx, st.step}), b.const_i32(0), b.const_i32(0), b.const_i32(0)});
        b.store(addr_reg(), nal);
        b.store(st.iter, b.op(spv::OpIAdd, t_i32, {b.load(t_i32, st.iter), b.const_i32(1)}));
        b.branch(st.header);
        b.label_bind(merge);
    }

    void emit_break(bool conditional, const BcInstr &ins) {
        if (loop_merges.empty()) { fail("BREAK fora de loop"); return; }
        // BREAK dentro de selection nao pode pular direto para o merge do loop
        // (SPIR-V estruturado): marca o flag do loop e devolve ao merge da
        // selection mais interna; o emit_endif propaga a saida em cascata.
        bool in_sel = !if_stack.empty();
        if (!conditional) {
            if (in_sel) {
                b.store(brk_flag(), b.const_bool(true));
                if_stack.back().had_break = true;
                b.branch(if_stack.back().merge);
            } else {
                b.branch(loop_merges.back());
            }
            b.label_bind(b.label());   // bloco morto (codigos seguintes caem aqui)
            return;
        }
        uint32_t a = shuffle4(load_src(ins.srcs[0]), ins.srcs[0]);
        uint32_t c = shuffle4(load_src(ins.srcs[1]), ins.srcs[1]);
        uint32_t condv = compare_v4(a, c, ins.cmp);
        uint32_t cond = b.op(spv::OpAny, t_bool, {condv});
        uint32_t dead = b.label();
        uint32_t target;
        if (in_sel) {
            b.store(brk_flag(), b.const_bool(true));
            if_stack.back().had_break = true;
            target = if_stack.back().merge;
        } else {
            target = loop_merges.back();
        }
        b.branch_cond(cond, target, dead);
        b.label_bind(dead);
    }

    void emit_rep(const BcInstr &ins) {
        // rep c#.x: loop sem aL
        uint32_t merge = b.label(), cont = b.label(), header = b.label(), body = b.label(),
                 body_next = b.label();
        uint32_t iter = b.variable(spv::StorageClassPrivate, t_i32, b.const_i32(0));
        uint32_t srcf = loop_source_f4(ins.srcs[0]);
        uint32_t count = b.op(spv::OpConvertFToS, t_i32, b.composite_extract(t_f32, srcf, {0}));
        b.store(iter, b.const_i32(0));
        b.branch(header);
        b.label_bind(header);
        b.loop_merge(merge, cont);
        b.branch(body);
        b.label_bind(body);
        uint32_t cond = b.op(spv::OpSLessThan, t_bool, {b.load(t_i32, iter), count});
        uint32_t check_merge = b.label();
        b.selection_merge(check_merge);
        b.branch_cond(cond, body_next, check_merge);
        b.label_bind(check_merge);
        b.branch(merge);
        b.label_bind(body_next);
        loop_merges.push_back(merge);
        loop_iters.push_back({iter, b.const_i32(0), header, cont});
    }

    // ------------------------------------------------------ dcl
    void process_dcl(const BcInstr &ins) {
        const BcOperand &o = ins.dests[0];
        if (o.type == BcReg::Sampler) {
            if (o.num < 16) smp_dim[o.num] = ins.dcl_sampler_type;
        } else if (o.type == BcReg::Input) {
            dcl_loc[o.num] = usage_to_location(ins.dcl_usage, ins.dcl_usage_index);
        } else if (o.type == BcReg::Output) {
            if (is_vs) {
                uint32_t usage = ins.dcl_usage;
                if (usage == D3DDECLUSAGE_POSITION && ins.dcl_usage_index == 0) {
                    dcl_loc[o.num] = kLocBuiltinPosition;   // position -> builtin
                } else {
                    dcl_loc[o.num] = usage_to_location(usage, ins.dcl_usage_index);
                }
            }
        }
    }

    // ------------------------------------------------------ driver geral
    // Traduz a sequencia de instrucoes do bloco atual. Retorna false em erro.
    bool emit_block(const std::vector<BcInstr> &instrs, size_t begin, size_t end) {
        for (size_t i = begin; i < end; i++) {
            const BcInstr &ins = instrs[i];
            switch (ins.op) {
            case B_NOP: break;
            case B_MOV: emit_load(ins); break;
            case B_ADD: emit_binop(ins, spv::OpFAdd); break;
            case B_SUB: emit_binop(ins, spv::OpFSub); break;
            case B_MUL: emit_binop(ins, spv::OpFMul); break;
            case B_MAD: emit_mad(ins); break;
            case B_DP3: emit_dot(ins, 3); break;
            case B_DP4: emit_dot(ins, 4); break;
            case B_MIN: emit_minmax(ins, false); break;
            case B_MAX: emit_minmax(ins, true); break;
            case B_SLT: emit_slt_sge(ins, false); break;
            case B_SGE: emit_slt_sge(ins, true); break;
            case B_EXP: emit_scalar_fn(ins, G450_Exp2); break;
            case B_LOG: emit_scalar_fn(ins, G450_Log2); break;
            case B_LIT: emit_lit(ins); break;
            case B_DST: emit_dst(ins); break;
            case B_LRP: emit_lrp(ins); break;
            case B_FRC: emit_scalar_fn(ins, G450_Fract); break;
            case B_M4x4: case B_M4x3: case B_M3x4: case B_M3x3: case B_M3x2: emit_matrix(ins); break;
            case B_POW: emit_pow(ins); break;
            case B_CRS: emit_crs(ins); break;
            case B_SGN: emit_scalar_fn(ins, G450_FSign); break;
            case B_ABS: emit_scalar_fn(ins, G450_FAbs); break;
            case B_NRM: emit_nrm(ins); break;
            case B_SINCOS: emit_sincos(ins); break;
            case B_RCP: emit_rcp(ins); break;
            case B_RSQ: emit_rsq(ins); break;
            case B_CMP: emit_cmp_sel(ins); break;
            case B_CND: emit_cnd(ins); break;
            case B_DP2ADD: emit_dp2add(ins); break;
            case B_DSX: emit_dsx_dsy(ins, false); break;
            case B_DSY: emit_dsx_dsy(ins, true); break;
            case B_MOVA: emit_load(ins); break;
            case B_SETP: emit_setp(ins); break;
            case B_TEXLD: case B_TEXLDL: case B_TEXLDD: {
                if (is_vs && ins.op != B_TEXLDL) { fail("texld em VS (so texldl no vs_3_0)"); return false; }
                emit_texld(ins);
                break;
            }
            case B_TEXKILL: emit_texkill(ins); break;
            case B_IF: emit_if(ins, false); break;
            case B_IFC: emit_if(ins, true); break;
            case B_ELSE: emit_else(); break;
            case B_ENDIF: emit_endif(); break;
            case B_LOOP: emit_loop(ins); break;
            case B_ENDLOOP: emit_endloop(); break;
            case B_REP: emit_rep(ins); break;
            case B_ENDREP: emit_endloop(); break;
            case B_BREAK: emit_break(false, ins); break;
            case B_BREAKC: emit_break(true, ins); break;
            case B_BREAKP: {
                if (loop_merges.empty()) { fail("BREAKP fora de loop"); return false; }
                uint32_t pv = b.load(t_bvec4, pred_reg());
                uint32_t px = b.composite_extract(t_bool, pv, {0});
                uint32_t dead = b.label();
                uint32_t target;
                if (!if_stack.empty()) {
                    b.store(brk_flag(), b.const_bool(true));
                    if_stack.back().had_break = true;
                    target = if_stack.back().merge;
                } else {
                    target = loop_merges.back();
                }
                b.branch_cond(px, target, dead);
                b.label_bind(dead);
                break;
            }
            case B_CALL: {
                auto it = label_fn.find(ins.srcs[0].num);
                if (it == label_fn.end()) { fail("CALL para label ausente"); return false; }
                b.function_call(it->second);
                break;
            }
            case B_CALLNZ: {
                auto it = label_fn.find(ins.srcs[0].num);
                if (it == label_fn.end()) { fail("CALLNZ para label ausente"); return false; }
                uint32_t cond = bool_of_const(ins.srcs[1]);
                uint32_t merge = b.label(), do_call = b.label();
                b.selection_merge(merge);
                b.branch_cond(cond, do_call, merge);
                b.label_bind(do_call);
                b.function_call(it->second);
                b.branch(merge);
                b.label_bind(merge);
                break;
            }
            case B_RET: return true;   // fim do bloco (caller decide)
            case B_LABEL: fail("LABEL no meio do bloco"); return false;
            case B_DCL: break;   // processado antes
            case B_DEF: case B_DEFI: case B_DEFB: break;   // shadow preenchido no prepass
            case B_TEXCOORD: fail("TEXCOORD (SM1.x)"); return false;
            default:
                fail(std::string("opcode nao implementado: ") + bc_op_name(ins.op));
                return false;
            }
            if (!error.empty()) return false;
        }
        return true;
    }

    void process_prepass(const std::vector<BcInstr> &instrs) {
        for (const auto &ins : instrs) {
            switch (ins.op) {
            case B_DCL: process_dcl(ins); break;
            case B_DEF: {
                // DEF c#: constantes baked no bytecode — shadow lido direto
                uint32_t c = b.const_vec(t_v4, {b.const_f32(ins.deff[0]), b.const_f32(ins.deff[1]),
                                                b.const_f32(ins.deff[2]), b.const_f32(ins.deff[3])});
                def_f[ins.dests[0].num] = c;
                break;
            }
            case B_DEFI: {
                ensure_ib_ubo();
                // DEFI shadow: i# como constante ivec4 no modulo
                uint32_t c = b.const_vec(t_ivec4, {b.const_i32(ins.defi[0]), b.const_i32(ins.defi[1]),
                                                   b.const_i32(ins.defi[2]), b.const_i32(ins.defi[3])});
                def_i[ins.dests[0].num] = c;
                break;
            }
            case B_DEFB: {
                ensure_ib_ubo();
                def_b[ins.dests[0].num] = b.const_bool(ins.defb != 0);
                break;
            }
            default: break;
            }
        }
    }

    // ordena subrotinas (DFS post-order a partir do main)
    bool order_labels(const std::vector<BcInstr> &instrs) {
        // main = instrucoes ate o primeiro LABEL; labels seguem
        std::unordered_map<uint32_t, std::pair<size_t, size_t>> blocks;
        size_t main_end = instrs.size();
        for (size_t i = 0; i < instrs.size(); i++) {
            if (instrs[i].op == B_LABEL) {
                if (main_end == (size_t)instrs.size()) main_end = i;
                uint32_t label_num = instrs[i].srcs[0].num;
                size_t start = i + 1;
                size_t stop = instrs.size();
                for (size_t j = i + 1; j < instrs.size(); j++)
                    if (instrs[j].op == B_LABEL) { stop = j; break; }
                blocks[label_num] = {start, stop};
            }
        }
        // DFS de calls a partir do main (pos-ordem: callee antes do caller)
        std::set<uint32_t> emitted;
        std::function<bool(uint32_t, int)> visit = [&](uint32_t label, int depth) {
            if (depth > 64) { error = "profundidade de call excessiva"; return false; }
            if (emitted.count(label)) return true;
            auto it = blocks.find(label);
            if (it == blocks.end()) { error = "CALL para label inexistente"; return false; }
            // visita calos primeiro (pos-ordem)
            for (size_t i = it->second.first; i < it->second.second; i++) {
                const BcInstr &ins = instrs[i];
                if (ins.op == B_CALL || ins.op == B_CALLNZ) {
                    if (!visit(ins.srcs[0].num, depth + 1)) return false;
                }
            }
            emitted.insert(label);
            call_order.push_back(label);
            return true;
        };
        for (size_t i = 0; i < main_end; i++) {
            const BcInstr &ins = instrs[i];
            if (ins.op == B_CALL || ins.op == B_CALLNZ) {
                if (!visit(ins.srcs[0].num, 0)) return false;
            }
        }
        // labels nao chamados: emite tambem (codigo morto legal)
        for (auto &kv : blocks) visit(kv.first, 0);
        m_blocks = std::move(blocks);
        m_main_end = main_end;
        return true;
    }

    std::unordered_map<uint32_t, std::pair<size_t, size_t>> m_blocks;
    size_t m_main_end = 0;

    bool run(XlateOut &out) {
        init();
        start_module();
        declare_ubos();
        process_prepass(sh.instrs);
        if (!order_labels(sh.instrs)) return false;
        if (sh.uses_ib) ensure_ib_ubo();

        // subrotinas
        for (uint32_t label : call_order) {
            uint32_t fn = b.id();
            label_fn[label] = fn;
        }
        for (uint32_t label : call_order) {
            auto blk = m_blocks[label];
            b.function_begin(label_fn[label], t_void, t_fn_void);
            if (!emit_block(sh.instrs, blk.first, blk.second)) return false;
            b.ret();
            b.function_end();
        }

        // main
        b.function_begin(b.entry_id(), t_void, t_fn_void);
        if (!emit_block(sh.instrs, 0, m_main_end)) return false;

        // alpha test (pos-PS, semantica D3D9)
        if (alpha_test && !is_vs) {
            uint32_t col = output_var(0);
            uint32_t c = b.load(t_v4, col);
            uint32_t sys_ptr = b.access_chain(t_v4_uniform_ptr, ps_ubo, {b.const_i32(1)});
            uint32_t sys = b.load(t_v4, sys_ptr);
            uint32_t a = b.composite_extract(t_f32, c, {3});
            uint32_t ref = b.composite_extract(t_f32, sys, {0});
            uint32_t cond = compare_scalar(a, ref, alpha_cmp);
            uint32_t merge = b.label(), kill_block = b.label();
            b.selection_merge(merge);
            b.branch_cond(cond, kill_block, merge);
            b.label_bind(kill_block);
            b.kill();
            b.label_bind(merge);
        }
        b.ret();
        b.function_end();
        out.spirv = b.finish();
        out.max_const = sh.max_const;
        out.sampler_mask = sh.sampler_mask;
        out.uses_ib = !def_i.empty() || !def_b.empty() || uses_ib_regs() || sh.uses_ib;
        return true;
    }

    bool uses_ib_regs() const {
        for (const auto &ins : sh.instrs) {
            for (auto &o : ins.srcs)
                if (o.type == BcReg::ConstInt || o.type == BcReg::ConstBool) return true;
        }
        return false;
    }

    uint32_t compare_scalar(uint32_t a, uint32_t c, uint32_t d3dspc) {
        switch (d3dspc) {
        case 1: return b.op(spv::OpFOrdLessThan, t_bool, {a, c});
        case 2: return b.op(spv::OpFOrdEqual, t_bool, {a, c});
        case 3: return b.op(spv::OpFOrdLessThanEqual, t_bool, {a, c});
        case 4: return b.op(spv::OpFOrdGreaterThan, t_bool, {a, c});
        case 5: return b.op(spv::OpFOrdNotEqual, t_bool, {a, c});
        case 6: return b.op(spv::OpFOrdGreaterThanEqual, t_bool, {a, c});
        case 7: return b.const_bool(true);
        case 0: return b.const_bool(false);
        default: return b.op(spv::OpFOrdLessThan, t_bool, {a, c});
        }
    }
};

} // namespace

// mapa fixo usage/index -> location (contrato em nvr_compile.h).POSITION vira
// builtin no VS (kLocBuiltinPosition) e nao passa por aqui.
uint32_t usage_to_location(uint32_t usage, uint32_t index) {
    if (index == 0) {
        switch (usage) {
        case D3DDECLUSAGE_POSITION: return kLocPosition;
        case D3DDECLUSAGE_BLENDWEIGHT: return kLocBlendWeight;
        case D3DDECLUSAGE_BLENDINDICES: return kLocBlendIndices;
        case D3DDECLUSAGE_NORMAL: return kLocNormal;
        case D3DDECLUSAGE_PSIZE: return kLocPointSize;
        case D3DDECLUSAGE_TEXCOORD: return kLocTexcoord0;
        case D3DDECLUSAGE_TANGENT: return kLocTangent;
        case D3DDECLUSAGE_BINORMAL: return kLocBinormal;
        case D3DDECLUSAGE_TESSFACTOR: return kLocTessfactor;
        case D3DDECLUSAGE_POSITIONT: return kLocPositionT;
        case D3DDECLUSAGE_COLOR: return kLocColor0;
        case D3DDECLUSAGE_FOG: return kLocFog;
        case D3DDECLUSAGE_DEPTH: return kLocDepth;
        case D3DDECLUSAGE_SAMPLE: return kLocSample;
        }
    } else {
        switch (usage) {
        case D3DDECLUSAGE_POSITION: return kLocPositionIdxBase + index;  // 33..35
        case D3DDECLUSAGE_NORMAL: return kLocNormalIdxBase + index;      // 37..39
        case D3DDECLUSAGE_TEXCOORD:
            if (index < 8) return kLocTexcoord0 + index;
            break;
        case D3DDECLUSAGE_COLOR:
            if (index == 1) return kLocColor1;
            break;
        }
    }
    // combinacao incomum: location reservada estavel (colisao teorica so com
    // declaracoes que o jogo nao usa; o log do draw mostraria)
    return 31;
}


bool translate_vs(const VsXlateIn &in, XlateOut &out) {
    if (in.sh->dynamic_sampler) {
        out.error = "indexacao dinamica de sampler (s[aL]) nao suportada";
        return false;
    }
    Ctx ctx(*in.sh, true, in.in_comps, in.flat_shade, false, 0);
    if (!ctx.run(out)) { out.error = ctx.error; return false; }
    return true;
}

bool translate_ps(const PsXlateIn &in, XlateOut &out) {
    if (in.sh->dynamic_sampler) {
        out.error = "indexacao dinamica de sampler (s[aL]) nao suportada";
        return false;
    }
    Ctx ctx(*in.sh, false, nullptr, false, in.alpha_test, in.alpha_cmp);
    if (!ctx.run(out)) { out.error = ctx.error; return false; }
    return true;
}

} // namespace nvr
