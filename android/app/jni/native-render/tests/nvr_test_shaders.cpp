// Harness desktop do native-render: valida o tradutor de bytecode D3D9
// (SM2/SM3) e o gerador fixed-function emitindo SPIR-V real para depois ser
// validado com spirv-val (estrutural, no host, sem GPU).
//
// Saida: .spv files em NVR_SPV_DIR (default /tmp/nvr-spv) + resumo no stdout.
// Exit code = numero de falhas de traducao.
#include "../../src/shader/nvr_dx9bc.h"
#include "../../src/shader/nvr_compile.h"
#include "../../src/shader/nvr_fff.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <sys/stat.h>

using namespace nvr;

// ---------------------------------------------------------------- assembler
// Monta bytecode D3D9 real (tokens de d3d9types.h) para exercitar o parser.
struct Asm {
    std::vector<uint32_t> w;
    uint32_t instr_count = 0;

    void begin(bool vs) {
        w.clear();
        instr_count = 0;
        w.push_back((vs ? 0xFFFE0000u : 0xFFFF0000u) | (3 << 8) | 0);   // vs_3_0/ps_3_0
        w.push_back(0);   // count (preenchido no end)
    }
    // tipos de registrador (valores de D3DSPR_*)
    static constexpr uint32_t R_TEMP = 0, R_INPUT = 1, R_CONST = 2, R_ADDR = 3, R_RASTOUT = 4,
                              R_ATTROUT = 5, R_OUTPUT = 6, R_CONSTINT = 7, R_COLOROUT = 8,
                              R_SAMPLER = 10, R_CONSTBOOL = 14, R_LOOP = 15, R_LABEL = 18,
                              R_PREDICATE = 19;
    // usages (D3DDECLUSAGE_*)
    static constexpr uint32_t U_POSITION = 0, U_NORMAL = 3, U_TEXCOORD = 5, U_COLOR = 10;

    // tipo de registrador ocupa 5 bits: (type&7) nos bits 28-30 e (type>>3)
    // nos bits 11-12 (D3DSPR_SAMPLER/CONSTBOOL/LOOP/LABEL/PREDICATE usam o par alto)
    static uint32_t reg_token(uint32_t type, uint32_t num) {
        return ((type & 7) << 28) | (((type >> 3) & 3) << 11) | (num & 0x7FF);
    }
    static uint32_t dst(uint32_t type, uint32_t num, uint32_t wm = 0xF, bool sat = false) {
        return reg_token(type, num) | ((wm & 0xF) << 16) | (sat ? (1u << 20) : 0);
    }
    static uint32_t src(uint32_t type, uint32_t num, uint32_t swz = 0xE4, uint32_t smod = 0) {
        // smod: valor D3DSPSM_* cru (0-13); o token guarda nos bits 24-27
        return reg_token(type, num) | ((swz & 0xFF) << 16) | ((smod & 0xF) << 24);
    }
    static uint32_t src_rel(uint32_t type, uint32_t num, uint32_t swz = 0xE4) {
        // D3DSHADER_ADDRMODE_RELATIVE (bit 13); o token do aL vem em seguida
        return reg_token(type, num) | ((swz & 0xFF) << 16) | (1u << 13);
    }
    static uint32_t aL_token() {
        // a0 (D3DSPR_ADDR = 3), swizzle .x
        return reg_token(3, 0) | ((0xE4 & 0xFF) << 16);
    }
    static uint32_t cmp_token(uint32_t op, uint32_t cmp) {
        return 0x80000000u | (op & 0xFFFF) | ((cmp & 0x7) << 16);
    }
    static uint32_t instr(uint32_t op) { return 0x80000000u | (op & 0xFFFF); }

