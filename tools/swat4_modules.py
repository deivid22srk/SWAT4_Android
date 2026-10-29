#!/usr/bin/env python3
"""SWAT 4 (GOG Gold, base game) module manifest.

Single source of truth for: discovery/lifting (tools/swat4_regen.py),
runtime/modules.c and ci/gen_stub. Addresses are rebased so no two images
overlap and none hits the guest stack/TEB/heap (see docs/RECON.md §3).

The exe has no relocation table, so it must stay at its preferred base
0x10900000. Every DLL has relocations and is rebased below 0x10000000.
"""

# id, filename, base, kind
# kind: "exe" is loaded last as GAME_EXE; "dll" is loaded from the game folder.
MODULES = [
    ("ogg",          "ogg.dll",                       0x03A00000, "dll"),
    ("vorbis",       "vorbis.dll",                    0x03B00000, "dll"),
    ("vorbisfile",   "vorbisfile.dll",                0x03C00000, "dll"),
    ("bink",         "binkw32.dll",                   0x02900000, "dll"),
    ("m4d",          "m4d.dll",                       0x02A00000, "dll"),
    ("rtcshader",    "RTCShader.dll",                 0x02700000, "dll"),
    ("ifc23",        "IFC23.dll",                     0x02B00000, "dll"),
    ("irr",          "IrrSupport.dll",                0x02C00000, "dll"),
    ("renderutil",   "RenderUtilities.dll",           0x02800000, "dll"),
    ("core",         "Core.dll",                      0x01000000, "dll"),
    ("engine",       "Engine.dll",                    0x01400000, "dll"),
    ("scripting",    "Scripting.dll",                 0x03700000, "dll"),
    ("window",       "Window.dll",                    0x02000000, "dll"),
    ("windrv",       "WinDrv.dll",                    0x02100000, "dll"),
    ("d3ddrv",       "D3DDrv.dll",                    0x02200000, "dll"),
    ("alaudio",      "ALAudio.dll",                   0x02300000, "dll"),
    ("openal",       "DefOpenAL32.dll",               0x02400000, "dll"),
    ("gui",          "GUI.dll",                       0x02500000, "dll"),
    ("ipdrv",        "IpDrv.dll",                     0x02600000, "dll"),
    ("aicommon",     "AICommon.dll",                  0x02D00000, "dll"),
    ("aiaware",      "SwatAIAwareness.dll",           0x02E00000, "dll"),
    ("tyrion",       "Tyrion.dll",                    0x03000000, "dll"),
    ("rwo",          "RWOSupport.dll",                0x03100000, "dll"),
    ("scriptcomp",   "ScriptCompiler.dll",            0x03800000, "dll"),
    ("unrealed",     "UnrealEdPerforce.dll",          0x03E00000, "dll"),
    ("igeffects",    "IGEffectsSystem.dll",           0x03400000, "dll"),
    ("igsound",      "IGSoundEffectsSubsystem.dll",   0x03500000, "dll"),
    ("igvisual",     "IGVisualEffectsSubsystem.dll",  0x03600000, "dll"),
    ("editor",       "Editor.dll",                    0x03D00000, "dll"),
    ("saicommon",    "SwatAICommon.dll",              0x02F00000, "dll"),
    ("swatgame",     "SwatGame.dll",                  0x03200000, "dll"),
    ("swatgui",      "SwatGui.dll",                   0x03300000, "dll"),
    # dinterface.dll exists in Content/System but no module imports it (checked
    # with pefile); it is not part of the boot path and is not recompiled.
]

EXE = ("exe", "Swat4.exe", 0x10900000, "exe")

# Load order: dependencies first (see docs/RECON.md §3). The exe is loaded
# last by runtime/main.c.
DLL_ORDER = [m[0] for m in MODULES]