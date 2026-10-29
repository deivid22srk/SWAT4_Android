# Importações — SWAT 4 (GOG Gold, base + expansão)

Gerado por `tools/imports_report.py` a partir de `pefile` sobre os binários fornecidos pelo usuário (nenhum binário está neste repositório). Lista a união de APIs de SO importadas por cada DLL, por nome. APIs de DLLs do próprio jogo (Core/Engine/...) não entram aqui: elas são resolvidas pelos exports recompilados (ver `runtime/shims.c`).

> Contagem: **1.201 APIs distintas** (base + expansão). `D3D9/DSOUND/DINPUT8/DDRAW` aparecem com 1 função cada porque o resto é chamado por vtables COM.

## SWAT 4 (base)

### ADVAPI32.DLL  (14 functions)

`CryptAcquireContextA`, `CryptGenRandom`, `CryptReleaseContext`, `GetUserNameA`, `GetUserNameW`, `RegCloseKey`, `RegCreateKeyExA`, `RegDeleteKeyA`, `RegDeleteValueA`, `RegFlushKey`, `RegOpenKeyA`, `RegOpenKeyExA`, `RegQueryValueExA`, `RegSetValueExA`

### BINKW32.DLL  (13 functions)

`_BinkClose@4`, `_BinkCopyToBuffer@28`, `_BinkDX9SurfaceType@4`, `_BinkDoFrame@4`, `_BinkGetSummary@8`, `_BinkGoto@12`, `_BinkNextFrame@4`, `_BinkOpen@8`, `_BinkOpenDirectSound@4`, `_BinkPause@8`, `_BinkSetSoundOnOff@8`, `_BinkSetSoundSystem@8`, `_BinkWait@4`

### COMCTL32.DLL  (28 functions)

`CreateToolbarEx`, `ImageList_Add`, `ImageList_BeginDrag`, `ImageList_Create`, `ImageList_Destroy`, `ImageList_DragEnter`, `ImageList_DragLeave`, `ImageList_DragMove`, `ImageList_DragShowNolock`, `ImageList_Draw`, `ImageList_DrawEx`, `ImageList_EndDrag`, `ImageList_GetBkColor`, `ImageList_GetDragImage`, `ImageList_GetIconSize`, `ImageList_GetImageCount`, `ImageList_LoadImageW`, `ImageList_Read`, `ImageList_Remove`, `ImageList_Replace`, `ImageList_ReplaceIcon`, `ImageList_SetBkColor`, `ImageList_SetDragCursorImage`, `ImageList_SetIconSize`, `ImageList_Write`, `InitCommonControls`, `_TrackMouseEvent`, `ord17`

### COMDLG32.DLL  (5 functions)

`ChooseColorA`, `FindTextA`, `GetOpenFileNameA`, `GetSaveFileNameA`, `PrintDlgA`

### D3D9.DLL  (1 functions)

`Direct3DCreate9`

### DDRAW.DLL  (1 functions)

`DirectDrawCreate`

### DINPUT8.DLL  (1 functions)

`DirectInput8Create`

### GDI32.DLL  (124 functions)

`AbortDoc`, `ArcTo`, `BitBlt`, `CloseMetaFile`, `CombineRgn`, `CopyMetaFileW`, `CreateBitmap`, `CreateCompatibleBitmap`, `CreateCompatibleDC`, `CreateDCA`, `CreateDCW`, `CreateDIBPatternBrushPt`, `CreateDIBSection`, `CreateDIBitmap`, `CreateEllipticRgn`, `CreateFontA`, `CreateFontIndirectW`, `CreateFontW`, `CreateHatchBrush`, `CreateMetaFileW`, `CreatePatternBrush`, `CreatePen`, `CreateRectRgn`, `CreateRectRgnIndirect`, `CreateSolidBrush`, `DPtoLP`, `DeleteDC`, `DeleteMetaFile`, `DeleteObject`, `Ellipse`, `EndDoc`, `EndPage`, `EnumFontFamiliesExW`, `EnumMetaFile`, `Escape`, `ExcludeClipRect`, `ExtCreatePen`, `ExtSelectClipRgn`, `ExtTextOutA`, `ExtTextOutW`, `GetBkColor`, `GetBkMode`, `GetCharWidthW`, `GetClipBox`, `GetClipRgn`, `GetCurrentPositionEx`, `GetDIBits`, `GetDeviceCaps`, `GetMapMode`, `GetNearestColor`, `GetObjectA`, `GetObjectType`, `GetObjectW`, `GetPixel`, `GetPolyFillMode`, `GetROP2`, `GetRgnBox`, `GetStockObject`, `GetStretchBltMode`, `GetTextAlign`, `GetTextColor`, `GetTextExtentExPointA`, `GetTextExtentPoint32A`, `GetTextExtentPoint32W`, `GetTextFaceW`, `GetTextMetricsW`, `GetViewportExtEx`, `GetViewportOrgEx`, `GetWindowExtEx`, `GetWindowOrgEx`, `IntersectClipRect`, `LPtoDP`, `LineTo`, `MoveToEx`, `OffsetClipRgn`, `OffsetRgn`, `OffsetViewportOrgEx`, `OffsetWindowOrgEx`, `PatBlt`, `PlayMetaFile`, `PlayMetaFileRecord`, `PolyBezierTo`, `PolyDraw`, `Polygon`, `PolylineTo`, `PtVisible`, `RectVisible`, `Rectangle`, `RestoreDC`, `SaveDC`, `ScaleViewportExtEx`, `ScaleWindowExtEx`, `SelectClipPath`, `SelectClipRgn`, `SelectObject`, `SelectPalette`, `SetAbortProc`, `SetArcDirection`, `SetBkColor`, `SetBkMode`, `SetBrushOrgEx`, `SetColorAdjustment`, `SetDeviceGammaRamp`, `SetMapMode`, `SetMapperFlags`, `SetPixel`, `SetPolyFillMode`, `SetROP2`, `SetRectRgn`, `SetStretchBltMode`, `SetTextAlign`, `SetTextCharacterExtra`, `SetTextColor`, `SetTextJustification`, `SetViewportExtEx`, `SetViewportOrgEx`, `SetWindowExtEx`, `SetWindowOrgEx`, `StartDocW`, `StartPage`, `StretchBlt`, `StretchDIBits`, `TextOutW`, `UnrealizeObject`

### IMM32.DLL  (6 functions)

`ImmAssociateContext`, `ImmCreateContext`, `ImmDestroyContext`, `ImmGetCompositionStringW`, `ImmGetContext`, `ImmReleaseContext`

### KERNEL32.DLL  (280 functions)

