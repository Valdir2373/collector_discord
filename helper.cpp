#include "helper.h"

void RunHidden(const wchar_t* cmdline, bool wait) {
    STARTUPINFOW si = {sizeof(si)};
    si.dwFlags = STARTF_USESHOWWINDOW; si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    wchar_t buf[1024]; wcscpy_s(buf, cmdline);
    if (CreateProcessW(nullptr, buf, nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        if (wait) WaitForSingleObject(pi.hProcess, 10000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread);
    }
}

bool IsStartedFromBat(LPSTR cmd) {
    return cmd && strstr(cmd, "/bat") != nullptr;
}

void MakeRandomName(wchar_t* out, int len) {
    SYSTEMTIME st; GetSystemTime(&st);
    DWORD seed = st.wMilliseconds ^ (GetCurrentProcessId() << 4)
               ^ (st.wSecond << 8) ^ (st.wMinute << 16);
    swprintf_s(out, len, L"%08X", seed ^ GetTickCount());
}

static BOOL CALLBACK MinimizeAllProc(HWND hwnd, LPARAM) {
    if (!IsWindowVisible(hwnd) || IsIconic(hwnd)) return TRUE;
    wchar_t cls[64] = {}; GetClassNameW(hwnd, cls, 64);
    if (!wcscmp(cls, L"Shell_TrayWnd") || !wcscmp(cls, L"Progman")
        || !wcscmp(cls, L"WorkerW")) return TRUE;
    ShowWindow(hwnd, SW_MINIMIZE); return TRUE;
}

void ShowDesktop() {
    HWND hTray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (hTray) SendMessageW(hTray, WM_COMMAND, (WPARAM)0x7402, 0);
    EnumWindows(MinimizeAllProc, 0); Sleep(400);
}
