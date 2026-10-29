/* Per-game names for the SWAT 4 port.
 *
 * This runtime is shared infrastructure; everything game-specific comes from
 * here and from runtime/swat4_shims.c. See docs/RECON.md for the measured
 * values (hashes, build, module set). Only the supported build is listed:
 * GOG Gold, Swat4.exe SHA-1 94d1a40f8f60b6664f539cab4c38a444b7c4a1e7.
 */
#pragma once

#define GAME_SWAT4 1

/* The thin launcher stub; the engine lives in the DLLs (see runtime/modules.c). */
#define GAME_EXE       "Swat4.exe"
/* Swat4.exe has no relocation table; it must run at its preferred base. */
#define GAME_EXE_BASE  0x10900000u
#define GAME_TITLE     "SWAT 4"
#define GAME_PKG_DIR   "io.github.deivid22srk.swat4"
/* The exe is at <root>/Content/System; the engine resolves packages relative
 * to that folder, so the runtime chdir()s there and keeps saves under <root>/port. */
#define GAME_SYS_DIR   "Content/System"
/* A plausible Windows install path. Kernel32 shims synthesise it for
 * GetModuleFileName/GetCommandLine; rt_host_path maps it back to g_game_dir. */
#define GAME_WIN_DIR    "C:\\GOG Games\\SWAT 4\\Content\\System"
#define GAME_WIN_PREFIX "\\gog games\\swat 4\\"