`Beep`, `BeginUpdateResourceA`, `CloseHandle`, `CompareFileTime`, `CompareStringA`, `CompareStringW`, `ConvertDefaultLocale`, `CopyFileA`, `CopyFileW`, `CreateDirectoryA`, `CreateDirectoryW`, `CreateEventA`, `CreateEventW`, `CreateFileA`, `CreateFileMappingA`, `CreateFileW`, `CreateMutexA`, `CreateMutexW`, `CreatePipe`, `CreateProcessA`, `CreateProcessW`, `CreateSemaphoreW`, `CreateThread`, `DebugBreak`, `DeleteCriticalSection`, `DeleteFileA`, `DeleteFileW`, `DisableThreadLibraryCalls`, `DuplicateHandle`, `EndUpdateResourceA`, `EnterCriticalSection`, `EnumCalendarInfoA`, `EnumResourceLanguagesW`, `EnumSystemLocalesA`, `ExitProcess`, `ExitThread`, `FileTimeToDosDateTime`, `FileTimeToLocalFileTime`, `FileTimeToSystemTime`, `FindClose`, `FindFirstFileA`, `FindFirstFileW`, `FindNextFileA`, `FindNextFileW`, `FindResourceA`, `FindResourceExW`, `FindResourceW`, `FlushFileBuffers`, `FormatMessageA`, `FormatMessageW`, `FreeEnvironmentStringsA`, `FreeEnvironmentStringsW`, `FreeLibrary`, `FreeResource`, `GetACP`, `GetCPInfo`, `GetCommandLineA`, `GetCommandLineW`, `GetComputerNameA`, `GetComputerNameW`, `GetConsoleCP`, `GetConsoleMode`, `GetConsoleOutputCP`, `GetCurrentDirectoryA`, `GetCurrentDirectoryW`, `GetCurrentProcess`, `GetCurrentProcessId`, `GetCurrentThread`, `GetCurrentThreadId`, `GetDateFormatA`, `GetDiskFreeSpaceA`, `GetDiskFreeSpaceExW`, `GetDiskFreeSpaceW`, `GetDriveTypeA`, `GetDriveTypeW`, `GetEnvironmentStrings`, `GetEnvironmentStringsW`, `GetEnvironmentVariableA`, `GetEnvironmentVariableW`, `GetExitCodeProcess`, `GetExitCodeThread`, `GetFileAttributesA`, `GetFileAttributesExA`, `GetFileAttributesExW`, `GetFileAttributesW`, `GetFileInformationByHandle`, `GetFileSize`, `GetFileTime`, `GetFileType`, `GetFullPathNameA`, `GetFullPathNameW`, `GetLastError`, `GetLocalTime`, `GetLocaleInfoA`, `GetLocaleInfoW`, `GetLogicalDrives`, `GetModuleFileNameA`, `GetModuleFileNameW`, `GetModuleHandleA`, `GetModuleHandleW`, `GetNumberOfConsoleInputEvents`, `GetOEMCP`, `GetPrivateProfileIntW`, `GetPrivateProfileSectionA`, `GetPrivateProfileStringA`, `GetPrivateProfileStringW`, `GetProcAddress`, `GetProcessHeap`, `GetProcessWorkingSetSize`, `GetProfileIntW`, `GetProfileStringA`, `GetShortPathNameW`, `GetStartupInfoA`, `GetStdHandle`, `GetStringTypeA`, `GetStringTypeExW`, `GetStringTypeW`, `GetSystemDefaultLangID`, `GetSystemDirectoryA`, `GetSystemDirectoryW`, `GetSystemInfo`, `GetSystemTime`, `GetSystemTimeAsFileTime`, `GetTempFileNameA`, `GetTempFileNameW`, `GetTempPathA`, `GetTempPathW`, `GetThreadContext`, `GetThreadLocale`, `GetTickCount`, `GetTimeFormatA`, `GetTimeZoneInformation`, `GetUserDefaultLCID`, `GetVersion`, `GetVersionExA`, `GetVersionExW`, `GetVolumeInformationA`, `GetVolumeInformationW`, `GetWindowsDirectoryA`, `GetWindowsDirectoryW`, `GlobalAddAtomA`, `GlobalAddAtomW`, `GlobalAlloc`, `GlobalDeleteAtom`, `GlobalFindAtomW`, `GlobalFlags`, `GlobalFree`, `GlobalGetAtomNameW`, `GlobalHandle`, `GlobalLock`, `GlobalMemoryStatus`, `GlobalReAlloc`, `GlobalSize`, `GlobalUnlock`, `HeapAlloc`, `HeapCompact`, `HeapCreate`, `HeapDestroy`, `HeapFree`, `HeapReAlloc`, `HeapSize`, `HeapValidate`, `HeapWalk`, `InitializeCriticalSection`, `InitializeCriticalSectionAndSpinCount`, `InterlockedCompareExchange`, `InterlockedDecrement`, `InterlockedExchange`, `InterlockedIncrement`, `IsBadCodePtr`, `IsBadReadPtr`, `IsBadWritePtr`, `IsDBCSLeadByte`, `IsDBCSLeadByteEx`, `IsDebuggerPresent`, `IsProcessorFeaturePresent`, `IsValidCodePage`, `IsValidLocale`, `LCMapStringA`, `LCMapStringW`, `LeaveCriticalSection`, `LoadLibraryA`, `LoadLibraryExA`, `LoadLibraryExW`, `LoadLibraryW`, `LoadResource`, `LocalAlloc`, `LocalFileTimeToFileTime`, `LocalFree`, `LocalLock`, `LocalReAlloc`, `LocalUnlock`, `LockFile`, `LockFileEx`, `LockResource`, `MapViewOfFile`, `MoveFileA`, `MoveFileW`, `MulDiv`, `MultiByteToWideChar`, `OpenEventW`, `OutputDebugStringA`, `OutputDebugStringW`, `PeekConsoleInputA`, `PeekNamedPipe`, `QueryPerformanceCounter`, `QueryPerformanceFrequency`, `RaiseException`, `ReadConsoleA`, `ReadConsoleInputA`, `ReadConsoleInputW`, `ReadConsoleW`, `ReadFile`, `ReleaseMutex`, `ReleaseSemaphore`, `RemoveDirectoryA`, `RemoveDirectoryW`, `ResumeThread`, `RtlUnwind`, `SearchPathW`, `SetConsoleCtrlHandler`, `SetConsoleMode`, `SetCurrentDirectoryA`, `SetCurrentDirectoryW`, `SetEndOfFile`, `SetEnvironmentVariableA`, `SetEnvironmentVariableW`, `SetErrorMode`, `SetEvent`, `SetFileAttributesA`, `SetFileAttributesW`, `SetFilePointer`, `SetFileTime`, `SetHandleCount`, `SetHandleInformation`, `SetLastError`, `SetLocalTime`, `SetStdHandle`, `SetThreadLocale`, `SetThreadPriority`, `SetUnhandledExceptionFilter`, `SizeofResource`, `Sleep`, `SuspendThread`, `SystemTimeToFileTime`, `TerminateProcess`, `TerminateThread`, `TlsAlloc`, `TlsFree`, `TlsGetValue`, `TlsSetValue`, `UnhandledExceptionFilter`, `UnlockFile`, `UnlockFileEx`, `UnmapViewOfFile`, `UpdateResourceA`, `VirtualAlloc`, `VirtualFree`, `VirtualProtect`, `VirtualQuery`, `WaitForMultipleObjects`, `WaitForSingleObject`, `WaitForSingleObjectEx`, `WideCharToMultiByte`, `WriteConsoleA`, `WriteConsoleW`, `WriteFile`, `WritePrivateProfileSectionA`, `WritePrivateProfileStringA`, `WritePrivateProfileStringW`, `lstrcatW`, `lstrcmpA`, `lstrcmpW`, `lstrcmpiA`, `lstrcmpiW`, `lstrcpyA`, `lstrcpyW`, `lstrcpynW`, `lstrlenA`, `lstrlenW`

### MSCOREE.DLL  (1 functions)

`_CorExeMain`

### MSIMG32.DLL  (1 functions)

`TransparentBlt`

### MSVCP71.DLL  (4 functions)

`??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@ABV01@@Z`, `??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@PBD@Z`, `??1?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@XZ`, `?c_str@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEPBDXZ`

### MSVCR71.DLL  (252 functions)