    void op0(uint32_t op) { w.push_back(instr(op)); instr_count++; }
    void op1(uint32_t op, uint32_t d) { w.push_back(instr(op)); w.push_back(d); instr_count++; }
    void op2(uint32_t op, uint32_t d, uint32_t s0) {
        w.push_back(instr(op)); w.push_back(d); w.push_back(s0); instr_count++;
    }
    void op3(uint32_t op, uint32_t d, uint32_t s0, uint32_t s1) {
        w.push_back(instr(op)); w.push_back(d); w.push_back(s0); w.push_back(s1); instr_count++;
    }
    void op4(uint32_t op, uint32_t d, uint32_t s0, uint32_t s1, uint32_t s2) {
        w.push_back(instr(op)); w.push_back(d); w.push_back(s0); w.push_back(s1); w.push_back(s2);
        instr_count++;
    }
    void op5(uint32_t op, uint32_t d, uint32_t s0, uint32_t s1, uint32_t s2, uint32_t s3) {
        w.push_back(instr(op)); w.push_back(d); w.push_back(s0); w.push_back(s1); w.push_back(s2);
        w.push_back(s3); instr_count++;
    }
    void add_token(uint32_t t) { w.push_back(t); }   // para operandos extras (aL)
    // op com src0 relativo: o token do aL segue IMEDIATAMENTE o operando relativo
    void op3_rel(uint32_t op, uint32_t d, uint32_t s0_rel, uint32_t s1) {
        w.push_back(instr(op)); w.push_back(d); w.push_back(s0_rel);
        w.push_back(aL_token()); w.push_back(s1); instr_count++;
    }
    void op_setp_rel(uint32_t cmp, uint32_t d, uint32_t s0, uint32_t s1_rel) {
        w.push_back(cmp_token(86 /*SETP*/, cmp)); w.push_back(d); w.push_back(s0);
        w.push_back(s1_rel); w.push_back(aL_token()); instr_count++;
    }
    void op_ifc(uint32_t cmp, uint32_t s0, uint32_t s1) {
        w.push_back(cmp_token(42 /*IFC*/, cmp)); w.push_back(s0); w.push_back(s1); instr_count++;
    }
    void op_breakc(uint32_t cmp, uint32_t s0, uint32_t s1) {
        w.push_back(cmp_token(46 /*BREAKC*/, cmp)); w.push_back(s0); w.push_back(s1); instr_count++;
    }
    void op_setp(uint32_t cmp, uint32_t d, uint32_t s0, uint32_t s1) {
        w.push_back(cmp_token(86 /*SETP*/, cmp)); w.push_back(d); w.push_back(s0); w.push_back(s1);
        instr_count++;
    }
    void def(uint32_t reg, float a, float b, float c, float dd) {
        w.push_back(instr(1 /*DEF*/));
        w.push_back(dst(R_CONST, reg));
        float f[4] = {a, b, c, dd};
        uint32_t bits[4];
        memcpy(bits, f, 16);
        w.push_back(bits[0]); w.push_back(bits[1]); w.push_back(bits[2]); w.push_back(bits[3]);
        instr_count++;
    }
    void defi(uint32_t reg, int a, int b, int c, int d) {
        w.push_back(instr(49 /*DEFI*/));
        w.push_back(dst(R_CONSTINT, reg));
        w.push_back((uint32_t)a); w.push_back((uint32_t)b); w.push_back((uint32_t)c);
        w.push_back((uint32_t)d);
        instr_count++;
    }
    void defb(uint32_t reg, uint32_t v) {
        w.push_back(instr(48 /*DEFB*/));
        w.push_back(dst(R_CONSTBOOL, reg));
        w.push_back(v);
        instr_count++;
    }
    void dcl_input(uint32_t usage, uint32_t idx, uint32_t reg_type, uint32_t num) {
        w.push_back(instr(32 /*DCL*/));
        w.push_back((usage & 0xF) | ((idx & 0xF) << 16));
        w.push_back(dst(reg_type, num));
        instr_count++;
    }
    void dcl_sampler(uint32_t stt, uint32_t num) {
        w.push_back(instr(32 /*DCL*/));
        w.push_back((stt & 0xF) << 27);
        w.push_back(dst(R_SAMPLER, num));
        instr_count++;
    }
    void dcl_output(uint32_t usage, uint32_t idx, uint32_t num) {
        dcl_input(usage, idx, R_OUTPUT, num);
    }
    std::vector<uint32_t> end() {
        // campo de comprimento D3D9 = DWORDs do corpo (inclui o token END)
        w.push_back(0x0000FFFFu);   // end
        w[1] = (uint32_t)w.size() - 2;
        return w;
    }
};

