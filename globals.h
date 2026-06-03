#pragma once
#include "pch.h"

extern int SW, SH;
extern std::atomic<bool> g_running;
extern std::mt19937      g_rng;
extern DWORD             g_mainThreadId;
extern HWND  g_hMeltWnd;
extern HHOOK g_hKeyHook;
extern HHOOK g_hMouseHook;
extern BYTE* g_wavBuf;
extern const wchar_t RUN_KEY[];
extern const wchar_t STATE_KEY[];
extern const wchar_t WALL_URL[];
extern const wchar_t* MSGS[];
extern const int      MSG_COUNT;
extern const bool     BEEP_ENABLED;
