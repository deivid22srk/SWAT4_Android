// Geracao SPIR-V do pipeline fixed-function D3D9. Contrato em nvr_fff.h.
//
// Desenho: UM pixel shader total (ops/args de TSS, fog table e alpha test vem
// de uniformes; o loop de stages para em op DISABLE). O VS e gerado por
// assinatura de inputs (o tipo dos atributos vem do declaration, exigencia do
// Vulkan) e implementa o modelo de iluminacao D3D9 completo.
#include "nvr_fff.h"
#include <cstring>

namespace nvr {

namespace {

struct UF {
    SpirvBuilder &b;
    uint32_t ubo, t_v4p, t_v4, t_f32;
    uint32_t slot(uint32_t i) {
        uint32_t p = b.access_chain(t_v4p, ubo, {b.const_i32(0), b.const_i32((int32_t)i)});
        return b.load(t_v4, p);
    }
    uint32_t comp(uint32_t i, uint32_t c) { return b.composite_extract(t_f32, slot(i), {c}); }
};

struct C {
    SpirvBuilder b;
    uint32_t t_void, t_f32, t_i32, t_bool, t_v2, t_v3, t_v4, t_mat4, t_bv4, t_v4p, t_fn;
    UF *u = nullptr;
    uint32_t max_slot = 0;