// ---------------------------------------------------------------- registro
static int g_failures = 0;
static int g_total = 0;
static const char *g_dir = nullptr;

static void write_spv(const std::string &name, const std::vector<uint32_t> &spirv) {
    char path[512];
    snprintf(path, sizeof path, "%s/%s.spv", g_dir, name.c_str());
    FILE *f = fopen(path, "wb");
    if (f) {
        fwrite(spirv.data(), 4, spirv.size(), f);
        fclose(f);
    }
}

static bool emit_bc(const char *name, bool is_vs, const BcShader &bc, const uint8_t *in_comps,
                    bool alpha_test = false, uint32_t alpha_cmp = 4) {
    g_total++;
    XlateOut out;
    bool ok;
    if (is_vs) {
        VsXlateIn in{&bc, in_comps, false};
        ok = translate_vs(in, out);
    } else {
        PsXlateIn in{&bc, alpha_test, alpha_cmp};
        ok = translate_ps(in, out);
    }
    if (!ok || out.spirv.empty()) {
        printf("FAIL %-34s traducao: %s\n", name, out.error.c_str());
        g_failures++;
        return false;
    }
    write_spv(name, out.spirv);
    printf("ok   %-34s %zu palavras\n", name, out.spirv.size());
    return true;
}

static bool assemble(const char *name, bool is_vs, std::vector<uint32_t> code,
                     const uint8_t *in_comps = nullptr, bool alpha_test = false,
                     uint32_t alpha_cmp = 4) {
    BcShader bc;
    std::string err;
    g_total++;
    if (!bc_parse(code.data(), code.size(), bc, err)) {
        printf("FAIL %-34s parse: %s\n", name, err.c_str());
        printf("  tokens(%zu):", code.size());
        for (size_t i = 0; i < code.size() && i < 80; i++) printf(" %08x", code[i]);
        printf("\n");
        g_failures++;
        return false;
    }
    g_total--;   // emit_bc conta de novo
    return emit_bc(name, is_vs, bc, in_comps, alpha_test, alpha_cmp);
}

static bool emit_fff(const char *name, const FffVsCfg &vs_cfg, const FffPsCfg &ps_cfg) {
    g_total++;
    bool any_fail = false;
    {
        XlateOut out;
        if (!generate_fff_vs(vs_cfg, out)) {
            printf("FAIL %s fff-vs: %s\n", name, out.error.c_str());
            any_fail = true;
        } else {
            write_spv(std::string(name) + "_vs", out.spirv);
        }
    }
    {
        XlateOut out;
        if (!generate_fff_ps(ps_cfg, out)) {
            printf("FAIL %s fff-ps: %s\n", name, out.error.c_str());
            any_fail = true;
        } else {
            write_spv(std::string(name) + "_ps", out.spirv);
        }
    }
    if (!any_fail) printf("ok   %-34s fff vs+ps\n", name);
    if (any_fail) g_failures++;
    return !any_fail;
}

// assinatura de inputs usada nos casos FFF (position3, normal3, color0(4),
// texcoord0(2), texcoord1(2))
static uint8_t g_comps[32];

static void fill_comps() {
    memset(g_comps, 0, 32);
    g_comps[kLocPosition] = 3;
    g_comps[kLocNormal] = 3;
    g_comps[kLocColor0] = 4;
    g_comps[kLocTexcoord0] = 2;
    g_comps[kLocTexcoord0 + 1] = 2;
}

