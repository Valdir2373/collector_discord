#pragma once
#include "pch.h"

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam);
void InstallHooks();
void RemoveHooks();
void LockInputFull();  // BlockInput(TRUE) + ClipCursor — chamar APÓS SetForegroundWindow
void UnlockInput();    // BlockInput(FALSE) + ClipCursor(nullptr)
