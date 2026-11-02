// IDirect3D9, caps D3D9, defaults de estado e helpers de empacotamento.
#include "nvr_device.h"
#include <SDL2/SDL.h>

namespace nvr {

// ---------------------------------------------------------------- defaults
void fill_default_state(DeviceState &s) {
    memset(s.rs, 0, sizeof s.rs);
    s.rs[D3DRS_ZENABLE] = D3DZB_FALSE;
    s.rs[D3DRS_FILLMODE] = D3DFILL_SOLID;
    s.rs[D3DRS_SHADEMODE] = D3DSHADE_GOURAUD;
    s.rs[D3DRS_ZWRITEENABLE] = TRUE;
    s.rs[D3DRS_ALPHATESTENABLE] = FALSE;
    s.rs[D3DRS_LASTPIXEL] = TRUE;
    s.rs[D3DRS_SRCBLEND] = D3DBLEND_ONE;
    s.rs[D3DRS_DESTBLEND] = D3DBLEND_ZERO;
    s.rs[D3DRS_CULLMODE] = D3DCULL_CCW;
    s.rs[D3DRS_ZFUNC] = D3DCMP_LESSEQUAL;
    s.rs[D3DRS_ALPHAFUNC] = D3DCMP_ALWAYS;
    s.rs[D3DRS_ALPHAREF] = 0;
    s.rs[D3DRS_DITHERENABLE] = FALSE;
    s.rs[D3DRS_ALPHABLENDENABLE] = FALSE;
    s.rs[D3DRS_FOGENABLE] = FALSE;
    s.rs[D3DRS_SPECULARENABLE] = FALSE;
    s.rs[D3DRS_FOGCOLOR] = 0;
    s.rs[D3DRS_FOGTABLEMODE] = D3DFOG_NONE;
    s.rs[D3DRS_FOGSTART] = 0;
    s.rs[D3DRS_FOGEND] = 0x3F800000;   // 1.0f como bits (D3D9 guarda float)
    s.rs[D3DRS_FOGDENSITY] = 0x3F800000;
    s.rs[D3DRS_RANGEFOGENABLE] = FALSE;
    s.rs[D3DRS_STENCILENABLE] = FALSE;
    s.rs[D3DRS_STENCILFAIL] = D3DSTENCILOP_KEEP;
    s.rs[D3DRS_STENCILZFAIL] = D3DSTENCILOP_KEEP;
    s.rs[D3DRS_STENCILPASS] = D3DSTENCILOP_KEEP;
    s.rs[D3DRS_STENCILFUNC] = D3DCMP_ALWAYS;
    s.rs[D3DRS_STENCILREF] = 0;
    s.rs[D3DRS_STENCILMASK] = 0xFFFFFFFFu;
    s.rs[D3DRS_STENCILWRITEMASK] = 0xFFFFFFFFu;
    s.rs[D3DRS_TEXTUREFACTOR] = 0xFFFFFFFFu;
    s.rs[D3DRS_WRAP0] = 0;
    s.rs[D3DRS_LIGHTING] = TRUE;
    s.rs[D3DRS_AMBIENT] = 0;
    s.rs[D3DRS_COLORVERTEX] = TRUE;
    s.rs[D3DRS_FOGVERTEXMODE] = D3DFOG_NONE;
    s.rs[D3DRS_CLIPPING] = TRUE;
    s.rs[D3DRS_LOCALVIEWER] = TRUE;
    s.rs[D3DRS_NORMALIZENORMALS] = FALSE;
    s.rs[D3DRS_DIFFUSEMATERIALSOURCE] = D3DMCS_COLOR1;
    s.rs[D3DRS_SPECULARMATERIALSOURCE] = D3DMCS_COLOR2;
    s.rs[D3DRS_AMBIENTMATERIALSOURCE] = D3DMCS_MATERIAL;
    s.rs[D3DRS_EMISSIVEMATERIALSOURCE] = D3DMCS_MATERIAL;
    s.rs[D3DRS_VERTEXBLEND] = D3DVBF_DISABLE;
    s.rs[D3DRS_POINTSIZE] = 0x3F800000;
    s.rs[D3DRS_POINTSIZE_MIN] = 0x3F800000;
    s.rs[D3DRS_POINTSIZE_MAX] = 0x42C80000;   // 96.0
    s.rs[D3DRS_POINTSPRITEENABLE] = FALSE;
    s.rs[D3DRS_POINTSCALEENABLE] = FALSE;
    s.rs[D3DRS_COLORWRITEENABLE] = 0xF;
    s.rs[D3DRS_BLENDOP] = D3DBLENDOP_ADD;
    s.rs[D3DRS_SCISSORTESTENABLE] = FALSE;
    s.rs[D3DRS_SLOPESCALEDEPTHBIAS] = 0;
    s.rs[D3DRS_DEPTHBIAS] = 0;
    s.rs[D3DRS_SEPARATEALPHABLENDENABLE] = FALSE;
    s.rs[D3DRS_SRCBLENDALPHA] = D3DBLEND_ONE;
    s.rs[D3DRS_DESTBLENDALPHA] = D3DBLEND_ZERO;
    s.rs[D3DRS_BLENDOPALPHA] = D3DBLENDOP_ADD;
    s.rs[D3DRS_TWOSIDEDSTENCILMODE] = FALSE;
    s.rs[D3DRS_CCW_STENCILFAIL] = D3DSTENCILOP_KEEP;
    s.rs[D3DRS_CCW_STENCILZFAIL] = D3DSTENCILOP_KEEP;
    s.rs[D3DRS_CCW_STENCILPASS] = D3DSTENCILOP_KEEP;
    s.rs[D3DRS_CCW_STENCILFUNC] = D3DCMP_ALWAYS;
    s.rs[D3DRS_MULTISAMPLEANTIALIAS] = TRUE;
    s.rs[D3DRS_MULTISAMPLEMASK] = 0xFFFFFFFFu;
    s.rs[D3DRS_BLENDFACTOR] = 0xFFFFFFFFu;

    for (uint32_t i = 0; i < 8; i++) {
        s.tss[i][D3DTSS_COLOROP] = i == 0 ? D3DTOP_MODULATE : D3DTOP_DISABLE;
        s.tss[i][D3DTSS_COLORARG0] = D3DTA_CURRENT;
        s.tss[i][D3DTSS_COLORARG1] = D3DTA_TEXTURE;
        s.tss[i][D3DTSS_COLORARG2] = D3DTA_CURRENT;
        s.tss[i][D3DTSS_ALPHAOP] = i == 0 ? D3DTOP_SELECTARG1 : D3DTOP_DISABLE;
        s.tss[i][D3DTSS_ALPHAARG0] = D3DTA_CURRENT;
        s.tss[i][D3DTSS_ALPHAARG1] = D3DTA_TEXTURE;
        s.tss[i][D3DTSS_ALPHAARG2] = D3DTA_CURRENT;
        s.tss[i][D3DTSS_BUMPENVMAT00] = 0;
        s.tss[i][D3DTSS_TEXCOORDINDEX] = i;
        s.tss[i][D3DTSS_TEXTURETRANSFORMFLAGS] = D3DTTFF_DISABLE;
    }
    for (uint32_t i = 0; i < 16; i++) {
        s.samp[i][D3DSAMP_ADDRESSU] = D3DTADDRESS_WRAP;
        s.samp[i][D3DSAMP_ADDRESSV] = D3DTADDRESS_WRAP;
        s.samp[i][D3DSAMP_ADDRESSW] = D3DTADDRESS_WRAP;
        s.samp[i][D3DSAMP_MAGFILTER] = D3DTEXF_POINT;
        s.samp[i][D3DSAMP_MINFILTER] = D3DTEXF_POINT;
        s.samp[i][D3DSAMP_MIPFILTER] = D3DTEXF_NONE;
        s.samp[i][D3DSAMP_MIPMAPLODBIAS] = 0;
        s.samp[i][D3DSAMP_MAXMIPLEVEL] = 0;
        s.samp[i][D3DSAMP_MAXANISOTROPY] = 1;
    }
    memset(s.xfm, 0, sizeof s.xfm);
    s.xfm[D3DTS_WORLD].m[0][0] = s.xfm[D3DTS_WORLD].m[1][1] = s.xfm[D3DTS_WORLD].m[2][2] =
        s.xfm[D3DTS_WORLD].m[3][3] = 1.0f;
    s.xfm[D3DTS_VIEW] = s.xfm[D3DTS_WORLD];
    s.xfm[D3DTS_PROJECTION] = s.xfm[D3DTS_WORLD];
    for (uint32_t i = 0; i < 8; i++) s.xfm[D3DTS_TEXTURE0 + i] = s.xfm[D3DTS_WORLD];
    memset(&s.mat, 0, sizeof s.mat);
    s.mat.Diffuse = {1, 1, 1, 1};
    s.mat.Ambient = {1, 1, 1, 1};
    s.mat.Power = 0;
    s.vp = {0, 0, 1280, 720, 0.0f, 1.0f};
    // objetos/estado de objeto que o Reset tambem precisa limpar: shaders,
    // texturas, streams, ib, decl, constantes (refs sao soltas pelo caller)
    s.vs = nullptr;
    s.ps = nullptr;
    s.ib = nullptr;
    s.decl = nullptr;
    s.fvf = 0;
    s.swvp = false;
    s.scene_open = false;
    memset(s.vf, 0, sizeof s.vf);
    memset(s.vi, 0, sizeof s.vi);
    memset(s.vbool, 0, sizeof s.vbool);
    memset(s.pf, 0, sizeof s.pf);
    memset(s.pi, 0, sizeof s.pi);
    memset(s.pb, 0, sizeof s.pb);
    memset(s.streams, 0, sizeof s.streams);
    for (uint32_t i = 0; i < 16; i++) s.tex[i] = nullptr;
    s.rt = nullptr;
    s.ds = nullptr;
}

// ------------------------------------------------------- packing de estado
static void packu(uint32_t &acc, uint32_t bits, uint32_t width) {
    acc = (acc << width) | (bits & ((1u << width) - 1));
}

DWORD pack_raster_state(const DeviceState &st) {
    uint32_t v = 0;
    packu(v, st.rs[D3DRS_FILLMODE], 3);
    packu(v, st.rs[D3DRS_CULLMODE], 2);
    packu(v, st.rs[D3DRS_SHADEMODE] == D3DSHADE_FLAT ? 1 : 0, 1);
    packu(v, st.rs[D3DRS_SCISSORTESTENABLE] ? 1 : 0, 1);
    packu(v, st.rs[D3DRS_LASTPIXEL] ? 1 : 0, 1);
    packu(v, st.rs[D3DRS_MULTISAMPLEANTIALIAS] ? 1 : 0, 1);
    packu(v, 0, 1);
    return v;
}

DWORD pack_ds_state(const DeviceState &st) {
    // 21 bits (mascaras de stencil entram como campos proprios do PipelineKey
    // — sao DWORDs arbitrarios e nao cabem aqui)
    uint32_t v = 0;
    packu(v, st.rs[D3DRS_ZENABLE] == D3DZB_TRUE ? 1 : 0, 1);
    packu(v, st.rs[D3DRS_ZWRITEENABLE] ? 1 : 0, 1);
    packu(v, st.rs[D3DRS_ZFUNC], 3);
    packu(v, st.rs[D3DRS_STENCILENABLE] ? 1 : 0, 1);
    packu(v, st.rs[D3DRS_STENCILFAIL], 4);
    packu(v, st.rs[D3DRS_STENCILZFAIL], 4);
    packu(v, st.rs[D3DRS_STENCILPASS], 4);
    packu(v, st.rs[D3DRS_STENCILFUNC], 3);
    return v;
}

DWORD pack_blend_state(const DeviceState &st) {
    uint32_t v = 0;
    packu(v, st.rs[D3DRS_ALPHABLENDENABLE] ? 1 : 0, 1);
    packu(v, st.rs[D3DRS_SRCBLEND], 5);
    packu(v, st.rs[D3DRS_DESTBLEND], 5);
    packu(v, st.rs[D3DRS_BLENDOP], 4);
    packu(v, st.rs[D3DRS_SEPARATEALPHABLENDENABLE] ? 1 : 0, 1);
    packu(v, st.rs[D3DRS_SRCBLENDALPHA], 5);
    packu(v, st.rs[D3DRS_DESTBLENDALPHA], 5);
    packu(v, st.rs[D3DRS_BLENDOPALPHA], 4);
    return v;
}

// ------------------------------------------------------------------- caps
void fill_device_caps(D3DCAPS9 *caps, const VkCore &vk) {
    memset(caps, 0, sizeof *caps);
    caps->DeviceType = D3DDEVTYPE_HAL;
    caps->AdapterOrdinal = 0;
    caps->Caps = D3DCAPS_READ_SCANLINE;
    caps->Caps2 = D3DCAPS2_FULLSCREENGAMMA | D3DCAPS2_CANRENDERWINDOWED | D3DCAPS2_DYNAMICTEXTURES;
    caps->Caps3 = D3DCAPS3_ALPHA_FULLSCREEN_FLIP_OR_DISCARD;
    caps->PresentationIntervals = D3DPRESENT_INTERVAL_IMMEDIATE | D3DPRESENT_INTERVAL_ONE;
    caps->CursorCaps = 0;
    caps->DevCaps = D3DDEVCAPS_EXECUTESYSTEMMEMORY | D3DDEVCAPS_EXECUTEVIDEOMEMORY |
                    D3DDEVCAPS_TLVERTEXSYSTEMMEMORY | D3DDEVCAPS_TLVERTEXVIDEOMEMORY |
                    D3DDEVCAPS_TEXTUREVIDEOMEMORY | D3DDEVCAPS_DRAWPRIMTLVERTEX |
                    D3DDEVCAPS_CANRENDERAFTERFLIP | D3DDEVCAPS_TEXTURENONLOCALVIDMEM |
                    D3DDEVCAPS_HWTRANSFORMANDLIGHT | D3DDEVCAPS_HWRASTERIZATION |
                    D3DDEVCAPS_PUREDEVICE;
    caps->PrimitiveMiscCaps = D3DPMISCCAPS_CULLNONE | D3DPMISCCAPS_CULLCW | D3DPMISCCAPS_CULLCCW |
                              D3DPMISCCAPS_COLORWRITEENABLE | D3DPMISCCAPS_CLIPTLVERTS |
                              D3DPMISCCAPS_BLENDOP | D3DPMISCCAPS_NULLREFERENCE;
    caps->RasterCaps = D3DPRASTERCAPS_DITHER | D3DPRASTERCAPS_ZTEST | D3DPRASTERCAPS_FOGVERTEX |
                       D3DPRASTERCAPS_FOGTABLE | D3DPRASTERCAPS_ANISOTROPY |
                       D3DPRASTERCAPS_WBUFFER | D3DPRASTERCAPS_DEPTHBIAS | D3DPRASTERCAPS_SLOPESCALEDEPTHBIAS |
                       D3DPRASTERCAPS_MULTISAMPLE_TOGGLE;
    caps->ZCmpCaps = D3DPCMPCAPS_NEVER | D3DPCMPCAPS_LESS | D3DPCMPCAPS_EQUAL | D3DPCMPCAPS_LESSEQUAL |
                     D3DPCMPCAPS_GREATER | D3DPCMPCAPS_NOTEQUAL | D3DPCMPCAPS_GREATEREQUAL | D3DPCMPCAPS_ALWAYS;
    caps->SrcBlendCaps = D3DPBLENDCAPS_ZERO | D3DPBLENDCAPS_ONE | D3DPBLENDCAPS_SRCCOLOR |
                         D3DPBLENDCAPS_INVSRCCOLOR | D3DPBLENDCAPS_SRCALPHA | D3DPBLENDCAPS_INVSRCALPHA |
                         D3DPBLENDCAPS_DESTALPHA | D3DPBLENDCAPS_INVDESTALPHA | D3DPBLENDCAPS_DESTCOLOR |
                         D3DPBLENDCAPS_INVDESTCOLOR | D3DPBLENDCAPS_SRCALPHASAT | D3DPBLENDCAPS_BOTHSRCALPHA |
                         D3DPBLENDCAPS_BOTHINVSRCALPHA;
    caps->DestBlendCaps = caps->SrcBlendCaps;
    caps->AlphaCmpCaps = caps->ZCmpCaps;
    caps->ShadeCaps = D3DPSHADECAPS_COLORGOURAUDRGB | D3DPSHADECAPS_SPECULARGOURAUDRGB |
                      D3DPSHADECAPS_ALPHAGOURAUDBLEND | D3DPSHADECAPS_FOGGOURAUD;
    caps->TextureCaps = D3DPTEXTURECAPS_ALPHA | D3DPTEXTURECAPS_ALPHAPALETTE | D3DPTEXTURECAPS_CUBEMAP |
                        D3DPTEXTURECAPS_VOLUMEMAP | D3DPTEXTURECAPS_MIPMAP | D3DPTEXTURECAPS_PROJECTED |
                        D3DPTEXTURECAPS_POW2 | D3DPTEXTURECAPS_NONPOW2CONDITIONAL;
    caps->TextureFilterCaps = D3DPTFILTERCAPS_MINFPOINT | D3DPTFILTERCAPS_MINFLINEAR |
                              D3DPTFILTERCAPS_MINFANISOTROPIC | D3DPTFILTERCAPS_MAGFPOINT |
                              D3DPTFILTERCAPS_MAGFLINEAR | D3DPTFILTERCAPS_MAGFANISOTROPIC |
                              D3DPTFILTERCAPS_MIPFPOINT | D3DPTFILTERCAPS_MIPFLINEAR;
    caps->CubeTextureFilterCaps = caps->TextureFilterCaps;
    caps->VolumeTextureFilterCaps = caps->TextureFilterCaps;
    caps->TextureAddressCaps = D3DPTADDRESSCAPS_WRAP | D3DPTADDRESSCAPS_MIRROR | D3DPTADDRESSCAPS_CLAMP |
                               D3DPTADDRESSCAPS_BORDER | D3DPTADDRESSCAPS_INDEPENDENTUV;
    caps->VolumeTextureAddressCaps = caps->TextureAddressCaps;
    caps->LineCaps = D3DLINECAPS_TEXTURE | D3DLINECAPS_ZTEST | D3DLINECAPS_BLEND |
                     D3DLINECAPS_ALPHACMP | D3DLINECAPS_FOG | D3DLINECAPS_ANTIALIAS;
    caps->MaxTextureWidth = 8192;
    caps->MaxTextureHeight = 8192;
    caps->MaxVolumeExtent = 2048;
    caps->MaxTextureRepeat = 32768;
    caps->MaxTextureAspectRatio = 8192;
    caps->MaxAnisotropy = 16;
    caps->MaxVertexW = 1e10f;
    caps->GuardBandLeft = -32768.0f;
    caps->GuardBandTop = -32768.0f;
    caps->GuardBandRight = 32768.0f;
    caps->GuardBandBottom = 32768.0f;
    caps->ExtentsAdjust = 0.0f;
    caps->StencilCaps = D3DSTENCILCAPS_KEEP | D3DSTENCILCAPS_ZERO | D3DSTENCILCAPS_REPLACE |
                        D3DSTENCILCAPS_INCRSAT | D3DSTENCILCAPS_DECRSAT | D3DSTENCILCAPS_INVERT |
                        D3DSTENCILCAPS_INCR | D3DSTENCILCAPS_DECR | D3DSTENCILCAPS_TWOSIDED;
    caps->FVFCaps = D3DFVFCAPS_PSIZE | (8u << 8);
    caps->TextureOpCaps = D3DTEXOPCAPS_DISABLE | D3DTEXOPCAPS_SELECTARG1 | D3DTEXOPCAPS_SELECTARG2 |
                          D3DTEXOPCAPS_MODULATE | D3DTEXOPCAPS_MODULATE2X | D3DTEXOPCAPS_MODULATE4X |
                          D3DTEXOPCAPS_ADD | D3DTEXOPCAPS_ADDSIGNED | D3DTEXOPCAPS_ADDSIGNED2X |
                          D3DTEXOPCAPS_SUBTRACT | D3DTEXOPCAPS_ADDSMOOTH | D3DTEXOPCAPS_BLENDDIFFUSEALPHA |
                          D3DTEXOPCAPS_BLENDTEXTUREALPHA | D3DTEXOPCAPS_BLENDFACTORALPHA |
                          D3DTEXOPCAPS_BLENDCURRENTALPHA | D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR |
                          D3DTEXOPCAPS_MODULATECOLOR_ADDALPHA | D3DTEXOPCAPS_MODULATEINVALPHA_ADDCOLOR |
                          D3DTEXOPCAPS_MODULATEINVCOLOR_ADDALPHA | D3DTEXOPCAPS_DOTPRODUCT3 |
                          D3DTEXOPCAPS_MULTIPLYADD | D3DTEXOPCAPS_LERP;
    caps->MaxTextureBlendStages = 8;
    caps->MaxSimultaneousTextures = 8;
    caps->VertexProcessingCaps = D3DVTXPCAPS_TEXGEN | D3DVTXPCAPS_MATERIALSOURCE7 |
                                 D3DVTXPCAPS_DIRECTIONALLIGHTS | D3DVTXPCAPS_POSITIONALLIGHTS |
                                 D3DVTXPCAPS_LOCALVIEWER | D3DVTXPCAPS_TWEENING;
    caps->MaxActiveLights = 8;
    caps->MaxUserClipPlanes = 6;
    caps->MaxVertexBlendMatrices = 4;
    caps->MaxVertexBlendMatrixIndex = 255;
    caps->MaxPointSize = 256.0f;
    caps->MaxPrimitiveCount = 1048575;
    caps->MaxVertexIndex = 0xFFFFF;
    caps->MaxStreams = 16;
    caps->VertexShaderVersion = D3DVS_VERSION(3, 0);
    caps->MaxVertexShaderConst = 256;
    caps->PixelShaderVersion = D3DPS_VERSION(3, 0);
    caps->PixelShader1xMaxValue = 8.0f;
    caps->DevCaps2 = D3DDEVCAPS2_STREAMOFFSET | D3DDEVCAPS2_ADAPTIVETESSRTPATCH;
    caps->MaxNpatchTessellationLevel = 1.0f;
    caps->MasterAdapterOrdinal = 0;
    caps->AdapterOrdinalInGroup = 0;
    caps->NumberOfAdaptersInGroup = 1;
    // MSAA real do device
    uint32_t sc = vk.max_sample_counts_color();
    DWORD ms = 0;
    for (uint32_t s = 2; s <= 16; s <<= 1)
        if (sc & s) ms |= 1u << (s - 1);
    caps->NumberOfAdaptersInGroup = 1;
}

// ---------------------------------------------------------------- IDirect3D9
bool NrD3D9::ensure_core() {
    if (m_core) return true;
    bool validation = getenv("NVR_VK_VALIDATION") && *getenv("NVR_VK_VALIDATION") == '1';
    if (!m_own_core.init(validation)) return false;
    m_core = &m_own_core;
    return true;
}

HRESULT NrD3D9::RegisterSoftwareDevice(void *) { return D3DERR_INVALIDCALL; }

UINT NrD3D9::GetAdapterCount() { return 1; }

HRESULT NrD3D9::GetAdapterIdentifier(UINT adapter, DWORD, D3DADAPTER_IDENTIFIER9 *out) {
    if (adapter != 0 || !out) return D3DERR_INVALIDCALL;
    if (!ensure_core()) return D3DERR_NOTAVAILABLE;
    memset(out, 0, sizeof *out);
    snprintf(out->Description, sizeof out->Description, "native-render Vulkan (%s)",
             m_core->props().deviceName);
    snprintf(out->DeviceName, sizeof out->DeviceName, "\\\\.\\DISPLAY1");
    out->Driver[0] = 0;
    out->VendorId = m_core->props().vendorID;
    out->DeviceId = m_core->props().deviceID;
    out->SubSysId = 0;
    out->Revision = 0;
    // GUID estavel derivado do vendor/device
    uint8_t guid[16] = {};
    memcpy(guid, "nvr-gpu-guid-01", 15);
    guid[0] = (uint8_t)m_core->props().vendorID;
    guid[1] = (uint8_t)(m_core->props().vendorID >> 8);
    guid[2] = (uint8_t)m_core->props().deviceID;
    guid[3] = (uint8_t)(m_core->props().deviceID >> 8);
    memcpy(&out->DeviceIdentifier, guid, 16);
    out->WHQLLevel = 0;
    return D3D_OK;
}

UINT NrD3D9::GetAdapterModeCount(UINT, D3DFORMAT) { return 9; }

HRESULT NrD3D9::EnumAdapterModes(UINT adapter, D3DFORMAT fmt, UINT mode, D3DDISPLAYMODE *out) {
    if (adapter != 0 || mode >= 9 || !out) return D3DERR_INVALIDCALL;
    SDL_Window *w = nullptr;
    // a bridge reescreve EnumAdapterModes; este caminho existe por completude
    int ww = 1280, wh = 720;
    if (SDL_GetCurrentVideoDriver() && strstr(SDL_GetCurrentVideoDriver(), "android")) {
        SDL_DisplayMode dm;
        if (SDL_GetDesktopDisplayMode(0, &dm) == 0) { ww = dm.w; wh = dm.h; }
    }
    out->Width = (UINT)(ww * (2 + mode) / 10) & ~1u;
    out->Height = (UINT)(wh * (2 + mode) / 10) & ~1u;
    out->RefreshRate = 60;
    out->Format = fmt == D3DFMT_UNKNOWN ? D3DFMT_X8R8G8B8 : fmt;
    return D3D_OK;
}

HRESULT NrD3D9::GetAdapterDisplayMode(UINT adapter, D3DDISPLAYMODE *out) {
    if (adapter != 0 || !out) return D3DERR_INVALIDCALL;
    memset(out, 0, sizeof *out);
    out->Width = 1280;
    out->Height = 720;
    out->RefreshRate = 60;
    out->Format = D3DFMT_X8R8G8B8;
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) == 0) {
        out->Width = (UINT)dm.w;
        out->Height = (UINT)dm.h;
    }
    return D3D_OK;
}