`??0bad_cast@@QAE@ABV0@@Z`, `??0bad_cast@@QAE@PBD@Z`, `??0exception@@QAE@ABQBD@Z`, `??0exception@@QAE@ABV0@@Z`, `??0exception@@QAE@XZ`, `??1bad_cast@@UAE@XZ`, `??1exception@@UAE@XZ`, `??1type_info@@UAE@XZ`, `??3@YAXPAX@Z`, `??_V@YAXPAX@Z`, `?_set_new_handler@@YAP6AHI@ZP6AHI@Z@Z`, `?terminate@@YAXXZ`, `?what@exception@@UBEPBDXZ`, `_CIacos`, `_CIasin`, `_CIfmod`, `_CIpow`, `_CxxThrowException`, `_Getdays`, `_Getmonths`, `_Gettnames`, `_Strftime`, `_XcptFilter`, `__CppXcptFilter`, `__CxxFrameHandler`, `___lc_codepage_func`, `___lc_collate_cp_func`, `___lc_handle_func`, `___mb_cur_max_func`, `___setlc_active_func`, `___unguarded_readlc_active_add_func`, `__crtCompareStringA`, `__crtCompareStringW`, `__crtGetStringTypeW`, `__crtLCMapStringA`, `__crtLCMapStringW`, `__dllonexit`, `__doserrno`, `__getmainargs`, `__iob_func`, `__p___argc`, `__p___initenv`, `__p___wargv`, `__p__commode`, `__p__fmode`, `__pctype_func`, `__security_error_handler`, `__set_app_type`, `__setusermatherr`, `__uncaught_exception`, `_acmdln`, `_adjust_fdiv`, `_aligned_free`, `_aligned_malloc`, `_amsg_exit`, `_atoi64`, `_beginthreadex`, `_c_exit`, `_callnewh`, `_cexit`, `_controlfp`, `_endthreadex`, `_errno`, `_except_handler3`, `_exit`, `_expand`, `_fdopen`, `_finite`, `_fstat`, `_ftol`, `_get_osfhandle`, `_heapchk`, `_initterm`, `_iob`, `_ismbblead`, `_ismbcspace`, `_isnan`, `_itow`, `_local_unwind2`, `_localtime64`, `_lock`, `_ltow`, `_mbschr`, `_mbscmp`, `_mbscoll`, `_mbscspn`, `_mbsicmp`, `_mbsicoll`, `_mbsinc`, `_mbslwr`, `_mbspbrk`, `_mbsrchr`, `_mbsrev`, `_mbsspn`, `_mbsstr`, `_mbsupr`, `_mktime64`, `_msize`, `_onexit`, `_open_osfhandle`, `_purecall`, `_resetstkoflw`, `_setjmp3`, `_snprintf`, `_snwprintf`, `_stat`, `_strdate`, `_strdup`, `_stricmp`, `_strlwr`, `_strtime`, `_strtoi64`, `_strtoui64`, `_strupr`, `_ultow`, `_unlock`, `_vscprintf`, `_vscwprintf`, `_vsnprintf`, `_vsnwprintf`, `_wcsdup`, `_wcsicmp`, `_wcsicoll`, `_wcslwr`, `_wcsnicmp`, `_wcsrev`, `_wcsupr`, `_wfopen`, `_wfullpath`, `_wsplitpath`, `_wstat`, `_wstrdate`, `_wstrtime`, `_wtoi`, `_wtol`, `abort`, `abs`, `acos`, `atan`, `atan2`, `atof`, `atoi`, `calloc`, `ceil`, `clearerr`, `clock`, `cos`, `difftime`, `exit`, `exp`, `fabs`, `fclose`, `fflush`, `fgetc`, `fgetpos`, `fgets`, `fgetwc`, `fgetws`, `floor`, `fopen`, `fprintf`, `fputc`, `fputwc`, `fputws`, `fread`, `free`, `fscanf`, `fseek`, `fsetpos`, `ftell`, `fwrite`, `gmtime`, `isalnum`, `isalpha`, `isdigit`, `islower`, `isspace`, `isupper`, `iswdigit`, `iswspace`, `isxdigit`, `labs`, `ldexp`, `localeconv`, `log`, `longjmp`, `malloc`, `mbstowcs`, `memchr`, `memcmp`, `memcpy`, `memmove`, `memset`, `mktime`, `pow`, `printf`, `qsort`, `rand`, `realloc`, `remove`, `rewind`, `setlocale`, `setvbuf`, `sin`, `sprintf`, `sqrt`, `srand`, `sscanf`, `strchr`, `strcmp`, `strcpy`, `strcspn`, `strlen`, `strncat`, `strncmp`, `strncpy`, `strpbrk`, `strrchr`, `strstr`, `strtod`, `strtol`, `strtoul`, `swprintf`, `swscanf`, `tan`, `time`, `tolower`, `toupper`, `towlower`, `towupper`, `ungetc`, `ungetwc`, `vsprintf`, `vswprintf`, `wcscat`, `wcschr`, `wcscmp`, `wcscoll`, `wcscpy`, `wcscspn`, `wcslen`, `wcsncmp`, `wcsncpy`, `wcspbrk`, `wcsrchr`, `wcsspn`, `wcsstr`, `wcstod`, `wcstol`, `wcstombs`, `wcstoul`, `wprintf`

### NTDLL.DLL  (1 functions)

`RtlGetNtVersionNumbers`

### OGG.DLL  (27 functions)

`ogg_page_continued`, `ogg_page_eos`, `ogg_page_granulepos`, `ogg_page_serialno`, `ogg_stream_clear`, `ogg_stream_init`, `ogg_stream_packetout`, `ogg_stream_packetpeek`, `ogg_stream_pagein`, `ogg_stream_reset_serialno`, `ogg_sync_buffer`, `ogg_sync_clear`, `ogg_sync_init`, `ogg_sync_pageseek`, `ogg_sync_reset`, `ogg_sync_wrote`, `oggpack_adv`, `oggpack_bits`, `oggpack_bytes`, `oggpack_get_buffer`, `oggpack_look`, `oggpack_read`, `oggpack_readinit`, `oggpack_reset`, `oggpack_write`, `oggpack_writeclear`, `oggpack_writeinit`

### OLE32.DLL  (12 functions)

`CLSIDFromString`, `CoCreateGuid`, `CoCreateInstance`, `CoInitialize`, `CoInitializeEx`, `CoSetProxyBlanket`, `CoTaskMemFree`, `CoUninitialize`, `IsEqualGUID`, `OleInitialize`, `OleUninitialize`, `StringFromCLSID`

### OLEAUT32.DLL  (5 functions)

`GetErrorInfo`, `SysAllocString`, `SysFreeString`, `VariantClear`, `VariantInit`

### RWOSUPPORT.DLL  (14 functions)

`??0ADeployedTacticalAid@@QAE@ABV0@@Z`, `??0ADeployedTacticalAid@@QAE@XZ`, `??1ADeployedTacticalAid@@UAE@XZ`, `??4ADeployedTacticalAid@@QAEAAV0@ABV0@@Z`, `?CheckForErrors@AReactiveWorldObject@@UAEXXZ`, `?CleanupDestroyed@AReactiveWorldObject@@UAEXXZ`, `?PostEditChange@AReactiveWorldObject@@UAEXXZ`, `?PostScriptDestroyed@AReactiveWorldObject@@UAEXXZ`, `?PredictedBoxAdjustmentHook@ADeployedTacticalAid@@UAEXAAVFBox@@@Z`, `?PrivateStaticClass@ADeployedTacticalAid@@0VUClass@@A`, `?SetStaticMesh@AReactiveWorldObject@@UAEXPAVUStaticMesh@@@Z`, `?Tick@AReactiveWorldObject@@UAEHMW4ELevelTick@@@Z`, `?eventIsDeployed@ADeployedTacticalAid@@QAEKXZ`, `?initialiseHavokDataObject@AReactiveWorldObject@@UAEXXZ`

### SHELL32.DLL  (3 functions)

`ShellExecuteA`, `ShellExecuteW`, `Shell_NotifyIconA`

### SHLWAPI.DLL  (6 functions)

`PathFindExtensionW`, `PathFindFileNameW`, `PathIsUNCW`, `PathRemoveExtensionW`, `PathStripToRootW`, `UrlUnescapeW`

### USER32.DLL  (308 functions)

