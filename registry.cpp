#include "registry.h"

void SaveWstr(const wchar_t* sub, const wchar_t* val, const wchar_t* data) {
    HKEY k;
    RegCreateKeyExW(HKEY_CURRENT_USER, sub, 0, nullptr, 0,
                    KEY_SET_VALUE, nullptr, &k, nullptr);
    RegSetValueExW(k, val, 0, REG_SZ, (BYTE*)data,
                   (DWORD)((wcslen(data) + 1) * sizeof(wchar_t)));
    RegCloseKey(k);
}

bool LoadWstr(const wchar_t* sub, const wchar_t* val, wchar_t* out, DWORD bytes) {
    HKEY k;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, sub, 0, KEY_READ, &k)) return false;
    bool ok = !RegQueryValueExW(k, val, nullptr, nullptr, (BYTE*)out, &bytes);
    RegCloseKey(k); return ok;
}

void SaveOriginalWallpaper() {
    wchar_t buf[MAX_PATH] = {};
    SystemParametersInfoW(SPI_GETDESKWALLPAPER, MAX_PATH, buf, 0);
    SaveWstr(L"Software\\TrollTerror", L"OrigWall", buf);
}

void RestoreOriginalWallpaper() {
    wchar_t buf[MAX_PATH] = {};
    if (LoadWstr(L"Software\\TrollTerror", L"OrigWall", buf, sizeof(buf)) && buf[0])
        SystemParametersInfoW(SPI_SETDESKWALLPAPER, 0, buf,
                              SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
}