HRESULT NrD3D9::CheckDeviceType(UINT, D3DDEVTYPE, D3DFORMAT disp, D3DFORMAT back, BOOL) {
    // X8R8G8B8/A8R8G8B8/R5G6B5 display/back sempre OK (swapchain format real
    // e escolhido no CreateDevice)
    switch (disp) {
    case D3DFMT_UNKNOWN: case D3DFMT_X8R8G8B8: case D3DFMT_A8R8G8B8: case D3DFMT_R5G6B5: break;
    default: return D3DERR_NOTAVAILABLE;
    }
    switch (back) {
    case D3DFMT_UNKNOWN: case D3DFMT_X8R8G8B8: case D3DFMT_A8R8G8B8: case D3DFMT_R5G6B5:
    case D3DFMT_A1R5G5B5: case D3DFMT_X1R5G5B5: break;
    default: return D3DERR_NOTAVAILABLE;
    }
    return D3D_OK;
}

HRESULT NrD3D9::CheckDeviceFormat(UINT, D3DDEVTYPE, D3DFORMAT, DWORD usage,
                                  D3DRESOURCETYPE rtype, D3DFORMAT fmt) {
    if (!ensure_core()) return D3DERR_NOTAVAILABLE;
    const FormatInfo &fi = format_info(fmt);
    if (fi.vk == VK_FORMAT_UNDEFINED) return D3DERR_NOTAVAILABLE;
    FormatSupport sup = query_format_support(m_core->physical(), fi.vk);
    if (fi.depth) return sup.depth_rt ? D3D_OK : D3DERR_NOTAVAILABLE;
    if (usage & (D3DUSAGE_RENDERTARGET | D3DUSAGE_DEPTHSTENCIL))
        return sup.color_rt ? D3D_OK : D3DERR_NOTAVAILABLE;
    if (rtype == D3DRTYPE_SURFACE) return D3D_OK;   // sysmem surface aceita formato
    return sup.sample ? D3D_OK : D3DERR_NOTAVAILABLE;
}