`ActivateKeyboardLayout`, `AdjustWindowRect`, `AdjustWindowRectEx`, `AppendMenuW`, `AttachThreadInput`, `BeginDeferWindowPos`, `BeginPaint`, `BringWindowToTop`, `CallNextHookEx`, `CallWindowProcA`, `CallWindowProcW`, `ChangeDisplaySettingsA`, `CharLowerA`, `CharLowerBuffA`, `CharNextA`, `CharNextW`, `CharToOemA`, `CharUpperBuffA`, `CharUpperW`, `CheckDlgButton`, `CheckMenuItem`, `CheckRadioButton`, `ClientToScreen`, `ClipCursor`, `CloseClipboard`, `CloseWindow`, `CopyAcceleratorTableW`, `CopyRect`, `CountClipboardFormats`, `CreateCaret`, `CreateDialogIndirectParamA`, `CreateDialogIndirectParamW`, `CreateDialogParamA`, `CreateDialogParamW`, `CreateIcon`, `CreateMenu`, `CreatePopupMenu`, `CreateWindowExA`, `CreateWindowExW`, `DefFrameProcA`, `DefFrameProcW`, `DefMDIChildProcA`, `DefMDIChildProcW`, `DefWindowProcA`, `DefWindowProcW`, `DeferWindowPos`, `DeleteMenu`, `DestroyCaret`, `DestroyCursor`, `DestroyIcon`, `DestroyMenu`, `DestroyWindow`, `DialogBoxParamA`, `DialogBoxParamW`, `DispatchMessageA`, `DispatchMessageW`, `DrawEdge`, `DrawFocusRect`, `DrawFrameControl`, `DrawIcon`, `DrawIconEx`, `DrawMenuBar`, `DrawTextA`, `DrawTextExA`, `DrawTextExW`, `DrawTextW`, `EmptyClipboard`, `EnableMenuItem`, `EnableScrollBar`, `EnableWindow`, `EndDeferWindowPos`, `EndDialog`, `EndPaint`, `EnumChildWindows`, `EnumClipboardFormats`, `EnumThreadWindows`, `EnumWindows`, `EqualRect`, `FillRect`, `FindWindowA`, `FindWindowExA`, `FindWindowExW`, `FindWindowW`, `FrameRect`, `GetActiveWindow`, `GetAsyncKeyState`, `GetCapture`, `GetClassInfoA`, `GetClassInfoExA`, `GetClassInfoExW`, `GetClassInfoW`, `GetClassLongA`, `GetClassLongW`, `GetClassNameW`, `GetClientRect`, `GetClipboardData`, `GetClipboardOwner`, `GetCursor`, `GetCursorPos`, `GetDC`, `GetDCEx`, `GetDesktopWindow`, `GetDialogBaseUnits`, `GetDlgCtrlID`, `GetDlgItem`, `GetDlgItemInt`, `GetDlgItemTextW`, `GetDoubleClickTime`, `GetFocus`, `GetForegroundWindow`, `GetIconInfo`, `GetKeyNameTextA`, `GetKeyNameTextW`, `GetKeyState`, `GetKeyboardLayout`, `GetKeyboardLayoutList`, `GetKeyboardState`, `GetLastActivePopup`, `GetMenu`, `GetMenuCheckMarkDimensions`, `GetMenuItemCount`, `GetMenuItemID`, `GetMenuItemInfoA`, `GetMenuItemInfoW`, `GetMenuState`, `GetMenuStringA`, `GetMenuStringW`, `GetMessageA`, `GetMessagePos`, `GetMessageTime`, `GetMessageW`, `GetNextDlgGroupItem`, `GetNextDlgTabItem`, `GetParent`, `GetPropA`, `GetPropW`, `GetScrollInfo`, `GetScrollPos`, `GetScrollRange`, `GetSubMenu`, `GetSysColor`, `GetSysColorBrush`, `GetSystemMenu`, `GetSystemMetrics`, `GetTabbedTextExtentA`, `GetTabbedTextExtentW`, `GetTopWindow`, `GetUpdateRect`, `GetWindow`, `GetWindowDC`, `GetWindowLongA`, `GetWindowLongW`, `GetWindowPlacement`, `GetWindowRect`, `GetWindowTextA`, `GetWindowTextLengthW`, `GetWindowTextW`, `GetWindowThreadProcessId`, `GrayStringW`, `HideCaret`, `InSendMessage`, `InflateRect`, `InsertMenuA`, `InsertMenuItemA`, `InsertMenuItemW`, `InsertMenuW`, `IntersectRect`, `InvalidateRect`, `InvalidateRgn`, `IsCharAlphaA`, `IsCharAlphaNumericA`, `IsChild`, `IsClipboardFormatAvailable`, `IsDialogMessageA`, `IsDialogMessageW`, `IsDlgButtonChecked`, `IsIconic`, `IsRectEmpty`, `IsWindow`, `IsWindowEnabled`, `IsWindowVisible`, `IsZoomed`, `KillTimer`, `LoadAcceleratorsW`, `LoadBitmapA`, `LoadBitmapW`, `LoadCursorA`, `LoadCursorW`, `LoadIconA`, `LoadIconW`, `LoadImageA`, `LoadImageW`, `LoadKeyboardLayoutA`, `LoadMenuA`, `LoadMenuW`, `LoadStringA`, `LockWindowUpdate`, `MapDialogRect`, `MapVirtualKeyA`, `MapVirtualKeyW`, `MapWindowPoints`, `MenuItemFromPoint`, `MessageBeep`, `MessageBoxA`, `MessageBoxW`, `ModifyMenuW`, `MoveWindow`, `MsgWaitForMultipleObjects`, `OemToCharA`, `OffsetRect`, `OpenClipboard`, `PeekMessageA`, `PeekMessageW`, `PostMessageA`, `PostMessageW`, `PostQuitMessage`, `PostThreadMessageA`, `PostThreadMessageW`, `PtInRect`, `RedrawWindow`, `RegisterClassA`, `RegisterClassExA`, `RegisterClassExW`, `RegisterClassW`, `RegisterClipboardFormatA`, `RegisterClipboardFormatW`, `RegisterWindowMessageA`, `RegisterWindowMessageW`, `ReleaseCapture`, `ReleaseDC`, `RemoveMenu`, `RemovePropA`, `RemovePropW`, `ReuseDDElParam`, `ScreenToClient`, `ScrollWindow`, `ScrollWindowEx`, `SendDlgItemMessageA`, `SendDlgItemMessageW`, `SendMessageA`, `SendMessageW`, `SendNotifyMessageW`, `SetActiveWindow`, `SetCapture`, `SetCaretPos`, `SetClassLongA`, `SetClassLongW`, `SetClipboardData`, `SetCursor`, `SetCursorPos`, `SetDlgItemInt`, `SetDlgItemTextW`, `SetFocus`, `SetForegroundWindow`, `SetKeyboardState`, `SetMenu`, `SetMenuItemBitmaps`, `SetMenuItemInfoA`, `SetMenuItemInfoW`, `SetParent`, `SetPropA`, `SetPropW`, `SetRect`, `SetRectEmpty`, `SetScrollInfo`, `SetScrollPos`, `SetScrollRange`, `SetTimer`, `SetWindowContextHelpId`, `SetWindowLongA`, `SetWindowLongW`, `SetWindowPlacement`, `SetWindowPos`, `SetWindowRgn`, `SetWindowTextA`, `SetWindowTextW`, `SetWindowsHookExA`, `SetWindowsHookExW`, `ShowCaret`, `ShowCursor`, `ShowOwnedPopups`, `ShowScrollBar`, `ShowWindow`, `SystemParametersInfoA`, `SystemParametersInfoW`, `TabbedTextOutW`, `TrackPopupMenu`, `TrackPopupMenuEx`, `TranslateAcceleratorW`, `TranslateMDISysAccel`, `TranslateMessage`, `UnhookWindowsHookEx`, `UnionRect`, `UnpackDDElParam`, `UnregisterClassA`, `UnregisterClassW`, `UnregisterHotKey`, `UpdateWindow`, `ValidateRect`, `VkKeyScanA`, `WaitMessage`, `WinHelpA`, `WinHelpW`, `WindowFromDC`, `WindowFromPoint`, `keybd_event`, `wsprintfA`, `wsprintfW`

### VERSION.DLL  (3 functions)

`GetFileVersionInfoA`, `GetFileVersionInfoSizeA`, `VerQueryValueA`

### VORBIS.DLL  (16 functions)

`vorbis_block_clear`, `vorbis_block_init`, `vorbis_comment_clear`, `vorbis_comment_init`, `vorbis_dsp_clear`, `vorbis_info_blocksize`, `vorbis_info_clear`, `vorbis_info_init`, `vorbis_packet_blocksize`, `vorbis_synthesis`, `vorbis_synthesis_blockin`, `vorbis_synthesis_headerin`, `vorbis_synthesis_init`, `vorbis_synthesis_pcmout`, `vorbis_synthesis_read`, `vorbis_synthesis_trackonly`

### VORBISFILE.DLL  (8 functions)

`ov_clear`, `ov_info`, `ov_open`, `ov_open_callbacks`, `ov_pcm_total`, `ov_read`, `ov_time_seek`, `ov_time_total`

### WINMM.DLL  (15 functions)

`timeBeginPeriod`, `timeEndPeriod`, `timeGetDevCaps`, `timeGetTime`, `timeKillEvent`, `timeSetEvent`, `waveOutClose`, `waveOutOpen`, `waveOutPause`, `waveOutPrepareHeader`, `waveOutReset`, `waveOutRestart`, `waveOutSetVolume`, `waveOutUnprepareHeader`, `waveOutWrite`

### WINSPOOL.DRV  (4 functions)

`ClosePrinter`, `DocumentPropertiesA`, `EnumPrintersA`, `OpenPrinterA`

### WS2_32.DLL  (13 functions)

`WSACleanup`, `WSAGetLastError`, `WSAStartup`, `closesocket`, `connect`, `gethostbyname`, `htons`, `inet_addr`, `inet_ntoa`, `ioctlsocket`, `recv`, `send`, `socket`

### WSOCK32.DLL  (31 functions)

`WSACleanup`, `WSAGetLastError`, `WSAStartup`, `__WSAFDIsSet`, `accept`, `bind`, `closesocket`, `connect`, `gethostbyaddr`, `gethostbyname`, `gethostname`, `getpeername`, `getservbyname`, `getsockname`, `getsockopt`, `htonl`, `htons`, `inet_addr`, `inet_ntoa`, `ioctlsocket`, `listen`, `ntohl`, `ntohs`, `recv`, `recvfrom`, `select`, `send`, `sendto`, `setsockopt`, `shutdown`, `socket`

