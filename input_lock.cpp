#include "input_lock.h"
#include "globals.h"

// ─── Estado interno das teclas ─────────────────────────────────────────────────
static bool s_keys[256] = {};

// ─── Máquina de estados do combo sequencial ────────────────────────────────────
// Sequência: [Ctrl+Tab+Z] soltar → [Ctrl+Alt+X] → sai
// Estado 0: aguardando Ctrl+Tab+Z
// Estado 1: Ctrl+Tab+Z completado — aguardando Ctrl+Alt+X (timeout 5s)
static int   s_comboState = 0;
static DWORD s_comboTime  = 0;
static const DWORD COMBO_TIMEOUT = 5000;

static void DoExit() {
    g_running = false;
    ClipCursor(nullptr);
    BlockInput(FALSE);
    PostQuitMessage(0);
}

static bool IsCombo1Down() {
    bool ctrl = s_keys[VK_CONTROL] || s_keys[VK_LCONTROL] || s_keys[VK_RCONTROL];
    return ctrl && s_keys[VK_TAB] && s_keys['Z'];
}

static bool IsCombo2Down() {
    bool ctrl = s_keys[VK_CONTROL] || s_keys[VK_LCONTROL] || s_keys[VK_RCONTROL];
    bool alt  = s_keys[VK_MENU]   || s_keys[VK_LMENU]    || s_keys[VK_RMENU];
    return ctrl && alt && s_keys['X'];
}

static void UpdateCombo() {
    switch (s_comboState) {
    case 0:
        if (IsCombo1Down()) { s_comboState = 1; s_comboTime = GetTickCount(); }
        break;
    case 1:
        if (GetTickCount() - s_comboTime > COMBO_TIMEOUT) { s_comboState = 0; break; }
        if (!IsCombo1Down() && IsCombo2Down()) { s_comboState = 2; DoExit(); }
        break;
    }
}

LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode >= 0) {
        auto* kbd = (KBDLLHOOKSTRUCT*)lParam;
        DWORD vk  = kbd->vkCode;
        if (wParam == WM_KEYDOWN   || wParam == WM_SYSKEYDOWN) { if (vk < 256) s_keys[vk] = true; }
        else if (wParam == WM_KEYUP || wParam == WM_SYSKEYUP)  { if (vk < 256) s_keys[vk] = false; }
        UpdateCombo();
        return 1; // bloqueia TUDO (exceto Ctrl+Alt+Del — nível kernel)
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    return (nCode >= 0) ? 1 : CallNextHookEx(nullptr, nCode, wParam, lParam);
}

void InstallHooks() {
    memset(s_keys, 0, sizeof(s_keys));
    s_comboState = 0;
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
