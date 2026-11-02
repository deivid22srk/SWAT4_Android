#include "nvr_spirv.h"
#include <spirv/unified1/spirv.h>

namespace nvr {

void SpirvBuilder::emit_to(Sec s, spv::Op opcode, const std::vector<uint32_t> &operands) {
    std::vector<uint32_t> &out =
        s == Sec::Meta ? m_meta : s == Sec::Entry ? m_entry_vec : s == Sec::Debug ? m_debug
        : s == Sec::Annot ? m_annot : s == Sec::Types ? m_types : m_funcs;
    out.push_back((1 + operands.size()) << 16 | (uint32_t)opcode);
    for (uint32_t o : operands) out.push_back(o);
}

void SpirvBuilder::emit(spv::Op opcode, const std::vector<uint32_t> &operands) {
    // roteamento por classe de opcode
    switch (opcode) {
    case spv::OpCapability:
    case spv::OpExtInstImport:
    case spv::OpMemoryModel:
        emit_to(Sec::Meta, opcode, operands);
        return;
    case spv::OpName:
        emit_to(Sec::Debug, opcode, operands);
        return;
    case spv::OpDecorate:
    case spv::OpMemberDecorate:
        emit_to(Sec::Annot, opcode, operands);
        return;
    case spv::OpTypeVoid: case spv::OpTypeBool: case spv::OpTypeInt: case spv::OpTypeFloat:
    case spv::OpTypeVector: case spv::OpTypeMatrix: case spv::OpTypeArray:
    case spv::OpTypeRuntimeArray: case spv::OpTypeStruct: case spv::OpTypeSampler:
    case spv::OpTypeImage: case spv::OpTypeSampledImage: case spv::OpTypeFunction:
    case spv::OpTypePointer: case spv::OpConstant: case spv::OpConstantTrue:
    case spv::OpConstantFalse: case spv::OpConstantComposite: case spv::OpConstantNull:
    case spv::OpVariable:   // (Private/Uniform/Input/Output; Function vars nao sao criadas aqui)
        emit_to(Sec::Types, opcode, operands);
        return;
    default: break;
    }
    emit_to(m_cur, opcode, operands);
}

void SpirvBuilder::emit_result(spv::Op opcode, uint32_t rt, uint32_t id, const std::vector<uint32_t> &operands) {
    std::vector<uint32_t> ops;
    ops.push_back(rt);
    ops.push_back(id);
    for (uint32_t o : operands) ops.push_back(o);
    emit(opcode, ops);
}

// Opcodes de TIPO e OpLabel nao tem slot de result type (spec: <opcode>
// <result id> <operands>); emit_result escreveria um 0 falso no lugar.
void SpirvBuilder::emit_type_result(spv::Op opcode, uint32_t id, const std::vector<uint32_t> &operands) {
    std::vector<uint32_t> ops;
    ops.push_back(id);
    for (uint32_t o : operands) ops.push_back(o);
    emit(opcode, ops);
}

void SpirvBuilder::start(spv::ExecutionModel model, const char *entry_name, uint32_t) {
    m_model = model;
    // o cabecalho (magic/version/bound/schema) e montado no finish(); aqui so
    // guardamos o modelo e reservamos o id do entry point.
    m_entry_name = entry_name;
    m_entry = id();
}

// Literal String SPIR-V: NUL-terminado e padded ate word (spec 2.2.1)
static void push_spv_string(std::vector<uint32_t> &out, const char *s) {
    uint32_t wl = (uint32_t)strlen(s);
    uint32_t words = wl / 4 + 1;   // sempre inclui o terminador NUL
    for (uint32_t i = 0; i < words; i++) {
        uint32_t w = 0;
        for (uint32_t ch = 0; ch < 4; ch++)
            if (i * 4 + ch < wl) w |= (uint32_t)(uint8_t)s[i * 4 + ch] << (8 * ch);
        out.push_back(w);
    }
}

std::vector<uint32_t> SpirvBuilder::finish() {
    // entry point + execution modes (model, id, NOME, interface — ordem do spec)
    std::vector<uint32_t> ep{(uint32_t)m_model, m_entry};
    push_spv_string(ep, m_entry_name.c_str());
    for (uint32_t v : m_iface) ep.push_back(v);
    m_entry_vec.insert(m_entry_vec.begin(), ep.begin(), ep.end());
    // entry sec começa com o opcode: prefixar wc+opcode
    std::vector<uint32_t> entry_final;
    entry_final.push_back((1 + m_entry_vec.size()) << 16 | (uint32_t)spv::OpEntryPoint);
    entry_final.insert(entry_final.end(), m_entry_vec.begin(), m_entry_vec.end());
    entry_final.insert(entry_final.end(), m_exec_modes.begin(), m_exec_modes.end());

    std::vector<uint32_t> out;
    out.reserve(m_meta.size() + m_types.size() + m_funcs.size() + entry_final.size() + 16);
    out.push_back(0x07230203);
    out.push_back(0x00010000);
    out.push_back(0);                          // generator
    out.push_back(0);                          // bound (patch abaixo)
    out.push_back(0);                          // schema
    for (uint32_t w : m_meta) out.push_back(w);
    for (uint32_t w : entry_final) out.push_back(w);
    for (uint32_t w : m_debug) out.push_back(w);
    for (uint32_t w : m_annot) out.push_back(w);
    for (uint32_t w : m_types) out.push_back(w);
    for (uint32_t w : m_funcs) out.push_back(w);
    out[3] = m_next_id;
    return out;
}

void SpirvBuilder::capability(spv::Capability c) { emit_to(Sec::Meta, spv::OpCapability, {(uint32_t)c}); }

void SpirvBuilder::ext_import(const char *name) {
    uint32_t idr = id();
    std::vector<uint32_t> ops{idr};
    push_spv_string(ops, name);
    emit_to(Sec::Meta, spv::OpExtInstImport, ops);
    m_std450 = idr;
}

void SpirvBuilder::memory_model(spv::AddressingModel a, spv::MemoryModel m) {
    emit_to(Sec::Meta, spv::OpMemoryModel, {(uint32_t)a, (uint32_t)m});
}

void SpirvBuilder::execution_mode(spv::ExecutionMode m, const std::vector<uint32_t> &literals) {
    std::vector<uint32_t> ops{m_entry, (uint32_t)m};
    for (uint32_t l : literals) ops.push_back(l);
    uint32_t wc = 1 + ops.size();
    m_exec_modes.push_back(wc << 16 | (uint32_t)spv::OpExecutionMode);
    for (uint32_t o : ops) m_exec_modes.push_back(o);
}

void SpirvBuilder::decorate(uint32_t target, spv::Decoration d, const std::vector<uint32_t> &literals) {
    std::vector<uint32_t> ops{target, (uint32_t)d};
    for (uint32_t l : literals) ops.push_back(l);
    emit(spv::OpDecorate, ops);
}
void SpirvBuilder::member_decorate(uint32_t target, uint32_t member, spv::Decoration d, uint32_t literal) {
    emit(spv::OpMemberDecorate, {target, member, (uint32_t)d, literal});
}
void SpirvBuilder::name(uint32_t target, const char *n) {
    std::vector<uint32_t> ops{target};
    push_spv_string(ops, n);
    emit(spv::OpName, ops);
}

uint32_t SpirvBuilder::type_void() {
    if (!m_void_t) { m_void_t = id(); emit_type_result(spv::OpTypeVoid, m_void_t, {}); }
    return m_void_t;
}
uint32_t SpirvBuilder::type_bool() {
    if (!m_bool_t) { m_bool_t = id(); emit_type_result(spv::OpTypeBool, m_bool_t, {}); }
    return m_bool_t;
}
uint32_t SpirvBuilder::type_float() {
    if (!m_f32_t) { m_f32_t = id(); emit_type_result(spv::OpTypeFloat, m_f32_t, {32}); }
    return m_f32_t;
}
uint32_t SpirvBuilder::type_int(bool signed_) {
    uint32_t &t = signed_ ? m_i32_t : m_u32_t;
    if (!t) { t = id(); emit_type_result(spv::OpTypeInt, t, {32, signed_ ? 1u : 0u}); }
    return t;
}
uint32_t SpirvBuilder::type_vec(uint32_t comp, uint32_t n) {
    uint32_t key = comp * 16 + n;
    auto it = m_vec_t.find(key);
    if (it != m_vec_t.end()) return it->second;
    uint32_t t = id();
    emit_type_result(spv::OpTypeVector, t, {comp, n});
    m_vec_t[key] = t;
    return t;
}
uint32_t SpirvBuilder::type_mat(uint32_t vec, uint32_t cols) {
    uint32_t key = vec * 16 + cols;
    auto it = m_mat_t.find(key);
    if (it != m_mat_t.end()) return it->second;
    uint32_t t = id();
    emit_type_result(spv::OpTypeMatrix, t, {vec, cols});
    m_mat_t[key] = t;
    return t;
}
uint32_t SpirvBuilder::type_array(uint32_t elem, uint32_t len) {
    uint32_t t = id();
    emit_type_result(spv::OpTypeArray, t, {elem, len});
    return t;
}
uint32_t SpirvBuilder::type_array_ubo(uint32_t elem, uint32_t len_const_id, uint32_t stride) {
    uint32_t t = id();
    emit_type_result(spv::OpTypeArray, t, {elem, len_const_id});
    decorate(t, spv::DecorationArrayStride, {stride});
    return t;
}

uint32_t SpirvBuilder::type_struct(const std::vector<uint32_t> &members) {
    uint32_t t = id();
    emit_type_result(spv::OpTypeStruct, t, members);
    return t;
}
uint32_t SpirvBuilder::type_sampler() {
    if (!m_sampler_t) { m_sampler_t = id(); emit_type_result(spv::OpTypeSampler, m_sampler_t, {}); }
    return m_sampler_t;
}
uint32_t SpirvBuilder::type_image(uint32_t st, spv::Dim dim, bool depth, bool arrayed, bool ms,
                                  uint32_t sampled, spv::ImageFormat fmt) {
    uint32_t key = st ^ ((uint32_t)dim << 8) ^ ((uint32_t)depth << 16) ^ ((uint32_t)arrayed << 17) ^
                   ((uint32_t)ms << 18) ^ (sampled << 20) ^ ((uint32_t)fmt << 24);
    auto it = m_img_t.find(key);
    if (it != m_img_t.end()) return it->second;
    uint32_t t = id();
    emit_type_result(spv::OpTypeImage, t,
                     {st, (uint32_t)dim, depth ? 1u : 0u, arrayed ? 1u : 0u, ms ? 1u : 0u, sampled, (uint32_t)fmt});
    m_img_t[key] = t;
    return t;
}
uint32_t SpirvBuilder::type_sampled_image(uint32_t image) {
    auto it = m_simg_t.find(image);
    if (it != m_simg_t.end()) return it->second;
    uint32_t t = id();
    emit_type_result(spv::OpTypeSampledImage, t, {image});
    m_simg_t[image] = t;
    return t;
}
uint32_t SpirvBuilder::type_function(uint32_t ret, const std::vector<uint32_t> &params) {
    uint32_t t = id();
    std::vector<uint32_t> ops{ret};
    for (uint32_t p : params) ops.push_back(p);
    emit_type_result(spv::OpTypeFunction, t, ops);
    return t;
}
uint32_t SpirvBuilder::type_pointer(spv::StorageClass sc, uint32_t base) {
    uint64_t key = ((uint64_t)sc << 32) | base;
    auto it = m_ptr_t.find(key);
    if (it != m_ptr_t.end()) return it->second;
    uint32_t t = id();
    emit_type_result(spv::OpTypePointer, t, {(uint32_t)sc, base});
    m_ptr_t[key] = t;
    return t;
}

uint32_t SpirvBuilder::const_u32(uint32_t v) {
    auto it = m_const_u.find(v);
    if (it != m_const_u.end()) return it->second;
    uint32_t c = id();
    emit_result(spv::OpConstant, type_int(false), c, {v});
    m_const_u[v] = c;
    return c;
}
uint32_t SpirvBuilder::const_i32(int32_t v) {
    auto it = m_const_i.find((uint32_t)v);
    if (it != m_const_i.end()) return it->second;
    uint32_t c = id();
    emit_result(spv::OpConstant, type_int(true), c, {(uint32_t)v});
    m_const_i[(uint32_t)v] = c;
    return c;
}
uint32_t SpirvBuilder::const_f32(float v) {
    uint32_t bits;
    memcpy(&bits, &v, 4);
    auto it = m_const_f.find(bits);
    if (it != m_const_f.end()) return it->second;
    uint32_t c = id();
    emit_result(spv::OpConstant, type_float(), c, {bits});
    m_const_f[bits] = c;
    return c;
}
uint32_t SpirvBuilder::const_bool(bool v) {
    uint32_t c = id();
    emit_result(v ? spv::OpConstantTrue : spv::OpConstantFalse, type_bool(), c, {});
    return c;
}
uint32_t SpirvBuilder::const_vec(uint32_t type, const std::vector<uint32_t> &comps) {
    uint32_t c = id();
    emit_result(spv::OpConstantComposite, type, c, comps);
    return c;
}
uint32_t SpirvBuilder::const_null(uint32_t type) {
    uint32_t c = id();
    emit_result(spv::OpConstantNull, type, c, {});
    return c;
}

uint32_t SpirvBuilder::variable(spv::StorageClass sc, uint32_t type, uint32_t init_const) {
    uint32_t pt = type_pointer(sc, type);
    uint32_t v = id();
    std::vector<uint32_t> ops{(uint32_t)sc};
    if (init_const) ops.push_back(init_const);
    emit_result(spv::OpVariable, pt, v, ops);
    return v;
}

void SpirvBuilder::function_begin(uint32_t fn_id, uint32_t ret_type, uint32_t fn_type) {
    m_cur = Sec::Funcs;
    emit_result(spv::OpFunction, ret_type, fn_id, {(uint32_t)spv::FunctionControlMaskNone, fn_type});
    uint32_t lbl = label();
    label_bind(lbl);
}
void SpirvBuilder::function_end() { emit(spv::OpFunctionEnd, {}); }

uint32_t SpirvBuilder::load(uint32_t type, uint32_t ptr) {
    uint32_t r = id();
    emit_result(spv::OpLoad, type, r, {ptr});
    return r;
}
void SpirvBuilder::store(uint32_t ptr, uint32_t value) { emit(spv::OpStore, {ptr, value}); }
uint32_t SpirvBuilder::access_chain(uint32_t pt, uint32_t base, const std::vector<uint32_t> &idx) {
    uint32_t r = id();
    std::vector<uint32_t> ops{base};
    for (uint32_t i : idx) ops.push_back(i);
    emit_result(spv::OpAccessChain, pt, r, ops);
    return r;
}
uint32_t SpirvBuilder::composite_construct(uint32_t type, const std::vector<uint32_t> &parts) {
    uint32_t r = id();
    emit_result(spv::OpCompositeConstruct, type, r, parts);
    return r;
}
uint32_t SpirvBuilder::composite_extract(uint32_t type, uint32_t comp, const std::vector<uint32_t> &idx) {
    uint32_t r = id();
    std::vector<uint32_t> ops{comp};
    for (uint32_t i : idx) ops.push_back(i);
    emit_result(spv::OpCompositeExtract, type, r, ops);
    return r;
}
uint32_t SpirvBuilder::vector_shuffle(uint32_t type, uint32_t v1, uint32_t v2, const std::vector<uint32_t> &idx) {
    uint32_t r = id();
    std::vector<uint32_t> ops{v1, v2};
    for (uint32_t i : idx) ops.push_back(i);
    emit_result(spv::OpVectorShuffle, type, r, ops);
    return r;
}
uint32_t SpirvBuilder::op(spv::Op opcode, uint32_t type, const std::vector<uint32_t> &operands) {
    uint32_t r = id();
    emit_result(opcode, type, r, operands);
    return r;
}
void SpirvBuilder::op0(spv::Op opcode, const std::vector<uint32_t> &operands) { emit(opcode, operands); }
uint32_t SpirvBuilder::ext(uint32_t type, uint32_t code, const std::vector<uint32_t> &ops) {
    uint32_t r = id();
    std::vector<uint32_t> all{m_std450, code};
    for (uint32_t o : ops) all.push_back(o);
    emit_result(spv::OpExtInst, type, r, all);
    return r;
}
uint32_t SpirvBuilder::function_call(uint32_t fn_id) {
    // OpFunctionCall: <result type> <result id> <function> (o id de resultado
    // e obrigatorio mesmo para funcoes void)
    uint32_t r = id();
    emit_result(spv::OpFunctionCall, type_void(), r, {fn_id});
    return r;
}

uint32_t SpirvBuilder::label() { return id(); }
void SpirvBuilder::label_bind(uint32_t lbl) { emit_type_result(spv::OpLabel, lbl, {}); }
void SpirvBuilder::branch(uint32_t t) { emit(spv::OpBranch, {t}); }
void SpirvBuilder::branch_cond(uint32_t c, uint32_t t, uint32_t f) {
    emit(spv::OpBranchConditional, {c, t, f});
}
void SpirvBuilder::loop_merge(uint32_t merge, uint32_t cont) {
    emit(spv::OpLoopMerge, {merge, cont, (uint32_t)spv::LoopControlMaskNone});
}
void SpirvBuilder::selection_merge(uint32_t merge) {
    emit(spv::OpSelectionMerge, {merge, (uint32_t)spv::SelectionControlMaskNone});
}
void SpirvBuilder::ret() { emit(spv::OpReturn, {}); }
void SpirvBuilder::kill() { emit(spv::OpKill, {}); }

void SpirvBuilder::add_interface_var(spv::StorageClass sc, uint32_t var_id) {
    if (sc == spv::StorageClassInput || sc == spv::StorageClassOutput) m_iface.push_back(var_id);
}

uint32_t SpirvBuilder::image_sample_implicit(uint32_t t4, uint32_t si, uint32_t coords) {
    uint32_t r = id();
    emit_result(spv::OpImageSampleImplicitLod, t4, r, {si, coords});
    return r;
}
uint32_t SpirvBuilder::image_sample_proj(uint32_t t4, uint32_t si, uint32_t coords) {
    uint32_t r = id();
    emit_result(spv::OpImageSampleProjImplicitLod, t4, r, {si, coords});
    return r;
}
uint32_t SpirvBuilder::image_sample_explicit_lod(uint32_t t4, uint32_t si, uint32_t coords, uint32_t lod) {
    uint32_t r = id();
    emit_result(spv::OpImageSampleExplicitLod, t4, r, {si, coords, (uint32_t)spv::ImageOperandsLodMask, lod});
    return r;
}
uint32_t SpirvBuilder::image_sample_bias(uint32_t t4, uint32_t si, uint32_t coords, uint32_t bias) {
    uint32_t r = id();
    emit_result(spv::OpImageSampleImplicitLod, t4, r,
                {si, coords, (uint32_t)spv::ImageOperandsBiasMask, bias});
    return r;
}
uint32_t SpirvBuilder::image_sample_grad(uint32_t t4, uint32_t si, uint32_t coords, uint32_t dx, uint32_t dy) {
    // Grad exige ExplicitLod (spec SPIR-V: ImplicitLod aceita Bias apenas)
    uint32_t r = id();
    emit_result(spv::OpImageSampleExplicitLod, t4, r,
                {si, coords, (uint32_t)spv::ImageOperandsGradMask, dx, dy});
    return r;
}

} // namespace nvr