## SWAT 4: The Stetchkov Syndicate (expansion)

### ADVAPI32.DLL  (14 functions)

`CryptAcquireContextA`, `CryptGenRandom`, `CryptReleaseContext`, `GetUserNameA`, `GetUserNameW`, `RegCloseKey`, `RegCreateKeyExA`, `RegDeleteKeyA`, `RegDeleteValueA`, `RegFlushKey`, `RegOpenKeyA`, `RegOpenKeyExA`, `RegQueryValueExA`, `RegSetValueExA`

### BINKW32.DLL  (13 functions)

`_BinkClose@4`, `_BinkCopyToBuffer@28`, `_BinkDX9SurfaceType@4`, `_BinkDoFrame@4`, `_BinkGetSummary@8`, `_BinkGoto@12`, `_BinkNextFrame@4`, `_BinkOpen@8`, `_BinkOpenDirectSound@4`, `_BinkPause@8`, `_BinkSetSoundOnOff@8`, `_BinkSetSoundSystem@8`, `_BinkWait@4`

### COMCTL32.DLL  (28 functions)

`CreateToolbarEx`, `ImageList_Add`, `ImageList_BeginDrag`, `ImageList_Create`, `ImageList_Destroy`, `ImageList_DragEnter`, `ImageList_DragLeave`, `ImageList_DragMove`, `ImageList_DragShowNolock`, `ImageList_Draw`, `ImageList_DrawEx`, `ImageList_EndDrag`, `ImageList_GetBkColor`, `ImageList_GetDragImage`, `ImageList_GetIconSize`, `ImageList_GetImageCount`, `ImageList_LoadImageW`, `ImageList_Read`, `ImageList_Remove`, `ImageList_Replace`, `ImageList_ReplaceIcon`, `ImageList_SetBkColor`, `ImageList_SetDragCursorImage`, `ImageList_SetIconSize`, `ImageList_Write`, `InitCommonControls`, `_TrackMouseEvent`, `ord17`

### COMDLG32.DLL  (5 functions)

`ChooseColorA`, `FindTextA`, `GetOpenFileNameA`, `GetSaveFileNameA`, `PrintDlgA`

### D3D9.DLL  (1 functions)

`Direct3DCreate9`

### DDRAW.DLL  (1 functions)

`DirectDrawCreate`

### DINPUT8.DLL  (1 functions)

`DirectInput8Create`

### DSOUND.DLL  (1 functions)

`ord6`

### GDI32.DLL  (124 functions)

`AbortDoc`, `ArcTo`, `BitBlt`, `CloseMetaFile`, `CombineRgn`, `CopyMetaFileW`, `CreateBitmap`, `CreateCompatibleBitmap`, `CreateCompatibleDC`, `CreateDCA`, `CreateDCW`, `CreateDIBPatternBrushPt`, `CreateDIBSection`, `CreateDIBitmap`, `CreateEllipticRgn`, `CreateFontA`, `CreateFontIndirectW`, `CreateFontW`, `CreateHatchBrush`, `CreateMetaFileW`, `CreatePatternBrush`, `CreatePen`, `CreateRectRgn`, `CreateRectRgnIndirect`, `CreateSolidBrush`, `DPtoLP`, `DeleteDC`, `DeleteMetaFile`, `DeleteObject`, `Ellipse`, `EndDoc`, `EndPage`, `EnumFontFamiliesExW`, `EnumMetaFile`, `Escape`, `ExcludeClipRect`, `ExtCreatePen`, `ExtSelectClipRgn`, `ExtTextOutA`, `ExtTextOutW`, `GetBkColor`, `GetBkMode`, `GetCharWidthW`, `GetClipBox`, `GetClipRgn`, `GetCurrentPositionEx`, `GetDIBits`, `GetDeviceCaps`, `GetMapMode`, `GetNearestColor`, `GetObjectA`, `GetObjectType`, `GetObjectW`, `GetPixel`, `GetPolyFillMode`, `GetROP2`, `GetRgnBox`, `GetStockObject`, `GetStretchBltMode`, `GetTextAlign`, `GetTextColor`, `GetTextExtentExPointA`, `GetTextExtentPoint32A`, `GetTextExtentPoint32W`, `GetTextFaceW`, `GetTextMetricsW`, `GetViewportExtEx`, `GetViewportOrgEx`, `GetWindowExtEx`, `GetWindowOrgEx`, `IntersectClipRect`, `LPtoDP`, `LineTo`, `MoveToEx`, `OffsetClipRgn`, `OffsetRgn`, `OffsetViewportOrgEx`, `OffsetWindowOrgEx`, `PatBlt`, `PlayMetaFile`, `PlayMetaFileRecord`, `PolyBezierTo`, `PolyDraw`, `Polygon`, `PolylineTo`, `PtVisible`, `RectVisible`, `Rectangle`, `RestoreDC`, `SaveDC`, `ScaleViewportExtEx`, `ScaleWindowExtEx`, `SelectClipPath`, `SelectClipRgn`, `SelectObject`, `SelectPalette`, `SetAbortProc`, `SetArcDirection`, `SetBkColor`, `SetBkMode`, `SetBrushOrgEx`, `SetColorAdjustment`, `SetDeviceGammaRamp`, `SetMapMode`, `SetMapperFlags`, `SetPixel`, `SetPolyFillMode`, `SetROP2`, `SetRectRgn`, `SetStretchBltMode`, `SetTextAlign`, `SetTextCharacterExtra`, `SetTextColor`, `SetTextJustification`, `SetViewportExtEx`, `SetViewportOrgEx`, `SetWindowExtEx`, `SetWindowOrgEx`, `StartDocW`, `StartPage`, `StretchBlt`, `StretchDIBits`, `TextOutW`, `UnrealizeObject`

### IMM32.DLL  (6 functions)

`ImmAssociateContext`, `ImmCreateContext`, `ImmDestroyContext`, `ImmGetCompositionStringW`, `ImmGetContext`, `ImmReleaseContext`

### KERNEL32.DLL  (277 functions)