    uint32_t splat(float f) {
        uint32_t k = b.const_f32(f);
        return b.composite_construct(t_v4, {k, k, k, k});
    }
    uint32_t splatv(uint32_t s) { return b.composite_construct(t_v4, {s, s, s, s}); }
    uint32_t splat3(uint32_t s) { return b.composite_construct(t_v3, {s, s, s}); }
    uint32_t itof(uint32_t i) { return b.op(spv::OpConvertSToF, t_f32, i); }
    uint32_t v3(uint32_t v) { return b.vector_shuffle(t_v3, v, v, {0, 1, 2}); }
    uint32_t x(uint32_t v) { return b.composite_extract(t_f32, v, {0}); }
    uint32_t y(uint32_t v) { return b.composite_extract(t_f32, v, {1}); }
    uint32_t z(uint32_t v) { return b.composite_extract(t_f32, v, {2}); }
    uint32_t w(uint32_t v) { return b.composite_extract(t_f32, v, {3}); }
    uint32_t fmax(uint32_t a, uint32_t c) { return b.ext(t_f32, G450_FMax, {a, c}); }
    uint32_t fmin(uint32_t a, uint32_t c) { return b.ext(t_f32, G450_FMin, {a, c}); }
    uint32_t norm3(uint32_t v) {
        uint32_t n2 = b.dot(t_f32, v3(v), v3(v));
        uint32_t inv = b.ext(t_f32, G450_InverseSqrt, {b.ext(t_f32, G450_FAbs, {n2})});
        uint32_t s = b.composite_construct(t_v3, {inv, inv, inv});
        return b.fmul(t_v3, v3(v), s);
    }
    uint32_t iset(uint32_t cond, uint32_t a, uint32_t c) { return b.select(t_f32, cond, a, c); }
    uint32_t ieq(uint32_t i, int32_t v) { return b.op(spv::OpIEqual, t_bool, {i, b.const_i32(v)}); }
    uint32_t feq(uint32_t f, float v) { return b.op(spv::OpFOrdEqual, t_bool, {f, b.const_f32(v)}); }
    uint32_t fne(uint32_t f, float v) { return b.op(spv::OpFUnordNotEqual, t_bool, {f, b.const_f32(v)}); }
    uint32_t toint(uint32_t f) { return b.op(spv::OpConvertFToS, t_i32, f); }
};

C common_begin(spv::ExecutionModel model, uint32_t binding) {
    C c{};
    c.b.capability(spv::CapabilityShader);
    c.b.ext_import("GLSL.std.450");
    c.t_void = c.b.type_void();
    c.t_f32 = c.b.type_float();
    c.t_i32 = c.b.type_int(true);
    c.t_bool = c.b.type_bool();
    c.t_v2 = c.b.type_vec(c.t_f32, 2);
    c.t_v3 = c.b.type_vec(c.t_f32, 3);
    c.t_v4 = c.b.type_vec(c.t_f32, 4);
    c.t_bv4 = c.b.type_vec(c.t_bool, 4);
    c.t_mat4 = c.b.type_mat(c.t_v4, 4);
    c.t_v4p = c.b.type_pointer(spv::StorageClassUniform, c.t_v4);
    c.b.start(model, "main");
    c.b.memory_model(spv::AddressingModelLogical, spv::MemoryModelGLSL450);
    uint32_t arr = c.b.type_array_ubo(c.t_v4, c.b.const_u32(256), 16);
    uint32_t blk = c.b.type_struct({arr});
    c.b.member_decorate(blk, 0, spv::DecorationOffset, 0);
    c.b.decorate(blk, spv::DecorationBlock);
    uint32_t ubo = c.b.variable(spv::StorageClassUniform, blk);
    c.b.decorate(ubo, spv::DecorationDescriptorSet, {0});
    c.b.decorate(ubo, spv::DecorationBinding, {binding});
    c.u = new UF{c.b, ubo, c.t_v4p, c.t_v4, c.t_f32};
    c.t_fn = c.b.type_function(c.t_void, {});
    c.b.function_begin(c.b.entry_id(), c.t_void, c.t_fn);
    return c;
}

} // namespace

// ------------------------------------------------------------------ VS
bool generate_fff_vs(const FffVsCfg &cfg, XlateOut &out) {
    C c = common_begin(spv::ExecutionModelVertex, kBindVsUbo);
    SpirvBuilder &b = c.b;
    UF &u = *c.u;

    auto input_v4 = [&](uint32_t loc) -> uint32_t {
        uint8_t comps = cfg.in_comps ? cfg.in_comps[loc] : 0;
        if (!comps) return b.const_null(c.t_v4);
        uint32_t vt = b.type_vec(c.t_f32, comps);
        uint32_t v = b.variable(spv::StorageClassInput, vt);
        b.decorate(v, spv::DecorationLocation, {loc});
        b.add_interface_var(spv::StorageClassInput, v);
        uint32_t val = b.load(vt, v);
        if (comps == 4) return val;
        std::vector<uint32_t> parts{val};
        for (uint32_t i = comps; i < 4; i++) parts.push_back(b.const_f32(0.0f));
        return b.composite_construct(c.t_v4, parts);
    };
    auto out_v4 = [&](uint32_t loc) {
        uint32_t v = b.variable(spv::StorageClassOutput, c.t_v4);
        b.decorate(v, spv::DecorationLocation, {loc});
        b.add_interface_var(spv::StorageClassOutput, v);
        return v;
    };

    uint32_t in_pos = input_v4(kLocPosition);
    uint32_t in_normal = input_v4(kLocNormal);
    uint32_t in_diffuse = input_v4(kLocColor0);
    uint32_t in_specular = input_v4(kLocColor1);
    uint32_t in_tex[8];
    for (uint32_t i = 0; i < 8; i++) in_tex[i] = input_v4(kLocTexcoord0 + i);

    uint32_t o_pos = b.variable(spv::StorageClassOutput, c.t_v4);
    b.decorate(o_pos, spv::DecorationBuiltIn, {(uint32_t)spv::BuiltInPosition});
    b.add_interface_var(spv::StorageClassOutput, o_pos);
    uint32_t o_diff = out_v4(kLocColor0);
    uint32_t o_spec = out_v4(kLocColor1);
    uint32_t o_fogv = out_v4(kLocFog);
    uint32_t o_tex[8];
    for (uint32_t i = 0; i < 8; i++) o_tex[i] = out_v4(kLocTexcoord0 + i);

    // ---- transforms
    auto mat_rows = [&](uint32_t base) {
        return b.composite_construct(c.t_mat4, {u.slot(base), u.slot(base + 1),
                                                u.slot(base + 2), u.slot(base + 3)});
    };
    uint32_t wpos = b.op(spv::OpMatrixTimesVector, c.t_v4, {mat_rows(kFffVsWorld), in_pos});
    uint32_t vpos = b.op(spv::OpMatrixTimesVector, c.t_v4, {mat_rows(kFffVsView), wpos});
    uint32_t clip = b.op(spv::OpMatrixTimesVector, c.t_v4, {mat_rows(kFffVsProj), vpos});
    b.store(o_pos, clip);

    uint32_t vpos3 = c.v3(vpos);
    uint32_t misc = u.slot(kFffVsMisc);

    // normal via inverse-transpose(world*view)
    uint32_t n3in = c.v3(in_normal);
    uint32_t r0 = u.slot(kFffVsWvIt), r1 = u.slot(kFffVsWvIt + 1), r2 = u.slot(kFffVsWvIt + 2);
    uint32_t n3 = b.composite_construct(c.t_v3,
        {b.dot(c.t_f32, n3in, c.v3(r0)), b.dot(c.t_f32, n3in, c.v3(r1)), b.dot(c.t_f32, n3in, c.v3(r2))});
    n3 = b.selectv(c.t_v3, 3, c.fne(c.z(misc), 0.0f), c.norm3(n3), n3);   // normalize_normals

    // eye vector (local viewer ou (0,0,1))
    uint32_t eye = b.selectv(c.t_v3, 3, c.fne(c.w(misc), 0.0f), c.norm3(b.fneg(c.t_v3, vpos3)),
                            b.composite_construct(c.t_v3, {b.const_f32(0.0f), b.const_f32(0.0f), b.const_f32(1.0f)}));
    uint32_t dist_rad = b.ext(c.t_f32, G450_Sqrt, {b.ext(c.t_f32, G450_FAbs, {b.dot(c.t_f32, vpos3, vpos3)})});
    uint32_t dist_z = b.ext(c.t_f32, G450_FAbs, {c.z(vpos)});
    uint32_t dist = b.select(c.t_f32, c.fne(u.comp(kFffVsFogColor, 3), 0.0f), dist_rad, dist_z);
    // (fogp+1).z = range_fog flag — packer grava

    // ---- fontes de material
    auto mat_or_vertex = [&](uint32_t sel_comp, uint32_t mat_slot, uint32_t fallback) {
        uint32_t sel = u.comp(kFffVsMatSrc, sel_comp);
        uint32_t m = u.slot(mat_slot);
        return b.selectv(c.t_v4, 4, c.feq(sel, 1.0f), in_diffuse,
                        b.selectv(c.t_v4, 4, c.feq(sel, 2.0f), in_specular, m));
        (void)fallback;
    };
    uint32_t mat_diff = mat_or_vertex(0, kFffVsMatDiff, 0);
    uint32_t mat_amb = mat_or_vertex(1, kFffVsMatAmb, 0);
    uint32_t mat_spec = mat_or_vertex(2, kFffVsMatSpec, 0);
    uint32_t mat_emis = mat_or_vertex(3, kFffVsMatEmis, 0);
    // specular color: o .w do slot guarda o power; para COLOR2 source o alpha
    // do specular vem do vertex specular

    // ---- acumuladores
    uint32_t g_amb = u.slot(kFffVsGlobalAmb);
    uint32_t acc = b.composite_construct(c.t_v4, {
        b.fadd(c.t_f32, b.fmul(c.t_f32, c.x(mat_amb), c.x(g_amb)), c.x(mat_emis)),
        b.fadd(c.t_f32, b.fmul(c.t_f32, c.y(mat_amb), c.y(g_amb)), c.y(mat_emis)),
        b.fadd(c.t_f32, b.fmul(c.t_f32, c.z(mat_amb), c.z(g_amb)), c.z(mat_emis)),
        b.fmul(c.t_f32, c.w(mat_amb), c.w(g_amb))});
    uint32_t spec = b.composite_construct(c.t_v4, {b.const_f32(0.0f), b.const_f32(0.0f), b.const_f32(0.0f), b.const_f32(0.0f)});

    uint32_t spec_en = c.fne(c.y(misc), 0.0f);

    // ---- luzes (8; type=0 = inativa)
    for (uint32_t li = 0; li < 8; li++) {
        uint32_t base = kFffVsLights + li * 6;
        uint32_t lpos = u.slot(base), ldir = u.slot(base + 1), ldiff = u.slot(base + 2),
                 lspec = u.slot(base + 3), latt = u.slot(base + 4), lamb = u.slot(base + 5);
        uint32_t ltype = c.toint(c.w(latt));
        uint32_t is_dir = c.ieq(ltype, D3DLIGHT_DIRECTIONAL);
        uint32_t is_point = c.ieq(ltype, D3DLIGHT_POINT);
        uint32_t is_spot = c.ieq(ltype, D3DLIGHT_SPOT);
        uint32_t active = b.op(spv::OpLogicalOr, c.t_bool,
                               {is_dir, b.op(spv::OpLogicalOr, c.t_bool, {is_point, is_spot})});

        uint32_t to_light = b.fsub(c.t_v3, c.v3(lpos), vpos3);
        uint32_t d2 = b.dot(c.t_f32, to_light, to_light);
        uint32_t dist_l = b.ext(c.t_f32, G450_Sqrt, {b.ext(c.t_f32, G450_FAbs, {d2})});
        uint32_t inv_d = b.composite_construct(c.t_v3,
            {b.ext(c.t_f32, G450_InverseSqrt, {b.ext(c.t_f32, G450_FAbs, {d2})}),
             b.ext(c.t_f32, G450_InverseSqrt, {b.ext(c.t_f32, G450_FAbs, {d2})}),
             b.ext(c.t_f32, G450_InverseSqrt, {b.ext(c.t_f32, G450_FAbs, {d2})})});
        uint32_t to_norm = b.fmul(c.t_v3, to_light, inv_d);
        uint32_t light_dir = b.selectv(c.t_v3, 3, is_dir, c.norm3(b.fneg(c.t_v3, c.v3(ldir))), to_norm);

        uint32_t a0 = c.x(latt), a1 = c.y(latt), a2 = c.z(latt);
        uint32_t denom = b.fadd(c.t_f32, b.fadd(c.t_f32, a0, b.fmul(c.t_f32, a1, dist_l)),
                                b.fmul(c.t_f32, a2, b.fmul(c.t_f32, dist_l, dist_l)));
        uint32_t atten = b.fdiv(c.t_f32, b.const_f32(1.0f), c.fmax(denom, b.const_f32(0.0001f)));
        uint32_t in_range = b.op(spv::OpFOrdLessThan, c.t_bool, {dist_l, c.w(lpos)});
        uint32_t atten_f = b.select(c.t_f32, is_dir, b.const_f32(1.0f),
                                    b.select(c.t_f32, in_range, atten, b.const_f32(0.0f)));
        // spot cone (theta em dir.w, phi em diff.w — ver nvr_fff.h)
        uint32_t rho = b.fneg(c.t_f32, b.dot(c.t_f32, to_norm, c.v3(ldir)));
        uint32_t cos_theta = b.ext(c.t_f32, G450_Cos, {b.fmul(c.t_f32, b.const_f32(0.5f), c.w(ldir))});
        uint32_t cos_phi = b.ext(c.t_f32, G450_Cos, {b.fmul(c.t_f32, b.const_f32(0.5f), c.w(ldiff))});
        uint32_t t_in = b.fdiv(c.t_f32, b.fsub(c.t_f32, rho, cos_phi),
                               b.fsub(c.t_f32, cos_theta, cos_phi));
        uint32_t smooth = b.ext(c.t_f32, G450_Pow,
                                {c.fmax(c.fmin(t_in, b.const_f32(1.0f)), b.const_f32(0.0f)),
                                 c.fmax(c.w(lspec), b.const_f32(0.0001f))});
        uint32_t spot_f = b.select(c.t_f32, b.op(spv::OpFOrdLessThan, c.t_bool, {rho, cos_phi}),
                                   b.const_f32(0.0f),
                                   b.select(c.t_f32, b.op(spv::OpFOrdGreaterThanEqual, c.t_bool, {rho, cos_theta}),
                                            b.const_f32(1.0f), smooth));
        atten_f = b.select(c.t_f32, is_spot, b.fmul(c.t_f32, atten_f, spot_f), atten_f);

        uint32_t ndotl = c.fmax(b.dot(c.t_f32, n3, light_dir), b.const_f32(0.0f));
        uint32_t h = c.norm3(b.fadd(c.t_v3, light_dir, eye));
        uint32_t ndoth = c.fmax(b.dot(c.t_f32, n3, h), b.const_f32(0.0f));
        uint32_t sh = b.ext(c.t_f32, G450_Pow, {ndoth, c.fmax(c.w(u.slot(kFffVsMatSpec)), b.const_f32(0.0001f))});
        uint32_t lit = b.op(spv::OpFOrdGreaterThan, c.t_bool, {ndotl, b.const_f32(0.0f)});

        uint32_t on = b.select(c.t_f32, active, b.const_f32(1.0f), b.const_f32(0.0f));
        uint32_t da = b.fmul(c.t_f32, b.fmul(c.t_f32, atten_f, on), ndotl);
        // difusa
        acc = b.fadd(c.t_v4, acc,
                     b.composite_construct(c.t_v4, {
                         b.fmul(c.t_f32, da, b.fmul(c.t_f32, c.x(mat_diff), c.x(ldiff))),
                         b.fmul(c.t_f32, da, b.fmul(c.t_f32, c.y(mat_diff), c.y(ldiff))),
                         b.fmul(c.t_f32, da, b.fmul(c.t_f32, c.z(mat_diff), c.z(ldiff))),
                         b.const_f32(0.0f)}));
        // ambiente da luz
        acc = b.fadd(c.t_v4, acc,
                     b.composite_construct(c.t_v4, {
                         b.fmul(c.t_f32, on, b.fmul(c.t_f32, c.x(mat_amb), c.x(lamb))),
                         b.fmul(c.t_f32, on, b.fmul(c.t_f32, c.y(mat_amb), c.y(lamb))),
                         b.fmul(c.t_f32, on, b.fmul(c.t_f32, c.z(mat_amb), c.z(lamb))),
                         b.const_f32(0.0f)}));
        // especular
        uint32_t sa = b.select(c.t_f32, b.op(spv::OpLogicalAnd, c.t_bool, {lit, spec_en}),
                               b.fmul(c.t_f32, b.fmul(c.t_f32, atten_f, on), sh), b.const_f32(0.0f));
        spec = b.fadd(c.t_v4, spec,
                      b.composite_construct(c.t_v4, {
                          b.fmul(c.t_f32, sa, b.fmul(c.t_f32, c.x(mat_spec), c.x(lspec))),
                          b.fmul(c.t_f32, sa, b.fmul(c.t_f32, c.y(mat_spec), c.y(lspec))),
                          b.fmul(c.t_f32, sa, b.fmul(c.t_f32, c.z(mat_spec), c.z(lspec))),
                          b.const_f32(0.0f)}));
        (void)is_point;
    }
    acc = b.composite_construct(c.t_v4, {c.x(acc), c.y(acc), c.z(acc), c.w(mat_diff)});
    b.store(o_diff, acc);
    // specular alpha = alpha do specular material/vertex
    b.store(o_spec, b.composite_construct(c.t_v4,
        {c.x(spec), c.y(spec), c.z(spec), c.w(mat_spec)}));

    // ---- fog
    uint32_t fogp = u.slot(kFffVsFog);
    uint32_t start = c.x(fogp), end = c.y(fogp), density = c.z(fogp);
    // vmode bruto (i32); byte baixo = D3DFOGMODE do vertex fog
    uint32_t vmode_raw = c.toint(c.w(fogp));
    uint32_t vm = b.op(spv::OpBitwiseAnd, c.t_i32, {vmode_raw, b.const_i32(0xFF)});
    uint32_t lin = b.fdiv(c.t_f32, b.fsub(c.t_f32, end, dist), b.fsub(c.t_f32, end, start));
    uint32_t one = b.const_f32(1.0f);
    uint32_t fog_e = b.fdiv(c.t_f32, one,
                            b.ext(c.t_f32, G450_Exp, {b.fneg(c.t_f32, b.fmul(c.t_f32, dist, density))}));
    uint32_t dd = b.fmul(c.t_f32, dist, density);
    uint32_t fog_e2 = b.fdiv(c.t_f32, one,
                             b.ext(c.t_f32, G450_Exp, {b.fneg(c.t_f32, b.fmul(c.t_f32, dd, dd))}));
    uint32_t f1 = c.iset(c.ieq(vm, 1), lin, one);
    uint32_t f2 = b.select(c.t_f32, c.ieq(vm, 2), fog_e, f1);
    uint32_t f3 = b.select(c.t_f32, c.ieq(vm, 3), fog_e2, f2);
    // vmode==0: passa a profundidade (table fog no PS); senao o fator
    uint32_t fog_out = b.selectv(c.t_v4, 4, c.ieq(vm, 0),
                                b.composite_construct(c.t_v4, {dist, dist, dist, one}),
                                b.composite_construct(c.t_v4, {f3, f3, f3, one}));
    b.store(o_fogv, fog_out);

    // ---- texcoords: TCI + texture transform
    for (uint32_t s = 0; s < 8; s++) {
        uint32_t tci = u.slot(kFffVsTci + s);
        uint32_t coord_idx = c.toint(c.x(tci));          // D3DTSS_TEXCOORDINDEX cru
        uint32_t mode = b.op(spv::OpShiftRightLogical, c.t_i32, {coord_idx, b.const_i32(16)});
        uint32_t idx = b.op(spv::OpBitwiseAnd, c.t_i32, {coord_idx, b.const_i32(0xF)});
        uint32_t passthru = in_tex[0];
        // seleciona in_tex[idx] (0..7) por cadeia de selects
        for (uint32_t i = 1; i < 8; i++)
            passthru = b.selectv(c.t_v4, 4, c.ieq(idx, (int32_t)i), in_tex[i], passthru);
        uint32_t is_norm = c.ieq(mode, 1);
        uint32_t is_pos = c.ieq(mode, 2);
        uint32_t ne = b.dot(c.t_f32, n3, eye);
        uint32_t two = b.composite_construct(c.t_v3, {b.const_f32(2.0f), b.const_f32(2.0f), b.const_f32(2.0f)});
        uint32_t refl = b.fsub(c.t_v3,
                               b.fmul(c.t_v3, b.fmul(c.t_v3, b.composite_construct(c.t_v3, {ne, ne, ne}), n3), two),
                               eye);
        uint32_t one = b.const_f32(1.0f);
        uint32_t special = b.selectv(c.t_v4, 4, is_norm,
                                    b.composite_construct(c.t_v4, {c.x(n3), c.y(n3), c.z(n3), one}),
                                    b.selectv(c.t_v4, 4, is_pos,
                                             b.composite_construct(c.t_v4, {c.x(vpos3), c.y(vpos3), c.z(vpos3), one}),
                                             b.composite_construct(c.t_v4, {c.x(refl), c.y(refl), c.z(refl), one})));
        uint32_t tc = b.selectv(c.t_v4, 4, c.ieq(mode, 0), passthru, special);

        // texture transform
        uint32_t ttff = c.toint(c.y(tci));
        uint32_t has_mat = c.fne(c.z(tci), 0.0f);
        uint32_t tmb = kFffVsTexMat + 4 * s;
        uint32_t tted = b.op(spv::OpMatrixTimesVector, c.t_v4,
                             {b.composite_construct(c.t_mat4, {u.slot(tmb), u.slot(tmb + 1),
                                                               u.slot(tmb + 2), u.slot(tmb + 3)}),
                              tc});
        uint32_t tc2 = b.selectv(c.t_v4, 4, has_mat, tted, tc);
        // D3DTTFF_PROJECTED: divide pelo componente COUNTn-1
        uint32_t proj = b.op(spv::OpINotEqual, c.t_bool,
                             {b.op(spv::OpBitwiseAnd, c.t_i32, {ttff, b.const_i32(0x10000)}), b.const_i32(0)});
        uint32_t count = b.op(spv::OpBitwiseAnd, c.t_i32, {ttff, b.const_i32(0xF)});
        // divisor = componente COUNTn (comparacao INTEIRA no count cru; 0/4 = w)
        uint32_t divisor = c.w(tc2);
        divisor = b.select(c.t_f32, c.ieq(count, 1), c.x(tc2), divisor);   // escalar
        divisor = b.select(c.t_f32, c.ieq(count, 2), c.y(tc2), divisor);
        divisor = b.select(c.t_f32, c.ieq(count, 3), c.z(tc2), divisor);
        uint32_t projected = b.fdiv(c.t_v4, tc2,
                                    b.composite_construct(c.t_v4, {divisor, divisor, divisor, divisor}));
        uint32_t tc3 = b.selectv(c.t_v4, 4, proj, projected, tc2);
        b.store(o_tex[s], tc3);
        c.max_slot = std::max(c.max_slot, tmb + 3);
    }

    b.ret();
    b.function_end();
    out.spirv = b.finish();
    out.max_const = std::max(kFffVsEnd, c.max_slot + 1);
    delete c.u;
    return true;
}

// ------------------------------------------------------------------ PS
bool generate_fff_ps(const FffPsCfg &cfg, XlateOut &out) {
    (void)cfg;
    C c = common_begin(spv::ExecutionModelFragment, kBindPsUbo);
    SpirvBuilder &b = c.b;
    UF &u = *c.u;
    c.b.execution_mode(spv::ExecutionModeOriginUpperLeft);

    auto input_v4 = [&](uint32_t loc) {
        uint32_t v = b.variable(spv::StorageClassInput, c.t_v4);
        b.decorate(v, spv::DecorationLocation, {loc});
        b.add_interface_var(spv::StorageClassInput, v);
        return b.load(c.t_v4, v);
    };
    uint32_t in_diff = input_v4(kLocColor0);
    uint32_t in_spec = input_v4(kLocColor1);
    uint32_t in_fog = input_v4(kLocFog);
    uint32_t in_tex[8];
    for (uint32_t i = 0; i < 8; i++) in_tex[i] = input_v4(kLocTexcoord0 + i);

    uint32_t o_col = b.variable(spv::StorageClassOutput, c.t_v4);
    b.decorate(o_col, spv::DecorationLocation, {0});
    b.add_interface_var(spv::StorageClassOutput, o_col);

    // samplers s0..s7 (os usados; bindings iguais ao SM3)
    uint32_t t_simg = b.type_sampled_image(
        b.type_image(c.t_f32, spv::Dim2D, false, false, false, 1, spv::ImageFormatUnknown));
    uint32_t smp[8];
    for (uint32_t i = 0; i < 8; i++) {
        smp[i] = b.variable(spv::StorageClassUniformConstant, t_simg);
        b.decorate(smp[i], spv::DecorationDescriptorSet, {0});
        b.decorate(smp[i], spv::DecorationBinding, {kBindSamplerBase + i});
    }

    uint32_t current = in_diff;   // stage 0: CURRENT = difusa
    uint32_t fog_src = in_fog;    // fator (vertex fog) ou profundidade (table)

    for (uint32_t s = 0; s < 8; s++) {
        uint32_t ca = u.slot(kFffPsStages + 2 * s);
        uint32_t cb = u.slot(kFffPsStages + 2 * s + 1);
        uint32_t a0 = c.x(ca), a1 = c.y(ca), cop = c.z(ca), a2 = c.w(ca);
        uint32_t b0 = c.x(cb), b1 = c.y(cb), aop = c.z(cb), b2 = c.w(cb);

        // args: valor D3DTA cru decodificado no shader
        // base: 0=CURRENT 1=TEXTURE? (D3DTA: 0 DIFFUSE, 1 CURRENT, 2 TEXTURE,
        // 3 TFACTOR, 4 SPECULAR, 5 TEMP) COMPLEMENT=0x10 ALPHAREPLICATE=0x20
        auto eval_arg = [&](uint32_t argf, uint32_t tex_sample) {
            uint32_t base = c.toint(argf);
            uint32_t basei = b.op(spv::OpBitwiseAnd, c.t_i32, {base, b.const_i32(0xF)});
            uint32_t v = current;
            for (uint32_t i = 0; i < 6; i++) {
                uint32_t cand = current;
                if (i == 1) cand = current;           // CURRENT (1)
                if (i == 0) cand = in_diff;           // DIFFUSE (0)
                if (i == 2) cand = tex_sample;        // TEXTURE (2)
                if (i == 3) cand = u.slot(kFffPsTfactor + s);  // TFACTOR (3)
                if (i == 4) cand = in_spec;           // SPECULAR (4)
                if (i == 5) cand = current;           // TEMP (5): sem suporte FF — current
                v = b.selectv(c.t_v4, 4, c.ieq(basei, (int32_t)i), cand, v);
            }
            // ALPHAREPLICATE
            uint32_t is_rep = b.op(spv::OpINotEqual, c.t_bool,
                                   {b.op(spv::OpBitwiseAnd, c.t_i32, {base, b.const_i32(0x20)}), b.const_i32(0)});
            uint32_t a = c.w(v);
            v = b.selectv(c.t_v4, 4, is_rep, b.composite_construct(c.t_v4, {a, a, a, a}), v);
            // COMPLEMENT
            uint32_t is_comp = b.op(spv::OpINotEqual, c.t_bool,
                                    {b.op(spv::OpBitwiseAnd, c.t_i32, {base, b.const_i32(0x10)}), b.const_i32(0)});
            uint32_t one4 = c.splat(1.0f);
            v = b.selectv(c.t_v4, 4, is_comp, b.fsub(c.t_v4, one4, v), v);
            return v;
        };

        // sample da textura da etapa (coordenada vec2 para 2D; o sampler e
        // uniform constant: OpLoad antes do sample)
        uint32_t smp_val = b.load(t_simg, smp[s]);
        uint32_t tex_sample = b.image_sample_implicit(
            c.t_v4, smp_val, b.vector_shuffle(c.t_v2, in_tex[s], in_tex[s], {0, 1}));

        // op color; a0 = ARG0 cru (MULTIPLYADD/LERP)
        auto apply_op = [&](uint32_t opf, uint32_t a0, uint32_t r1, uint32_t r2,
                            uint32_t tex_sample, uint32_t diff) {
            uint32_t op = c.toint(opf);
            uint32_t res = r1;   // SELECTARG1 (1) e DISABLE (2 -> resultado inalterado)
            auto op_eq = [&](int32_t o) { return c.ieq(op, o); };
            // D3DTOP: DISABLE=1 SELECTARG2=2 SELECTARG1=3 MODULATE=4 MODULATE2X=5
            // MODULATE4X=6 ADD=7 ADDSIGNED=8 ADDSIGNED2X=9 SUBTRACT=10 ADDSMOOTH=11
            // BLENDDIFFUSEALPHA=12 BLENDTEXTUREALPHA=13 BLENDFACTORALPHA=14
            // BLENDCURRENTALPHA=15 LERP=18? (LERP=18? nao: 17=BLENDCURRENTALPHA?
            // valores reais: DISABLE=1, SELECTARG2=2, SELECTARG1=3, MODULATE=4,
            // MODULATE2X=5, MODULATE4X=6, ADD=7, ADDSIGNED=8, ADDSIGNED2X=9,
            // SUBTRACT=10, ADDSMOOTH=11, BLENDDIFFUSEALPHA=12, BLENDTEXTUREALPHA=13,
            // BLENDFACTORALPHA=14, BLENDCURRENTALPHA=15, MODULATEALPHA_ADDCOLOR=16,
            // MODULATECOLOR_ADDALPHA=17, MODULATEINVALPHA_ADDCOLOR=18,
            // MODULATEINVCOLOR_ADDALPHA=19, BUMPENVMAP=20, ..., DOTPRODUCT3=24,
            // MULTIPLYADD=26, LERP=27
            uint32_t half = c.splat(0.5f);
            res = b.selectv(c.t_v4, 4, op_eq(4), b.fmul(c.t_v4, r1, r2), res);
            res = b.selectv(c.t_v4, 4, op_eq(5), b.fmul(c.t_v4, b.fmul(c.t_v4, r1, r2), c.splat(2.0f)), res);
            res = b.selectv(c.t_v4, 4, op_eq(6), b.fmul(c.t_v4, b.fmul(c.t_v4, r1, r2), c.splat(4.0f)), res);
            res = b.selectv(c.t_v4, 4, op_eq(7), b.fadd(c.t_v4, r1, r2), res);
            res = b.selectv(c.t_v4, 4, op_eq(8), b.fsub(c.t_v4, b.fadd(c.t_v4, r1, r2), half), res);
            res = b.selectv(c.t_v4, 4, op_eq(9), b.fmul(c.t_v4, b.fsub(c.t_v4, b.fadd(c.t_v4, r1, r2), half), c.splat(2.0f)), res);
            res = b.selectv(c.t_v4, 4, op_eq(10), b.fsub(c.t_v4, r1, r2), res);
            res = b.selectv(c.t_v4, 4, op_eq(11), b.fadd(c.t_v4, b.fmul(c.t_v4, b.fsub(c.t_v4, c.splat(1.0f), r1), r2), r1), res);
            res = b.selectv(c.t_v4, 4, op_eq(12), b.ext(c.t_v4, G450_FMix, {r1, r2, c.splatv(c.w(diff))}), res);
            res = b.selectv(c.t_v4, 4, op_eq(13), b.ext(c.t_v4, G450_FMix, {r1, r2, c.splatv(c.w(tex_sample))}), res);
            res = b.selectv(c.t_v4, 4, op_eq(14), b.ext(c.t_v4, G450_FMix, {r1, r2, c.splat(c.w(u.slot(kFffPsTfactor + s)))}), res);
            res = b.selectv(c.t_v4, 4, op_eq(15), b.ext(c.t_v4, G450_FMix, {r1, r2, c.splatv(c.w(current))}), res);
            // MODULATEALPHA_ADDCOLOR: rgb=r1.rgb*r2.a + r2.rgb; a=r1.a*r2.a
            {
                uint32_t r2a = c.splatv(c.w(r2));
                uint32_t rgb = b.fadd(c.t_v4, b.fmul(c.t_v4, r1, r2a), r2);
                res = b.selectv(c.t_v4, 4, op_eq(16), rgb, res);
            }
            // MODULATECOLOR_ADDALPHA: rgb=r1.rgb*r2.rgb + r1.a; a=r1.a
            {
                uint32_t rgb = b.fadd(c.t_v4, b.fmul(c.t_v4, r1, r2), c.splatv(c.w(r1)));
                res = b.selectv(c.t_v4, 4, op_eq(17), rgb, res);
            }
            // MODULATEINVALPHA_ADDCOLOR: rgb=r1.rgb*(1-r2.a)+r2.rgb
            {
                uint32_t ia = c.splat(b.fsub(c.t_f32, b.const_f32(1.0f), c.w(r2)));
                uint32_t rgb = b.fadd(c.t_v4, b.fmul(c.t_v4, r1, ia), r2);
                res = b.selectv(c.t_v4, 4, op_eq(18), rgb, res);
            }
            // MODULATEINVCOLOR_ADDALPHA: rgb=r1.rgb*(1-r1.rgb)+r2.rgb
            {
                uint32_t inv = b.fsub(c.t_v4, c.splat(1.0f), r1);
                uint32_t rgb = b.fadd(c.t_v4, b.fmul(c.t_v4, r1, inv), r2);
                res = b.selectv(c.t_v4, 4, op_eq(19), rgb, res);
            }
            // DOTPRODUCT3: dot(r1.rgb, r2.rgb) replicado (sem normalizar —
            // semantica D3D9)
            {
                uint32_t d = b.dot(c.t_f32, c.v3(r1), c.v3(r2));
                res = b.selectv(c.t_v4, 4, op_eq(24), b.composite_construct(c.t_v4, {d, d, d, d}), res);
            }
            // MULTIPLYADD: dest = arg0*arg1 + arg2 (a0 vem do slot .x)
            {
                uint32_t arg0 = eval_arg(a0, tex_sample);
                uint32_t mad = b.fma(c.t_v4, arg0, r1, r2);
                res = b.selectv(c.t_v4, 4, op_eq(26), mad, res);
            }
            // LERP: dest = arg0*(1-arg2) + arg1*arg2
            {
                uint32_t arg0 = eval_arg(a0, tex_sample);
                uint32_t l = b.ext(c.t_v4, G450_FMix, {arg0, r1, r2});
                res = b.selectv(c.t_v4, 4, op_eq(27), l, res);
            }
            res = b.selectv(c.t_v4, 4, op_eq(2), r2, res);   // SELECTARG2
            res = b.selectv(c.t_v4, 4, op_eq(3), r1, res);   // SELECTARG1
            return res;
        };

        uint32_t arg1c = eval_arg(a1, tex_sample);
        uint32_t arg2c = eval_arg(a2, tex_sample);
        uint32_t new_color = apply_op(cop, a0, arg1c, arg2c, tex_sample, in_diff);
        // alpha op (argumentos de alpha proprios: ALPHAARG0/1/2)
        uint32_t a_arg1 = eval_arg(b1, tex_sample);
        uint32_t a_arg2 = eval_arg(b2, tex_sample);
        uint32_t new_alpha = apply_op(aop, b0, a_arg1, a_arg2, tex_sample, in_diff);
        // combina: rgb do color op, a do alpha op
        uint32_t combined = b.composite_construct(c.t_v4,
            {c.x(new_color), c.y(new_color), c.z(new_color), c.w(new_alpha)});
        // DISABLE (op==1) para o color op: mantem current
        uint32_t disabled = c.ieq(c.toint(cop), 1);
        current = b.selectv(c.t_v4, 4, disabled, current, combined);
        // parar: as proximas etapas tambem vao ver op DISABLE e manter current —
        // sem branch (a cadeia e puramente funcional).
    }

    // fog: slot = (start, end, tmode|vmode<<8, density). tmode != 0 -> table fog
    // (fator calculado aqui da profundidade que o VS passou em oFog.x);
    // tmode == 0 com vmode != 0 -> vertex fog (fator direto do VS); ambos 0 ->
    // sem fog.
    uint32_t fogc = u.slot(kFffPsFogColor);
    uint32_t fogp = u.slot(kFffPsFog);
    uint32_t tvm = c.toint(c.z(fogp));
    uint32_t tmode = b.op(spv::OpBitwiseAnd, c.t_i32, {tvm, b.const_i32(0xFF)});
    uint32_t vmode_ps = b.op(spv::OpBitwiseAnd, c.t_i32,
                             {b.op(spv::OpShiftRightLogical, c.t_i32, {tvm, b.const_i32(8)}),
                              b.const_i32(0xFF)});
    {
        uint32_t start = c.x(fogp), end = c.y(fogp), density = c.w(fogp);
        uint32_t dist = c.x(fog_src);
        uint32_t lin = b.fdiv(c.t_f32, b.fsub(c.t_f32, end, dist), b.fsub(c.t_f32, end, start));
        uint32_t one = b.const_f32(1.0f);
        uint32_t fog_e = b.fdiv(c.t_f32, one,
                                b.ext(c.t_f32, G450_Exp, {b.fneg(c.t_f32, b.fmul(c.t_f32, dist, density))}));
        uint32_t dd = b.fmul(c.t_f32, dist, density);
        uint32_t fog_e2 = b.fdiv(c.t_f32, one,
                                 b.ext(c.t_f32, G450_Exp, {b.fneg(c.t_f32, b.fmul(c.t_f32, dd, dd))}));
        uint32_t f1 = c.iset(c.ieq(tmode, 1), lin, one);
        uint32_t f2 = b.select(c.t_f32, c.ieq(tmode, 2), fog_e, f1);
        uint32_t f3 = b.select(c.t_f32, c.ieq(tmode, 3), fog_e2, f2);
        // fator final: table usa a formula; vertex usa o fator do VS (oFog.x)
        uint32_t factor = b.select(c.t_f32, c.ieq(tmode, 0), c.x(fog_src), f3);
        // tmode/vmode sao conhecidos na geracao (assinatura do pipeline): o fog
        // entra ou nao por constante; o fator e dinamico via oFog.
        uint32_t fog_en = (tmode != 0 || vmode_ps != 0) ? b.const_bool(true) : b.const_bool(false);
        uint32_t fogged = b.ext(c.t_v4, G450_FMix,
                                {b.composite_construct(c.t_v4, {c.x(fogc), c.y(fogc), c.z(fogc), c.w(current)}),
                                 current, c.splatv(factor)});
        current = b.selectv(c.t_v4, 4, fog_en, fogged, current);
    }

    // alpha test (sys.x) — op uniform-driven
    uint32_t sys = u.slot(kFffPsSys);
    uint32_t aref = c.x(sys);
    uint32_t aout = c.w(current);
    {
        uint32_t cmp = c.toint(c.y(sys));
        uint32_t cond = b.op(spv::OpFOrdLessThan, c.t_bool, {aout, aref});   // 1=LESS
        spv::Op ops[6] = {spv::OpFOrdLessThan, spv::OpFOrdEqual, spv::OpFOrdLessThanEqual,
                          spv::OpFOrdGreaterThan, spv::OpFOrdNotEqual, spv::OpFOrdGreaterThanEqual};
        for (uint32_t i = 1; i <= 6; i++)
            cond = b.select(c.t_bool, c.ieq(cmp, (int32_t)i),
                            b.op(ops[i - 1], c.t_bool, {aout, aref}), cond);
        // 0=NEVER (falha sempre), 7=ALWAYS
        cond = b.select(c.t_bool, c.ieq(cmp, 0), b.const_bool(false), cond);
        cond = b.select(c.t_bool, c.ieq(cmp, 7), b.const_bool(true), cond);
        uint32_t keep = b.op(spv::OpLogicalNot, c.t_bool, {cond});
        uint32_t merge = b.label(), kill_block = b.label();
        b.selection_merge(merge);
        b.branch_cond(keep, kill_block, merge);
        b.label_bind(kill_block);
        b.kill();
        b.label_bind(merge);
    }

    b.store(o_col, current);
    b.ret();
    b.function_end();
    out.spirv = b.finish();
    out.max_const = kFffPsSys + 1;
    delete c.u;
    return true;
}

} // namespace nvr
