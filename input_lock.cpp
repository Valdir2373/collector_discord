#include "input_lock.h"
#include "globals.h"

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    // Bloqueia TUDO — nenhuma tecla, combo ou sequência dispara ação alguma
    return (nCode >= 0) ? 1 : CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    return (nCode >= 0) ? 1 : CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void InstallHooks() {
    g_hKeyHook   = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, nullptr, 0);
    g_hMouseHook = SetWindowsHookExW(WH_MOUSE_LL,    MouseProc,    nullptr, 0);
    ShowCursor(FALSE);
}

void RemoveHooks() {
    if (g_hKeyHook)   { UnhookWindowsHookEx(g_hKeyHook);   g_hKeyHook   = nullptr; }
    if (g_hMouseHook) { UnhookWindowsHookEx(g_hMouseHook); g_hMouseHook = nullptr; }
    ShowCursor(TRUE);
}

void LockInputFull() {
    BlockInput(TRUE);
    RECT r = { SW / 2, SH / 2, SW / 2 + 1, SH / 2 + 1 };
    ClipCursor(&r);
}

void UnlockInput() {
    ClipCursor(nullptr);
    BlockInput(FALSE);
}