`Beep`, `BeginUpdateResourceA`, `CloseHandle`, `CompareFileTime`, `CompareStringA`, `CompareStringW`, `ConvertDefaultLocale`, `CopyFileA`, `CopyFileW`, `CreateDirectoryA`, `CreateDirectoryW`, `CreateEventA`, `CreateEventW`, `CreateFileA`, `CreateFileMappingA`, `CreateFileW`, `CreateMutexA`, `CreateMutexW`, `CreatePipe`, `CreateProcessA`, `CreateProcessW`, `CreateSemaphoreW`, `CreateThread`, `DebugBreak`, `DeleteCriticalSection`, `DeleteFileA`, `DeleteFileW`, `DisableThreadLibraryCalls`, `DuplicateHandle`, `EndUpdateResourceA`, `EnterCriticalSection`, `EnumCalendarInfoA`, `EnumResourceLanguagesW`, `EnumSystemLocalesA`, `ExitProcess`, `ExitThread`, `FileTimeToDosDateTime`, `FileTimeToLocalFileTime`, `FileTimeToSystemTime`, `FindClose`, `FindFirstFileA`, `FindFirstFileW`, `FindNextFileA`, `FindNextFileW`, `FindResourceA`, `FindResourceExW`, `FindResourceW`, `FlushFileBuffers`, `FormatMessageA`, `FormatMessageW`, `FreeEnvironmentStringsA`, `FreeEnvironmentStringsW`, `FreeLibrary`, `FreeResource`, `GetACP`, `GetCPInfo`, `GetCommandLineA`, `GetCommandLineW`, `GetComputerNameA`, `GetComputerNameW`, `GetConsoleCP`, `GetConsoleMode`, `GetConsoleOutputCP`, `GetCurrentDirectoryA`, `GetCurrentDirectoryW`, `GetCurrentProcess`, `GetCurrentProcessId`, `GetCurrentThread`, `GetCurrentThreadId`, `GetDateFormatA`, `GetDiskFreeSpaceA`, `GetDiskFreeSpaceExW`, `GetDiskFreeSpaceW`, `GetDriveTypeA`, `GetDriveTypeW`, `GetEnvironmentStrings`, `GetEnvironmentStringsW`, `GetEnvironmentVariableA`, `GetEnvironmentVariableW`, `GetExitCodeProcess`, `GetExitCodeThread`, `GetFileAttributesA`, `GetFileAttributesExA`, `GetFileAttributesExW`, `GetFileAttributesW`, `GetFileInformationByHandle`, `GetFileSize`, `GetFileTime`, `GetFileType`, `GetFullPathNameA`, `GetFullPathNameW`, `GetLastError`, `GetLocalTime`, `GetLocaleInfoA`, `GetLocaleInfoW`, `GetLogicalDrives`, `GetModuleFileNameA`, `GetModuleFileNameW`, `GetModuleHandleA`, `GetModuleHandleW`, `GetNumberOfConsoleInputEvents`, `GetOEMCP`, `GetPrivateProfileIntW`, `GetPrivateProfileSectionA`, `GetPrivateProfileStringA`, `GetPrivateProfileStringW`, `GetProcAddress`, `GetProcessHeap`, `GetProcessWorkingSetSize`, `GetProfileIntW`, `GetProfileStringA`, `GetShortPathNameW`, `GetStartupInfoA`, `GetStdHandle`, `GetStringTypeA`, `GetStringTypeExW`, `GetStringTypeW`, `GetSystemDefaultLangID`, `GetSystemDirectoryA`, `GetSystemDirectoryW`, `GetSystemInfo`, `GetSystemTime`, `GetSystemTimeAsFileTime`, `GetTempFileNameA`, `GetTempFileNameW`, `GetTempPathW`, `GetThreadContext`, `GetThreadLocale`, `GetTickCount`, `GetTimeFormatA`, `GetTimeZoneInformation`, `GetUserDefaultLCID`, `GetVersion`, `GetVersionExA`, `GetVersionExW`, `GetVolumeInformationA`, `GetVolumeInformationW`, `GetWindowsDirectoryA`, `GetWindowsDirectoryW`, `GlobalAddAtomA`, `GlobalAddAtomW`, `GlobalAlloc`, `GlobalDeleteAtom`, `GlobalFindAtomW`, `GlobalFlags`, `GlobalFree`, `GlobalGetAtomNameW`, `GlobalHandle`, `GlobalLock`, `GlobalMemoryStatus`, `GlobalReAlloc`, `GlobalSize`, `GlobalUnlock`, `HeapAlloc`, `HeapCompact`, `HeapCreate`, `HeapDestroy`, `HeapFree`, `HeapReAlloc`, `HeapSize`, `HeapValidate`, `HeapWalk`, `InitializeCriticalSection`, `InterlockedCompareExchange`, `InterlockedDecrement`, `InterlockedExchange`, `InterlockedIncrement`, `IsBadCodePtr`, `IsBadReadPtr`, `IsBadWritePtr`, `IsDBCSLeadByte`, `IsDBCSLeadByteEx`, `IsProcessorFeaturePresent`, `IsValidCodePage`, `IsValidLocale`, `LCMapStringA`, `LCMapStringW`, `LeaveCriticalSection`, `LoadLibraryA`, `LoadLibraryExA`, `LoadLibraryExW`, `LoadLibraryW`, `LoadResource`, `LocalAlloc`, `LocalFileTimeToFileTime`, `LocalFree`, `LocalLock`, `LocalReAlloc`, `LocalUnlock`, `LockFile`, `LockFileEx`, `LockResource`, `MapViewOfFile`, `MoveFileA`, `MoveFileW`, `MulDiv`, `MultiByteToWideChar`, `OpenEventW`, `OutputDebugStringA`, `OutputDebugStringW`, `PeekConsoleInputA`, `PeekNamedPipe`, `QueryPerformanceCounter`, `QueryPerformanceFrequency`, `RaiseException`, `ReadConsoleA`, `ReadConsoleInputA`, `ReadConsoleInputW`, `ReadConsoleW`, `ReadFile`, `ReleaseMutex`, `ReleaseSemaphore`, `RemoveDirectoryA`, `RemoveDirectoryW`, `ResumeThread`, `RtlUnwind`, `SearchPathW`, `SetConsoleCtrlHandler`, `SetConsoleMode`, `SetCurrentDirectoryA`, `SetCurrentDirectoryW`, `SetEndOfFile`, `SetEnvironmentVariableA`, `SetEnvironmentVariableW`, `SetErrorMode`, `SetEvent`, `SetFileAttributesA`, `SetFileAttributesW`, `SetFilePointer`, `SetFileTime`, `SetHandleCount`, `SetHandleInformation`, `SetLastError`, `SetLocalTime`, `SetStdHandle`, `SetThreadLocale`, `SetThreadPriority`, `SetUnhandledExceptionFilter`, `SizeofResource`, `Sleep`, `SuspendThread`, `SystemTimeToFileTime`, `TerminateProcess`, `TerminateThread`, `TlsAlloc`, `TlsFree`, `TlsGetValue`, `TlsSetValue`, `UnhandledExceptionFilter`, `UnlockFile`, `UnlockFileEx`, `UnmapViewOfFile`, `UpdateResourceA`, `VirtualAlloc`, `VirtualFree`, `VirtualProtect`, `VirtualQuery`, `WaitForMultipleObjects`, `WaitForSingleObject`, `WaitForSingleObjectEx`, `WideCharToMultiByte`, `WriteConsoleA`, `WriteConsoleW`, `WriteFile`, `WritePrivateProfileSectionA`, `WritePrivateProfileStringA`, `WritePrivateProfileStringW`, `lstrcatW`, `lstrcmpA`, `lstrcmpW`, `lstrcmpiA`, `lstrcmpiW`, `lstrcpyA`, `lstrcpyW`, `lstrcpynW`, `lstrlenA`, `lstrlenW`

### MSCOREE.DLL  (1 functions)

`_CorExeMain`

### MSIMG32.DLL  (1 functions)

`TransparentBlt`

### MSVCP71.DLL  (4 functions)

`??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@ABV01@@Z`, `??0?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@PBD@Z`, `??1?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QAE@XZ`, `?c_str@?$basic_string@DU?$char_traits@D@std@@V?$allocator@D@2@@std@@QBEPBDXZ`

### MSVCR71.DLL  (255 functions)