HRESULT NrD3D9::CheckDeviceMultiSampleType(UINT, D3DDEVTYPE, D3DFORMAT back, BOOL, D3DMULTISAMPLE_TYPE ms,
                                           DWORD *quality) {
    if (!ensure_core()) return D3DERR_NOTAVAILABLE;
    if (quality) *quality = 1;
    if (ms == D3DMULTISAMPLE_NONE) return D3D_OK;
    if (ms == D3DMULTISAMPLE_NONMASKABLE) return D3D_OK;
    uint32_t want = (uint32_t)ms;
    if ((m_core->max_sample_counts_color() & want) == 0) return D3DERR_NOTAVAILABLE;
    return D3D_OK;
}

HRESULT NrD3D9::CheckDepthStencilMatch(UINT, D3DDEVTYPE, D3DFORMAT, D3DFORMAT rt, D3DFORMAT ds) {
    if (!ensure_core()) return D3DERR_NOTAVAILABLE;
    bool stencil;
    VkFormat f = choose_depth_format(m_core->physical(), ds, &stencil);
    if (!f) return D3DERR_NOTAVAILABLE;
    switch (rt) {
    case D3DFMT_X8R8G8B8: case D3DFMT_A8R8G8B8: case D3DFMT_R5G6B5: case D3DFMT_A2R10G10B10:
        return D3D_OK;
    default: return D3DERR_NOTAVAILABLE;
    }
}