int main(int argc, char **argv) {
    g_dir = argc > 1 ? argv[1] : "/tmp/nvr-spv";
    mkdir(g_dir, 0755);
    fill_comps();

    // ---------------- VS 1: T&L basico com DEF + m4x4 + dp3
    {
        Asm a;
        a.begin(true);
        a.def(0, 1, 0, 0, 0); a.def(1, 0, 1, 0, 0); a.def(2, 0, 0, 1, 0); a.def(3, 0, 0, 0, 1);
        a.def(4, 0.25f, 0.5f, 0.75f, 1.0f);
        a.dcl_input(Asm::U_POSITION, 0, Asm::R_INPUT, 0);
        a.dcl_input(Asm::U_NORMAL, 0, Asm::R_INPUT, 1);
        a.dcl_output(Asm::U_POSITION, 0, 0);
        a.dcl_output(Asm::U_COLOR, 0, 0);
        a.op3(21 /*M4x4*/, Asm::dst(Asm::R_RASTOUT, 0), Asm::src(Asm::R_INPUT, 0),
              Asm::src(Asm::R_CONST, 0));
        a.op3(9 /*DP3*/, Asm::dst(Asm::R_TEMP, 0, 1), Asm::src(Asm::R_INPUT, 1),
              Asm::src(Asm::R_CONST, 4));
        a.op2(2 /*MOV*/, Asm::dst(Asm::R_COLOROUT, 0),
              Asm::src(Asm::R_TEMP, 0, 0x00));   // r0.x replicado
        assemble("vs_basic_tl", true, a.end());
    }

    // ---------------- VS 2: loop com aL + BREAK dentro de IFC (cascata)
    {
        Asm a;
        a.begin(true);
        a.defi(0, 3, 0, 1, 0);   // i0 = (count 3, start 0, step 1)
        a.def(10, 2.0f, 0.0f, 0.0f, 0.0f);
        a.dcl_input(Asm::U_POSITION, 0, Asm::R_INPUT, 0);
        a.dcl_output(Asm::U_POSITION, 0, 0);
        a.dcl_output(Asm::U_COLOR, 0, 0);
        a.op2(2, Asm::dst(Asm::R_TEMP, 1), Asm::src(Asm::R_CONST, 10));
        a.op2(28 /*LOOP*/, Asm::dst(Asm::R_LOOP, 0), Asm::src(Asm::R_CONSTINT, 0));
        {
            // add r0, c[aL + 5], r1 (aL vem logo apos o operando relativo)
            a.op3_rel(3 /*ADD*/, Asm::dst(Asm::R_TEMP, 0), Asm::src_rel(Asm::R_CONST, 5),
                      Asm::src(Asm::R_TEMP, 1));
            // ifc_lt r0.x, -r1.x
            a.op_ifc(1 /*LT*/, Asm::src(Asm::R_TEMP, 0, 0x00),
                     Asm::src(Asm::R_TEMP, 1, 0x00, 1 /*NEG*/));
            {
                a.op0(45 /*BREAK*/);   // break incondicional DENTRO do if
                a.op2(2, Asm::dst(Asm::R_TEMP, 2), Asm::src(Asm::R_CONST, 10));   // morto
            }
            a.op0(44 /*ENDIF*/);
        }
        a.op0(30 /*ENDLOOP*/);
        a.op2(2, Asm::dst(Asm::R_RASTOUT, 0), Asm::src(Asm::R_INPUT, 0));
        a.op2(2, Asm::dst(Asm::R_COLOROUT, 0), Asm::src(Asm::R_TEMP, 0));
        assemble("vs_loop_break_if", true, a.end());
    }

    // ---------------- VS 3: mova + rep + setp + breakp
    {
        Asm a;
        a.begin(true);
        a.defi(0, 2, 0, 1, 0);
        a.dcl_input(Asm::U_POSITION, 0, Asm::R_INPUT, 0);
        a.dcl_output(Asm::U_POSITION, 0, 0);
        a.dcl_output(Asm::U_TEXCOORD, 0, 0);
        a.op2(47 /*MOVA*/, Asm::dst(Asm::R_ADDR, 0, 1), Asm::src(Asm::R_INPUT, 0));
        a.op1(39 /*REP*/, Asm::src(Asm::R_CONSTINT, 0));   // rep i# (sem dest)
        {
            a.op_setp_rel(1 /*LT*/, Asm::dst(Asm::R_PREDICATE, 0, 1),
                          Asm::src(Asm::R_TEMP, 0, 0x00), Asm::src_rel(Asm::R_CONST, 0, 0x00));
            a.op1(88 /*BREAKP*/, Asm::dst(Asm::R_PREDICATE, 0, 1));
        }
        a.op0(40 /*ENDREP*/);
        a.op2(2, Asm::dst(Asm::R_RASTOUT, 0), Asm::src(Asm::R_INPUT, 0));
        a.op2(2, Asm::dst(Asm::R_ATTROUT, 0), Asm::src(Asm::R_INPUT, 0));
        assemble("vs_mova_setp_breakp", true, a.end());
    }

    // ---------------- VS 4: call/label/ret + sincos + pow + srcmods
    {
        Asm a;
        a.begin(true);
        a.dcl_input(Asm::U_POSITION, 0, Asm::R_INPUT, 0);
        a.dcl_output(Asm::U_POSITION, 0, 0);
        a.dcl_output(Asm::U_TEXCOORD, 0, 0);
        a.def(20, 1.5707963f, 0, 0, 0);
        a.op1(26 /*CALL*/, Asm::dst(Asm::R_LABEL, 1));
        a.op2(38 /*SINCOS*/, Asm::dst(Asm::R_TEMP, 0, 0x3), Asm::src(Asm::R_CONST, 20));
        // sincos tem 2 operandos dummy depois (c#, c#+1) — o parser le e ignora
        a.add_token(Asm::src(Asm::R_CONST, 30));
        a.add_token(Asm::src(Asm::R_CONST, 31));
        a.op3(33 /*POW*/, Asm::dst(Asm::R_TEMP, 1),
              Asm::src(Asm::R_TEMP, 0, 0x00, 11 /*ABS*/), Asm::src(Asm::R_CONST, 20, 0x55));
        a.op2(2, Asm::dst(Asm::R_RASTOUT, 0), Asm::src(Asm::R_INPUT, 0));
        a.op2(2, Asm::dst(Asm::R_ATTROUT, 0), Asm::src(Asm::R_TEMP, 0));
        a.op0(29 /*RET*/);
        a.op1(31 /*LABEL*/, Asm::dst(Asm::R_LABEL, 1));
        a.op3(10 /*DP4*/, Asm::dst(Asm::R_TEMP, 3), Asm::src(Asm::R_INPUT, 0),
              Asm::src(Asm::R_CONST, 20));
        a.op0(29 /*RET*/);
        assemble("vs_call_sincos_pow", true, a.end());
    }

    // ---------------- VS 5: texldl (vs_3_0) + cube
    {
        Asm a;
        a.begin(true);
        a.dcl_input(Asm::U_POSITION, 0, Asm::R_INPUT, 0);
        a.dcl_input(Asm::U_TEXCOORD, 0, Asm::R_INPUT, 1);
        a.dcl_sampler(2 /*2D*/, 0);
        a.dcl_sampler(3 /*CUBE*/, 1);
        a.dcl_output(Asm::U_POSITION, 0, 0);
        a.dcl_output(Asm::U_COLOR, 0, 0);
        a.op3(87 /*TEXLDL*/, Asm::dst(Asm::R_TEMP, 0), Asm::src(Asm::R_INPUT, 1),
              Asm::src(Asm::R_SAMPLER, 0));
        a.op3(87, Asm::dst(Asm::R_TEMP, 1), Asm::src(Asm::R_INPUT, 1),
              Asm::src(Asm::R_SAMPLER, 1));
        a.op2(2, Asm::dst(Asm::R_RASTOUT, 0), Asm::src(Asm::R_INPUT, 0));
        a.op2(2, Asm::dst(Asm::R_COLOROUT, 0), Asm::src(Asm::R_TEMP, 0));
        assemble("vs_texldl_2d_cube", true, a.end());
    }

    // ---------------- PS 1: texld/texkill/cmp/lrp + alpha test
    {
        Asm a;
        a.begin(false);
        a.dcl_input(Asm::U_COLOR, 0, Asm::R_INPUT, 0);
        a.dcl_input(Asm::U_TEXCOORD, 0, Asm::R_INPUT, 1);
        a.dcl_sampler(2 /*2D*/, 0);
        a.def(0, 0.5f, 0.5f, 0.5f, 1.0f);
        a.op3(66 /*TEXLD*/, Asm::dst(Asm::R_TEMP, 0), Asm::src(Asm::R_INPUT, 1),
              Asm::src(Asm::R_SAMPLER, 0));
        a.op1(65 /*TEXKILL*/, Asm::dst(Asm::R_TEMP, 1));
        a.op4(82 /*CMP*/, Asm::dst(Asm::R_TEMP, 2), Asm::src(Asm::R_TEMP, 0, 0x00, 1 /*NEG*/),
              Asm::src(Asm::R_CONST, 0), Asm::src(Asm::R_TEMP, 0));
        a.op4(19 /*LRP*/, Asm::dst(Asm::R_TEMP, 3), Asm::src(Asm::R_INPUT, 0, 0xFF),
              Asm::src(Asm::R_TEMP, 2), Asm::src(Asm::R_TEMP, 0));
        a.op2(2, Asm::dst(Asm::R_COLOROUT, 0), Asm::src(Asm::R_TEMP, 3));
        assemble("ps_texld_kill_cmp_lrp", false, a.end(), nullptr, true, 4 /*GREATER*/);
    }

    // ---------------- PS 2: dp2add + dsx/dsy + texldb + texldd + cube
    {
        Asm a;
        a.begin(false);
        a.dcl_input(Asm::U_TEXCOORD, 0, Asm::R_INPUT, 1);
        a.dcl_sampler(2 /*2D*/, 0);
        a.dcl_sampler(3 /*CUBE*/, 1);
        a.dcl_output(Asm::U_COLOR, 0, 0);
        a.op3(66, Asm::dst(Asm::R_TEMP, 0), Asm::src(Asm::R_INPUT, 1), Asm::src(Asm::R_SAMPLER, 0));
        a.w[a.w.size() - 4] |= (2u << 16);   // D3DSI_TEXLD_BIAS
        a.op4(83 /*DP2ADD*/, Asm::dst(Asm::R_TEMP, 1), Asm::src(Asm::R_TEMP, 0),
              Asm::src(Asm::R_INPUT, 1), Asm::src(Asm::R_TEMP, 0));
        a.op2(89 /*DSX*/, Asm::dst(Asm::R_TEMP, 2), Asm::src(Asm::R_TEMP, 0));
        a.op2(90 /*DSY*/, Asm::dst(Asm::R_TEMP, 3), Asm::src(Asm::R_TEMP, 0));
        // texldd: dest, coords, dx, dy, sampler (ordem do spec D3D9)
        a.op5(85 /*TEXLDD*/, Asm::dst(Asm::R_TEMP, 4), Asm::src(Asm::R_INPUT, 1),
              Asm::src(Asm::R_TEMP, 2), Asm::src(Asm::R_TEMP, 3), Asm::src(Asm::R_SAMPLER, 1));
        a.op3(66, Asm::dst(Asm::R_TEMP, 5), Asm::src(Asm::R_INPUT, 1),
              Asm::src(Asm::R_SAMPLER, 1));
        a.op2(2, Asm::dst(Asm::R_COLOROUT, 0), Asm::src(Asm::R_TEMP, 4));
        assemble("ps_dp2add_dsx_texldb_ldd", false, a.end());
    }

    // ---------------- PS 3: DEFB + if b# + loop i#
    {
        Asm a;
        a.begin(false);
        a.dcl_input(Asm::U_COLOR, 0, Asm::R_INPUT, 0);
        a.defb(0, 1);
        a.defi(1, 2, 0, 1, 0);
        a.dcl_output(Asm::U_COLOR, 0, 0);
        a.op1(41 /*IF*/, Asm::src(Asm::R_CONSTBOOL, 0));
        {
            a.op2(28 /*LOOP*/, Asm::dst(Asm::R_LOOP, 0), Asm::src(Asm::R_CONSTINT, 1));
            {
                a.op2(2, Asm::dst(Asm::R_TEMP, 0), Asm::src(Asm::R_INPUT, 0));
            }
            a.op0(30 /*ENDLOOP*/);
        }
        a.op0(44 /*ENDIF*/);
        a.op2(2, Asm::dst(Asm::R_COLOROUT, 0), Asm::src(Asm::R_TEMP, 0));
        assemble("ps_ifb_loop", false, a.end());
    }

    // ---------------- PS 4: texld projetado + breakc em loop
    {
        Asm a;
        a.begin(false);
        a.dcl_input(Asm::U_TEXCOORD, 0, Asm::R_INPUT, 1);
        a.dcl_sampler(2 /*2D*/, 0);
        a.defi(2, 4, 0, 1, 0);
        a.dcl_output(Asm::U_COLOR, 0, 0);
        a.op3(66, Asm::dst(Asm::R_TEMP, 0), Asm::src(Asm::R_INPUT, 1), Asm::src(Asm::R_SAMPLER, 0));
        a.w[a.w.size() - 4] |= (1u << 16);   // D3DSI_TEXLD_PROJECT
        a.op2(28, Asm::dst(Asm::R_LOOP, 0), Asm::src(Asm::R_CONSTINT, 2));
        {
            a.op_breakc(6 /*GE*/, Asm::src(Asm::R_TEMP, 0, 0x00), Asm::src(Asm::R_CONST, 0, 0x00));
        }
        a.op0(30);
        a.op2(2, Asm::dst(Asm::R_COLOROUT, 0), Asm::src(Asm::R_TEMP, 0));
        assemble("ps_proj_breakc", false, a.end());
    }

    // ---------------- PS 5: multiplicidade de srcmods + writemasks parciais
    {
        Asm a;
        a.begin(false);
        a.dcl_input(Asm::U_COLOR, 0, Asm::R_INPUT, 0);
        a.def(0, 0.25f, 0.5f, 0.75f, 1.0f);
        a.dcl_output(Asm::U_COLOR, 0, 0);
        a.dcl_output(Asm::U_COLOR, 1, 1);
        // mad com todos os srcmods principais
        a.op4(5 /*MAD*/, Asm::dst(Asm::R_TEMP, 0, 0x7 /*xyz*/, true /*sat*/),
              Asm::src(Asm::R_INPUT, 0, 0xE4, 2 /*BIAS*/),
              Asm::src(Asm::R_CONST, 0, 0xE4, 7 /*X2*/),
              Asm::src(Asm::R_INPUT, 0, 0xE4, 6 /*COMP*/));
        // dst instruction
        a.op3(18 /*DST*/, Asm::dst(Asm::R_TEMP, 1), Asm::src(Asm::R_INPUT, 0),
              Asm::src(Asm::R_CONST, 0));
        // nrm
        a.op2(37 /*NRM*/, Asm::dst(Asm::R_TEMP, 2), Asm::src(Asm::R_INPUT, 0));
        // crs
        a.op3(34 /*CRS*/, Asm::dst(Asm::R_TEMP, 3), Asm::src(Asm::R_INPUT, 0),
              Asm::src(Asm::R_CONST, 0));
        // abs/sign
        a.op2(36 /*ABS*/, Asm::dst(Asm::R_TEMP, 4, 0x3), Asm::src(Asm::R_INPUT, 0));
        a.op2(2, Asm::dst(Asm::R_COLOROUT, 0), Asm::src(Asm::R_TEMP, 0));
        a.op2(2, Asm::dst(Asm::R_COLOROUT, 1, 0x8 /*a*/), Asm::src(Asm::R_TEMP, 1, 0xFF));
        assemble("ps_srcmods_masks", false, a.end());
    }

    // ---------------- FFF: quatro assinaturas cobrindo o espaco de estado ----
    {
        FffVsCfg vs{};
        FffPsCfg ps{};
        vs.in_comps = g_comps;
        vs.stage[0].coord_index = 0;
        ps.stage[0].color_arg1 = 2;   // TEXTURE
        ps.stage[0].color_arg2 = 1;   // CURRENT
        ps.stage[0].color_op = 4;     // MODULATE
        ps.stage[0].alpha_arg1 = 2;
        ps.stage[0].alpha_arg2 = 1;
        ps.stage[0].alpha_op = 4;
        ps.active_stages = 1;
        emit_fff("fff_min", vs, ps);
    }
    {
        FffVsCfg vs{};
        FffPsCfg ps{};
        vs.in_comps = g_comps;
        vs.lights[0].enabled = true;
        vs.lights[0].type = 1;   // POINT
        vs.lights[1].enabled = true;
        vs.lights[1].type = 3;   // DIRECTIONAL
        vs.lights[2].enabled = true;
        vs.lights[2].type = 2;   // SPOT
        vs.fog_vmode = 1;
        vs.specular_enable = true;
        vs.range_fog = true;
        ps.fog_vmode = 1;
        ps.stage[0].color_op = 7;    // ADD
        ps.stage[0].alpha_op = 3;    // SELECTARG1
        ps.stage[1].color_op = 5;    // MODULATE2X
        ps.stage[1].alpha_op = 1;    // DISABLE
        ps.active_stages = 2;
        emit_fff("fff_lights_fog", vs, ps);
    }
    {
        FffVsCfg vs{};
        FffPsCfg ps{};
        vs.in_comps = g_comps;
        vs.stage[0].coord_index = 0x00030000 | 2;   // TCI_CAMERASPACENORMAL | idx 2
        vs.stage[0].ttff = 2 | 0x10000;             // COUNT2 | PROJECTED
        vs.stage[0].has_texmat = true;
        for (int i = 0; i < 16; i++) vs.stage[0].texmat[i] = (i % 5 == 0) ? 1.0f : 0.0f;
        ps.fog_tmode = 2;   // EXP
        ps.stage[0].color_op = 13;   // BLENDTEXTUREALPHA
        ps.stage[0].color_arg2 = 0;  // DIFFUSE
        ps.active_stages = 1;
        emit_fff("fff_tci_proj_tablefog", vs, ps);
    }
    {
        FffVsCfg vs{};
        FffPsCfg ps{};
        vs.in_comps = g_comps;
        ps.stage[0].color_op = 26;   // MULTIPLYADD
        ps.stage[0].color_arg0 = 2;
        ps.stage[0].color_arg1 = 1;
        ps.stage[0].color_arg2 = 0;
        ps.stage[0].alpha_op = 27;   // LERP
        ps.stage[0].alpha_arg0 = 3;  // TFACTOR
        ps.stage[0].alpha_arg1 = 1;
        ps.stage[0].alpha_arg2 = 4;  // SPECULAR
        ps.stage[1].color_op = 24;   // DOTPRODUCT3
        ps.stage[1].color_arg1 = 2;
        ps.stage[1].color_arg2 = 1;
        ps.stage[1].alpha_op = 14;   // BLENDFACTORALPHA
        ps.active_stages = 2;
        emit_fff("fff_mad_lerp_dot3", vs, ps);
    }
    {
        // FFF: fog exp2 + alpha test + specular
        FffVsCfg vs{};
        FffPsCfg ps{};
        vs.in_comps = g_comps;
        vs.fog_vmode = 3;
        vs.fog_start = 10.0f;
        vs.fog_end = 100.0f;
        vs.fog_density = 0.02f;
        ps.fog_tmode = 3;
        ps.fog_start = 10.0f;
        ps.fog_end = 100.0f;
        ps.fog_density = 0.02f;
        ps.alpha_test = true;
        ps.alpha_cmp = 6;   // GREATEREQUAL
        ps.stage[0].color_op = 3;    // SELECTARG1
        ps.stage[0].alpha_op = 1;    // DISABLE
        ps.active_stages = 1;
        emit_fff("fff_exp2fog_alphatest", vs, ps);
    }

    printf("\n%d/%d casos geraram SPIR-V (%d falhas)\n", g_total - g_failures, g_total,
           g_failures);
    return g_failures;
}