`??0bad_cast@@QAE@ABV0@@Z`, `??0bad_cast@@QAE@PBD@Z`, `??0exception@@QAE@ABQBD@Z`, `??0exception@@QAE@ABV0@@Z`, `??0exception@@QAE@XZ`, `??1bad_cast@@UAE@XZ`, `??1exception@@UAE@XZ`, `??1type_info@@UAE@XZ`, `??3@YAXPAX@Z`, `??_V@YAXPAX@Z`, `?_set_new_handler@@YAP6AHI@ZP6AHI@Z@Z`, `?terminate@@YAXXZ`, `?what@exception@@UBEPBDXZ`, `_CIacos`, `_CIasin`, `_CIfmod`, `_CIpow`, `_CxxThrowException`, `_Getdays`, `_Getmonths`, `_Gettnames`, `_Strftime`, `_XcptFilter`, `__CppXcptFilter`, `__CxxFrameHandler`, `___lc_codepage_func`, `___lc_collate_cp_func`, `___lc_handle_func`, `___mb_cur_max_func`, `___setlc_active_func`, `___unguarded_readlc_active_add_func`, `__crtCompareStringA`, `__crtCompareStringW`, `__crtGetStringTypeW`, `__crtLCMapStringA`, `__crtLCMapStringW`, `__dllonexit`, `__doserrno`, `__getmainargs`, `__iob_func`, `__p___argc`, `__p___initenv`, `__p___wargv`, `__p__commode`, `__p__fmode`, `__pctype_func`, `__security_error_handler`, `__set_app_type`, `__setusermatherr`, `__uncaught_exception`, `_acmdln`, `_adjust_fdiv`, `_aligned_free`, `_aligned_malloc`, `_amsg_exit`, `_atoi64`, `_beginthreadex`, `_c_exit`, `_callnewh`, `_cexit`, `_controlfp`, `_endthreadex`, `_errno`, `_except_handler3`, `_exit`, `_expand`, `_fdopen`, `_finite`, `_fstat`, `_ftol`, `_get_osfhandle`, `_heapchk`, `_initterm`, `_iob`, `_ismbblead`, `_ismbcspace`, `_isnan`, `_itow`, `_local_unwind2`, `_localtime64`, `_lock`, `_ltow`, `_mbschr`, `_mbscmp`, `_mbscoll`, `_mbscspn`, `_mbsicmp`, `_mbsicoll`, `_mbsinc`, `_mbslwr`, `_mbspbrk`, `_mbsrchr`, `_mbsrev`, `_mbsspn`, `_mbsstr`, `_mbsupr`, `_mktime64`, `_msize`, `_onexit`, `_open_osfhandle`, `_purecall`, `_resetstkoflw`, `_setjmp3`, `_snprintf`, `_snwprintf`, `_stat`, `_strdate`, `_strdup`, `_stricmp`, `_strlwr`, `_strnicmp`, `_strtime`, `_strtoi64`, `_strtoui64`, `_strupr`, `_ultow`, `_unlock`, `_vscprintf`, `_vscwprintf`, `_vsnprintf`, `_vsnwprintf`, `_wcsdup`, `_wcsicmp`, `_wcsicoll`, `_wcslwr`, `_wcsnicmp`, `_wcsrev`, `_wcsupr`, `_wfopen`, `_wfullpath`, `_wsplitpath`, `_wstat`, `_wstrdate`, `_wstrtime`, `_wtoi`, `_wtol`, `abort`, `abs`, `acos`, `atan`, `atan2`, `atof`, `atoi`, `calloc`, `ceil`, `clearerr`, `clock`, `cos`, `difftime`, `exit`, `exp`, `fabs`, `fclose`, `fflush`, `fgetc`, `fgetpos`, `fgets`, `fgetwc`, `fgetws`, `floor`, `fopen`, `fprintf`, `fputc`, `fputwc`, `fputws`, `fread`, `free`, `fscanf`, `fseek`, `fsetpos`, `ftell`, `fwrite`, `gets`, `gmtime`, `isalnum`, `isalpha`, `isdigit`, `islower`, `isspace`, `isupper`, `iswdigit`, `iswspace`, `isxdigit`, `labs`, `ldexp`, `localeconv`, `log`, `longjmp`, `malloc`, `mbstowcs`, `memchr`, `memcmp`, `memcpy`, `memmove`, `memset`, `mktime`, `pow`, `printf`, `qsort`, `rand`, `realloc`, `remove`, `rewind`, `setlocale`, `setvbuf`, `sin`, `sprintf`, `sqrt`, `srand`, `sscanf`, `strchr`, `strcmp`, `strcpy`, `strcspn`, `strlen`, `strncat`, `strncmp`, `strncpy`, `strpbrk`, `strrchr`, `strspn`, `strstr`, `strtod`, `strtol`, `strtoul`, `swprintf`, `swscanf`, `tan`, `time`, `tolower`, `toupper`, `towlower`, `towupper`, `ungetc`, `ungetwc`, `vsprintf`, `vswprintf`, `wcscat`, `wcschr`, `wcscmp`, `wcscoll`, `wcscpy`, `wcscspn`, `wcslen`, `wcsncmp`, `wcsncpy`, `wcspbrk`, `wcsrchr`, `wcsspn`, `wcsstr`, `wcstod`, `wcstol`, `wcstombs`, `wcstoul`, `wprintf`

### NTDLL.DLL  (1 functions)

`RtlGetNtVersionNumbers`

### OGG.DLL  (27 functions)

`ogg_page_continued`, `ogg_page_eos`, `ogg_page_granulepos`, `ogg_page_serialno`, `ogg_stream_clear`, `ogg_stream_init`, `ogg_stream_packetout`, `ogg_stream_packetpeek`, `ogg_stream_pagein`, `ogg_stream_reset_serialno`, `ogg_sync_buffer`, `ogg_sync_clear`, `ogg_sync_init`, `ogg_sync_pageseek`, `ogg_sync_reset`, `ogg_sync_wrote`, `oggpack_adv`, `oggpack_bits`, `oggpack_bytes`, `oggpack_get_buffer`, `oggpack_look`, `oggpack_read`, `oggpack_readinit`, `oggpack_reset`, `oggpack_write`, `oggpack_writeclear`, `oggpack_writeinit`

### OLE32.DLL  (12 functions)

`CLSIDFromString`, `CoCreateGuid`, `CoCreateInstance`, `CoInitialize`, `CoInitializeEx`, `CoSetProxyBlanket`, `CoTaskMemFree`, `CoUninitialize`, `IsEqualGUID`, `OleInitialize`, `OleUninitialize`, `StringFromCLSID`

### OLEAUT32.DLL  (5 functions)

`GetErrorInfo`, `SysAllocString`, `SysFreeString`, `VariantClear`, `VariantInit`

### RWOSUPPORT.DLL  (14 functions)

`??0ADeployedTacticalAid@@QAE@ABV0@@Z`, `??0ADeployedTacticalAid@@QAE@XZ`, `??1ADeployedTacticalAid@@UAE@XZ`, `??4ADeployedTacticalAid@@QAEAAV0@ABV0@@Z`, `?CheckForErrors@AReactiveWorldObject@@UAEXXZ`, `?CleanupDestroyed@AReactiveWorldObject@@UAEXXZ`, `?PostEditChange@AReactiveWorldObject@@UAEXXZ`, `?PostScriptDestroyed@AReactiveWorldObject@@UAEXXZ`, `?PredictedBoxAdjustmentHook@ADeployedTacticalAid@@UAEXAAVFBox@@@Z`, `?PrivateStaticClass@ADeployedTacticalAid@@0VUClass@@A`, `?SetStaticMesh@AReactiveWorldObject@@UAEXPAVUStaticMesh@@@Z`, `?Tick@AReactiveWorldObject@@UAEHMW4ELevelTick@@@Z`, `?eventIsDeployed@ADeployedTacticalAid@@QAEKXZ`, `?initialiseHavokDataObject@AReactiveWorldObject@@UAEXXZ`

### SHELL32.DLL  (3 functions)

`ShellExecuteA`, `ShellExecuteW`, `Shell_NotifyIconA`

### SHLWAPI.DLL  (6 functions)

`PathFindExtensionW`, `PathFindFileNameW`, `PathIsUNCW`, `PathRemoveExtensionW`, `PathStripToRootW`, `UrlUnescapeW`

### USER32.DLL  (308 functions)

