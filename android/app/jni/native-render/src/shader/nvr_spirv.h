// Emissor SPIR-V binario (SPIR-V 1.0, suficiente para Vulkan 1.1).
// Mantem as secoes do layout logico do modulo separadas e as concatena no
// finish(), com OpEntryPoint/OpExecutionMode gerados ali (a interface so e
// conhecida no fim). Usado pelos tradutores de bytecode e pelo fixed-function.
#pragma once
#include "../nvr_common.h"
#include <spirv/unified1/spirv.hpp>


namespace nvr {

class SpirvBuilder {
public:
    SpirvBuilder() = default;

    void start(spv::ExecutionModel model, const char *entry_name, uint32_t spirv_version = 0x00010000);
    // (entry point/exec modes/debug/annot/types/funcs sao montados no finish)

    std::vector<uint32_t> finish();

    uint32_t id() { return m_next_id++; }
    uint32_t entry_id() const { return m_entry; }
    uint32_t std450_id() const { return m_std450; }

    void capability(spv::Capability c);
    void ext_import(const char *name);
    void memory_model(spv::AddressingModel a, spv::MemoryModel m);
    void execution_mode(spv::ExecutionMode m, const std::vector<uint32_t> &literals = {});

    void decorate(uint32_t target, spv::Decoration d, const std::vector<uint32_t> &literals = {});
    void member_decorate(uint32_t target, uint32_t member, spv::Decoration d, uint32_t literal);
    void name(uint32_t target, const char *n);

    uint32_t type_void();
    uint32_t type_bool();
    uint32_t type_float();
    uint32_t type_int(bool signed_);
    uint32_t type_vec(uint32_t comp_type, uint32_t n);
    uint32_t type_mat(uint32_t vec_type, uint32_t cols);
    uint32_t type_array(uint32_t elem, uint32_t length_const_id);
    // array de UBO (std140): decora ArrayStride na criacao (stride em bytes)
    uint32_t type_array_ubo(uint32_t elem, uint32_t len_const_id, uint32_t stride);
    uint32_t type_struct(const std::vector<uint32_t> &members);
    uint32_t type_sampler();
    uint32_t type_image(uint32_t sampled_type, spv::Dim dim, bool depth, bool arrayed, bool ms,
                        uint32_t sampled, spv::ImageFormat fmt);
    uint32_t type_sampled_image(uint32_t image);
    uint32_t type_function(uint32_t ret, const std::vector<uint32_t> &params);
    uint32_t type_pointer(spv::StorageClass sc, uint32_t base);

    uint32_t const_u32(uint32_t v);
    uint32_t const_i32(int32_t v);
    uint32_t const_f32(float v);
    uint32_t const_bool(bool v);
    uint32_t const_vec(uint32_t type, const std::vector<uint32_t> &comps);
    uint32_t const_null(uint32_t type);

    uint32_t variable(spv::StorageClass sc, uint32_t type, uint32_t init_const = 0);

    void function_begin(uint32_t fn_id, uint32_t ret_type, uint32_t fn_type);
    void function_end();

    uint32_t load(uint32_t type, uint32_t ptr);
    void store(uint32_t ptr, uint32_t value);
    uint32_t access_chain(uint32_t ptr_type, uint32_t base, const std::vector<uint32_t> &indices);
    uint32_t composite_construct(uint32_t type, const std::vector<uint32_t> &parts);
    uint32_t composite_extract(uint32_t type, uint32_t composite, const std::vector<uint32_t> &idx);
    uint32_t vector_shuffle(uint32_t type, uint32_t v1, uint32_t v2, const std::vector<uint32_t> &idx);
    uint32_t op(spv::Op opcode, uint32_t type, const std::vector<uint32_t> &operands);
    uint32_t op(spv::Op opcode, uint32_t type, uint32_t single_operand) {
        return op(opcode, type, std::vector<uint32_t>{single_operand});
    }
    void op0(spv::Op opcode, const std::vector<uint32_t> &operands);