HRESULT NrD3D9::CheckDeviceFormatConversion(UINT, D3DDEVTYPE, D3DFORMAT src, D3DFORMAT dst) {
    if ((src == D3DFMT_X8R8G8B8 || src == D3DFMT_A8R8G8B8 || src == D3DFMT_R5G6B5) &&
        (dst == D3DFMT_X8R8G8B8 || dst == D3DFMT_R5G6B5))
        return D3D_OK;
    return D3DERR_NOTAVAILABLE;
}

HRESULT NrD3D9::GetDeviceCaps(UINT adapter, D3DDEVTYPE type, D3DCAPS9 *out) {
    if (!ensure_core()) return D3DERR_NOTAVAILABLE;
    if (adapter != 0 || type != D3DDEVTYPE_HAL || !out) return D3DERR_INVALIDCALL;
    fill_device_caps(out, *m_core);
    return D3D_OK;
}

HMONITOR NrD3D9::GetAdapterMonitor(UINT) { return (HMONITOR)0x00010001; }

HRESULT NrD3D9::CreateDevice(UINT adapter, D3DDEVTYPE type, HWND hwnd, DWORD flags,
                             D3DPRESENT_PARAMETERS *pp, IDirect3DDevice9 **out) {
    if (adapter != 0 || type != D3DDEVTYPE_HAL || !pp || !out) return D3DERR_INVALIDCALL;
    if (!ensure_core()) return D3DERR_NOTAVAILABLE;
    auto *dev = new NrDevice();
    dev->owner = (void *)this;
    dev->refs = 1;
    AddRef();
    if (!dev->init(this, adapter, type, hwnd, flags, pp)) {
        dev->Release();
        return D3DERR_NOTAVAILABLE;
    }
    *out = dev;
    return D3D_OK;
}

HRESULT NrD3D9::QueryInterface(REFIID riid, void **out) {
    if (!out) return E_POINTER;
    *out = nullptr;
    if (riid == __uuidof(IDirect3D9) || riid == __uuidof(IUnknown)) {
        *out = (IDirect3D9 *)this;
        AddRef();
        return S_OK;
    }
    return E_NOINTERFACE;
}

ULONG NrD3D9::Release() {
    ULONG r = --refs;
    if (!r) delete this;
    return r;
}

} // namespace nvr