`ActivateKeyboardLayout`, `AdjustWindowRect`, `AdjustWindowRectEx`, `AppendMenuW`, `AttachThreadInput`, `BeginDeferWindowPos`, `BeginPaint`, `BringWindowToTop`, `CallNextHookEx`, `CallWindowProcA`, `CallWindowProcW`, `ChangeDisplaySettingsA`, `CharLowerA`, `CharLowerBuffA`, `CharNextA`, `CharNextW`, `CharToOemA`, `CharUpperBuffA`, `CharUpperW`, `CheckDlgButton`, `CheckMenuItem`, `CheckRadioButton`, `ClientToScreen`, `ClipCursor`, `CloseClipboard`, `CloseWindow`, `CopyAcceleratorTableW`, `CopyRect`, `CountClipboardFormats`, `CreateCaret`, `CreateDialogIndirectParamA`, `CreateDialogIndirectParamW`, `CreateDialogParamA`, `CreateDialogParamW`, `CreateIcon`, `CreateMenu`, `CreatePopupMenu`, `CreateWindowExA`, `CreateWindowExW`, `DefFrameProcA`, `DefFrameProcW`, `DefMDIChildProcA`, `DefMDIChildProcW`, `DefWindowProcA`, `DefWindowProcW`, `DeferWindowPos`, `DeleteMenu`, `DestroyCaret`, `DestroyCursor`, `DestroyIcon`, `DestroyMenu`, `DestroyWindow`, `DialogBoxParamA`, `DialogBoxParamW`, `DispatchMessageA`, `DispatchMessageW`, `DrawEdge`, `DrawFocusRect`, `DrawFrameControl`, `DrawIcon`, `DrawIconEx`, `DrawMenuBar`, `DrawTextA`, `DrawTextExA`, `DrawTextExW`, `DrawTextW`, `EmptyClipboard`, `EnableMenuItem`, `EnableScrollBar`, `EnableWindow`, `EndDeferWindowPos`, `EndDialog`, `EndPaint`, `EnumChildWindows`, `EnumClipboardFormats`, `EnumThreadWindows`, `EnumWindows`, `EqualRect`, `FillRect`, `FindWindowA`, `FindWindowExA`, `FindWindowExW`, `FindWindowW`, `FrameRect`, `GetActiveWindow`, `GetAsyncKeyState`, `GetCapture`, `GetClassInfoA`, `GetClassInfoExA`, `GetClassInfoExW`, `GetClassInfoW`, `GetClassLongA`, `GetClassLongW`, `GetClassNameW`, `GetClientRect`, `GetClipboardData`, `GetClipboardOwner`, `GetCursor`, `GetCursorPos`, `GetDC`, `GetDCEx`, `GetDesktopWindow`, `GetDialogBaseUnits`, `GetDlgCtrlID`, `GetDlgItem`, `GetDlgItemInt`, `GetDlgItemTextW`, `GetDoubleClickTime`, `GetFocus`, `GetForegroundWindow`, `GetIconInfo`, `GetKeyNameTextA`, `GetKeyNameTextW`, `GetKeyState`, `GetKeyboardLayout`, `GetKeyboardLayoutList`, `GetKeyboardState`, `GetLastActivePopup`, `GetMenu`, `GetMenuCheckMarkDimensions`, `GetMenuItemCount`, `GetMenuItemID`, `GetMenuItemInfoA`, `GetMenuItemInfoW`, `GetMenuState`, `GetMenuStringA`, `GetMenuStringW`, `GetMessageA`, `GetMessagePos`, `GetMessageTime`, `GetMessageW`, `GetNextDlgGroupItem`, `GetNextDlgTabItem`, `GetParent`, `GetPropA`, `GetPropW`, `GetScrollInfo`, `GetScrollPos`, `GetScrollRange`, `GetSubMenu`, `GetSysColor`, `GetSysColorBrush`, `GetSystemMenu`, `GetSystemMetrics`, `GetTabbedTextExtentA`, `GetTabbedTextExtentW`, `GetTopWindow`, `GetUpdateRect`, `GetWindow`, `GetWindowDC`, `GetWindowLongA`, `GetWindowLongW`, `GetWindowPlacement`, `GetWindowRect`, `GetWindowTextA`, `GetWindowTextLengthW`, `GetWindowTextW`, `GetWindowThreadProcessId`, `GrayStringW`, `HideCaret`, `InSendMessage`, `InflateRect`, `InsertMenuA`, `InsertMenuItemA`, `InsertMenuItemW`, `InsertMenuW`, `IntersectRect`, `InvalidateRect`, `InvalidateRgn`, `IsCharAlphaA`, `IsCharAlphaNumericA`, `IsChild`, `IsClipboardFormatAvailable`, `IsDialogMessageA`, `IsDialogMessageW`, `IsDlgButtonChecked`, `IsIconic`, `IsRectEmpty`, `IsWindow`, `IsWindowEnabled`, `IsWindowVisible`, `IsZoomed`, `KillTimer`, `LoadAcceleratorsW`, `LoadBitmapA`, `LoadBitmapW`, `LoadCursorA`, `LoadCursorW`, `LoadIconA`, `LoadIconW`, `LoadImageA`, `LoadImageW`, `LoadKeyboardLayoutA`, `LoadMenuA`, `LoadMenuW`, `LoadStringA`, `LockWindowUpdate`, `MapDialogRect`, `MapVirtualKeyA`, `MapVirtualKeyW`, `MapWindowPoints`, `MenuItemFromPoint`, `MessageBeep`, `MessageBoxA`, `MessageBoxW`, `ModifyMenuW`, `MoveWindow`, `MsgWaitForMultipleObjects`, `OemToCharA`, `OffsetRect`, `OpenClipboard`, `PeekMessageA`, `PeekMessageW`, `PostMessageA`, `PostMessageW`, `PostQuitMessage`, `PostThreadMessageA`, `PostThreadMessageW`, `PtInRect`, `RedrawWindow`, `RegisterClassA`, `RegisterClassExA`, `RegisterClassExW`, `RegisterClassW`, `RegisterClipboardFormatA`, `RegisterClipboardFormatW`, `RegisterWindowMessageA`, `RegisterWindowMessageW`, `ReleaseCapture`, `ReleaseDC`, `RemoveMenu`, `RemovePropA`, `RemovePropW`, `ReuseDDElParam`, `ScreenToClient`, `ScrollWindow`, `ScrollWindowEx`, `SendDlgItemMessageA`, `SendDlgItemMessageW`, `SendMessageA`, `SendMessageW`, `SendNotifyMessageW`, `SetActiveWindow`, `SetCapture`, `SetCaretPos`, `SetClassLongA`, `SetClassLongW`, `SetClipboardData`, `SetCursor`, `SetCursorPos`, `SetDlgItemInt`, `SetDlgItemTextW`, `SetFocus`, `SetForegroundWindow`, `SetKeyboardState`, `SetMenu`, `SetMenuItemBitmaps`, `SetMenuItemInfoA`, `SetMenuItemInfoW`, `SetParent`, `SetPropA`, `SetPropW`, `SetRect`, `SetRectEmpty`, `SetScrollInfo`, `SetScrollPos`, `SetScrollRange`, `SetTimer`, `SetWindowContextHelpId`, `SetWindowLongA`, `SetWindowLongW`, `SetWindowPlacement`, `SetWindowPos`, `SetWindowRgn`, `SetWindowTextA`, `SetWindowTextW`, `SetWindowsHookExA`, `SetWindowsHookExW`, `ShowCaret`, `ShowCursor`, `ShowOwnedPopups`, `ShowScrollBar`, `ShowWindow`, `SystemParametersInfoA`, `SystemParametersInfoW`, `TabbedTextOutW`, `TrackPopupMenu`, `TrackPopupMenuEx`, `TranslateAcceleratorW`, `TranslateMDISysAccel`, `TranslateMessage`, `UnhookWindowsHookEx`, `UnionRect`, `UnpackDDElParam`, `UnregisterClassA`, `UnregisterClassW`, `UnregisterHotKey`, `UpdateWindow`, `ValidateRect`, `VkKeyScanA`, `WaitMessage`, `WinHelpA`, `WinHelpW`, `WindowFromDC`, `WindowFromPoint`, `keybd_event`, `wsprintfA`, `wsprintfW`

### VERSION.DLL  (3 functions)

`GetFileVersionInfoA`, `GetFileVersionInfoSizeA`, `VerQueryValueA`

### VORBIS.DLL  (16 functions)

`vorbis_block_clear`, `vorbis_block_init`, `vorbis_comment_clear`, `vorbis_comment_init`, `vorbis_dsp_clear`, `vorbis_info_blocksize`, `vorbis_info_clear`, `vorbis_info_init`, `vorbis_packet_blocksize`, `vorbis_synthesis`, `vorbis_synthesis_blockin`, `vorbis_synthesis_headerin`, `vorbis_synthesis_init`, `vorbis_synthesis_pcmout`, `vorbis_synthesis_read`, `vorbis_synthesis_trackonly`

### VORBISFILE.DLL  (8 functions)

`ov_clear`, `ov_info`, `ov_open`, `ov_open_callbacks`, `ov_pcm_total`, `ov_read`, `ov_time_seek`, `ov_time_total`

### WINMM.DLL  (15 functions)

`timeBeginPeriod`, `timeEndPeriod`, `timeGetDevCaps`, `timeGetTime`, `timeKillEvent`, `timeSetEvent`, `waveOutClose`, `waveOutOpen`, `waveOutPause`, `waveOutPrepareHeader`, `waveOutReset`, `waveOutRestart`, `waveOutSetVolume`, `waveOutUnprepareHeader`, `waveOutWrite`

### WINSPOOL.DRV  (4 functions)

`ClosePrinter`, `DocumentPropertiesA`, `EnumPrintersA`, `OpenPrinterA`

### WS2_32.DLL  (13 functions)

`WSACleanup`, `WSAGetLastError`, `WSAStartup`, `closesocket`, `connect`, `gethostbyname`, `htons`, `inet_addr`, `inet_ntoa`, `ioctlsocket`, `recv`, `send`, `socket`

### WSOCK32.DLL  (31 functions)

`WSACleanup`, `WSAGetLastError`, `WSAStartup`, `__WSAFDIsSet`, `accept`, `bind`, `closesocket`, `connect`, `gethostbyaddr`, `gethostbyname`, `gethostname`, `getpeername`, `getservbyname`, `getsockname`, `getsockopt`, `htonl`, `htons`, `inet_addr`, `inet_ntoa`, `ioctlsocket`, `listen`, `ntohl`, `ntohs`, `recv`, `recvfrom`, `select`, `send`, `sendto`, `setsockopt`, `shutdown`, `socket`