    // conveniencias de ALU usadas pelos tradutores
    uint32_t dot(uint32_t t, uint32_t a, uint32_t b) { return op(spv::OpDot, t, {a, b}); }
    uint32_t fmul(uint32_t t, uint32_t a, uint32_t b) { return op(spv::OpFMul, t, {a, b}); }
    uint32_t fadd(uint32_t t, uint32_t a, uint32_t b) { return op(spv::OpFAdd, t, {a, b}); }
    uint32_t fsub(uint32_t t, uint32_t a, uint32_t b) { return op(spv::OpFSub, t, {a, b}); }
    uint32_t fdiv(uint32_t t, uint32_t a, uint32_t b) { return op(spv::OpFDiv, t, {a, b}); }
    uint32_t fneg(uint32_t t, uint32_t a) { return op(spv::OpFNegate, t, {a}); }
    uint32_t fma(uint32_t t, uint32_t a, uint32_t b, uint32_t c) {
        return ext(t, 50 /*GLSL.std.450 Fma*/, {a, b, c});
    }
    uint32_t select(uint32_t t, uint32_t cond, uint32_t a, uint32_t b) {
        return op(spv::OpSelect, t, {cond, a, b});
    }
    // select com resultado VETORIAL e condicao escalar: replana a condicao para
    // bvecN (SPIR-V 1.0/Vulkan 1.1 nao aceita scalar cond + vector result)
    uint32_t selectv(uint32_t t, uint32_t n, uint32_t cond, uint32_t a, uint32_t b) {
        if (n == 1) return select(t, cond, a, b);
        uint32_t bt = type_vec(type_bool(), n);
        std::vector<uint32_t> comps(n, cond);
        uint32_t bv = composite_construct(bt, comps);
        return op(spv::OpSelect, t, {bv, a, b});
    }
    uint32_t ext(uint32_t type, uint32_t op_code, const std::vector<uint32_t> &ops);
    uint32_t function_call(uint32_t fn_id);

    uint32_t label();
    void label_bind(uint32_t lbl);
    void branch(uint32_t target);
    void branch_cond(uint32_t cond, uint32_t t, uint32_t f);
    void loop_merge(uint32_t merge, uint32_t cont);
    void selection_merge(uint32_t merge);
    void ret();
    void kill();

    void add_interface_var(spv::StorageClass sc, uint32_t var_id);

    uint32_t image_sample_implicit(uint32_t type4, uint32_t sampled_img, uint32_t coords);
    uint32_t image_sample_proj(uint32_t type4, uint32_t sampled_img, uint32_t coords);
    uint32_t image_sample_explicit_lod(uint32_t type4, uint32_t sampled_img, uint32_t coords, uint32_t lod);
    uint32_t image_sample_bias(uint32_t type4, uint32_t sampled_img, uint32_t coords, uint32_t bias);
    uint32_t image_sample_grad(uint32_t type4, uint32_t sampled_img, uint32_t coords, uint32_t dx, uint32_t dy);

    // criacao de funcoes adicionais (subrotinas)
    uint32_t make_function_type() { return type_function(type_void(), {}); }

private:
    enum class Sec { Meta, Entry, Debug, Annot, Types, Funcs };
    void emit_to(Sec s, spv::Op opcode, const std::vector<uint32_t> &operands);
    void emit(spv::Op opcode, const std::vector<uint32_t> &operands);      // -> secao corrente
    void emit_result(spv::Op opcode, uint32_t rt, uint32_t id, const std::vector<uint32_t> &operands);
    void emit_type_result(spv::Op opcode, uint32_t id, const std::vector<uint32_t> &operands);
    Sec m_cur = Sec::Meta;

    std::vector<uint32_t> m_meta, m_entry_vec, m_debug, m_annot, m_types, m_funcs, m_exec_modes;
    std::string m_entry_name;
    uint32_t m_next_id = 1;
    uint32_t m_entry = 0;
    uint32_t m_std450 = 0;
    spv::ExecutionModel m_model = spv::ExecutionModelVertex;
    std::vector<uint32_t> m_iface;
    uint32_t m_void_t = 0, m_bool_t = 0, m_f32_t = 0, m_i32_t = 0, m_u32_t = 0, m_sampler_t = 0;
    std::unordered_map<uint32_t, uint32_t> m_vec_t, m_mat_t, m_img_t, m_simg_t;
    std::unordered_map<uint64_t, uint32_t> m_ptr_t, m_const_f, m_const_i, m_const_u;
};

// Ids reais do GLSL.std.450 (spec 1.17)
enum Glsl450 {
    G450_Round = 1, G450_Trunc = 3, G450_FAbs = 4, G450_SAbs = 5, G450_FSign = 6,
    G450_Floor = 8, G450_Ceil = 9, G450_Fract = 10, G450_Sin = 13, G450_Cos = 14,
    G450_Exp = 27, G450_Log = 28, G450_Exp2 = 29, G450_Log2 = 30, G450_Sqrt = 31,
    G450_InverseSqrt = 32, G450_Pow = 26, G450_FMin = 37, G450_SMin = 39,
    G450_FMax = 40, G450_FClamp = 43, G450_FMix = 46, G450_Step = 48,
    G450_SmoothStep = 49, G450_Fma = 50
};

} // namespace nvr
