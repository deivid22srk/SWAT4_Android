/* SWAT 4 host shims for things the generic runtime cannot cover.
 *
 * Most of what SWAT 4 imports from the OS is handled by the shared runtime:
 *   kernel32.c  files, memory, threads, sync, time, TLS, module handles
 *   user32.c    window class, messages, display modes
 *   msvcrt.c    VC7.1 CRT (aliased to msvcr71.dll here)
 *   misc.c      advapi32/ole32/shell32/winmm/version
 *   d3d9_bridge Direct3D 9 COM interfaces (dxvk-native)
 *   dinput.c / input.c / dsound.c / xaudio2.c  input and audio devices
 *
 * Anything still missing deliberately aborts with the import name when first
 * called ("unimplemented import dll!name"), so a gap is never silent.
 */
#include "rt.h"
#include <string.h>
#include <stdlib.h>

/* ---------------------------------------------- Massive Inc. in-game ads */
/* The service is dead and online features are out of scope. Every entry point
 * reports failure so the game skips the ad slots instead of waiting on a
 * network that does not exist. */
SHIM(ad_Initialize) { LOG("massive: Initialize -> NULL (service is dead)"); RETC(0); }
SHIM(ad_Shutdown) { RETC(0); }
SHIM(ad_EnterZone) { RETC(0); }
SHIM(ad_Update) { RETC(0); }
SHIM(ad_sub_ctor) { LOG("massive: subscriber ctor (no-op)"); RETC(ARG(0)); }
SHIM(ad_sub_dtor) { RETC(0); }
SHIM(ad_sub_SetImpression) { RETC(0); }

/* -------------------------------------------------------------- gdi32 */
/* SWAT 4 imports ~124 GDI functions, mostly for the UnrealEd UI and for the
 * Bink windowed blit path. The game itself renders through D3DDrv; these exist
 * so the imports resolve and a stray call does not abort. Anything that would
 * need a real HDC returns a harmless fake handle. */
SHIM(gdi_GetStockObject) { static uint32_t next = 0x7000; RET(next++, 1); }
SHIM(gdi_CreateCompatibleDC) { static uint32_t next = 0x7100; RET(next++, 1); }
SHIM(gdi_DeleteDC) { RET(1, 1); }
SHIM(gdi_DeleteObject) { RET(1, 1); }
SHIM(gdi_SelectObject) { RET(ARG(1), 2); }
SHIM(gdi_CreateCompatibleBitmap) { static uint32_t next = 0x7200; RET(next++, 3); }
SHIM(gdi_CreateDIBSection) {
    /* (HDC, const BITMAPINFO *bmi, UINT usage, void **bits, HANDLE section, DWORD offset) */
    uint32_t bmi = ARG(1), bits_out = ARG(3);
    uint32_t w = bmi ? M32(bmi + 4) : 1, h = bmi ? (uint32_t)abs((int)M32(bmi + 8)) : 1;
    uint32_t bpp = bmi ? M16(bmi + 14) : 32;
    uint32_t size = w * h * (bpp ? bpp : 32) / 8;
    uint32_t buf = gmalloc(size ? size : 16);
    memset(MEM + buf, 0, size ? size : 16);
    if (bits_out) M32(bits_out) = buf;
    static uint32_t next = 0x7300;
    TRACE("gdi: CreateDIBSection %ux%u %ubpp -> %08x", w, h, bpp, buf);
    RET(next++, 6);
}
SHIM(gdi_StretchBlt) { RET(1, 11); }
SHIM(gdi_SetStretchBltMode) { RET(1, 2); }
SHIM(gdi_GetDIBits) { RET(1, 7); }
SHIM(gdi_GetDeviceCaps) { RET(32, 2); }
SHIM(gdi_GetPixel) { RET(0, 3); }
SHIM(gdi_SetPixel) { RET(0, 4); }

#define S2(dll, name, fn) {dll, name, fn},
static const ShimDef defs[] = {
    /* Massive ad client (base repack uses the "xmassiveadclientdyn" name). */
    {"xmassiveadclientdyn.dll", "?Initialize@XMassiveAdClient@MassiveAdClient3@@SAPAV12@PBU__MAD_MASSIVE_INIT_STRUCT@2@@Z", shim_ad_Initialize},
    {"xmassiveadclientdyn.dll", "?Shutdown@XMassiveAdClient@MassiveAdClient3@@SAKHI@Z", shim_ad_Shutdown},
    {"xmassiveadclientdyn.dll", "?EnterZone@XMassiveAdClient@MassiveAdClient3@@SAKPBD@Z", shim_ad_EnterZone},
    {"xmassiveadclientdyn.dll", "?Update@XMassiveAdClient@MassiveAdClient3@@SAKH@Z", shim_ad_Update},
    {"xmassiveadclientdyn.dll", "??0XMassiveAdClientSubscriber@MassiveAdClient3@@QAE@PBD@Z", shim_ad_sub_ctor},
    {"xmassiveadclientdyn.dll", "??1XMassiveAdClientSubscriber@MassiveAdClient3@@QAE@XZ", shim_ad_sub_dtor},
    {"xmassiveadclientdyn.dll", "?SetImpression@XMassiveAdClientSubscriber@MassiveAdClient3@@QAEXPAU_MAD_Impression@2@@Z", shim_ad_sub_SetImpression},

    {"gdi32.dll", "GetStockObject", shim_gdi_GetStockObject},
    {"gdi32.dll", "CreateCompatibleDC", shim_gdi_CreateCompatibleDC},
    {"gdi32.dll", "DeleteDC", shim_gdi_DeleteDC},
    {"gdi32.dll", "DeleteObject", shim_gdi_DeleteObject},
    {"gdi32.dll", "SelectObject", shim_gdi_SelectObject},
    {"gdi32.dll", "CreateCompatibleBitmap", shim_gdi_CreateCompatibleBitmap},
    {"gdi32.dll", "CreateDIBSection", shim_gdi_CreateDIBSection},
    {"gdi32.dll", "StretchBlt", shim_gdi_StretchBlt},
    {"gdi32.dll", "SetStretchBltMode", shim_gdi_SetStretchBltMode},
    {"gdi32.dll", "GetDIBits", shim_gdi_GetDIBits},
    {"gdi32.dll", "GetDeviceCaps", shim_gdi_GetDeviceCaps},
    {"gdi32.dll", "GetPixel", shim_gdi_GetPixel},
    {"gdi32.dll", "SetPixel", shim_gdi_SetPixel},
    {NULL, NULL, NULL}
};
#undef S2

void swat4_shims_init(void) { rt_register_shims(defs); }