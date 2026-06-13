#include "melt.h"
#include "globals.h"
#include "input_lock.h"

LRESULT CALLBACK MeltWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    if(msg==WM_CLOSE||(msg==WM_SYSCOMMAND&&(wp&0xFFF0)==SC_CLOSE)) return 0;
    if(msg==WM_PAINT){PAINTSTRUCT ps;BeginPaint(hWnd,&ps);EndPaint(hWnd,&ps);return 0;}
    if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(hWnd,msg,wp,lp);
}

// ─── EROSÃO: preto SOBE por baixo de cada coluna em ritmo diferente ───────────
// O conteúdo original FICA NO LUGAR — apenas o fundo preto avança de baixo pra cima
// Colunas com velocidade diferente = perfil dentado de "montanhas" na fronteira
struct MeltCol { int x, w; float erosion, speed; };

static void InitMeltCols(std::vector<MeltCol>& cols) {
    std::uniform_int_distribution<int>    wd(1, 5);
    std::uniform_real_distribution<float> spd(0.3f, 6.0f);
    for(int x = 0; x < SW; ) {
        MeltCol c;
        c.x       = x;
        c.w       = wd(g_rng);
        if(x + c.w > SW) c.w = SW - x;
        c.erosion = 0.0f;
        c.speed   = spd(g_rng); // velocidade aleatória por coluna
        cols.push_back(c);
        x += c.w;
    }
}

// Copia original para dst; depois preenche a parte de baixo de cada coluna com preto
static void ApplyBottomErosion(const DWORD* orig, DWORD* dst,
                                std::vector<MeltCol>& cols) {
    memcpy(dst, orig, (size_t)SW * SH * sizeof(DWORD));

    for(auto& c : cols) {
        c.erosion += c.speed;
        int eroded = (int)c.erosion;
        if(eroded > SH) eroded = SH;

        // Preto sobe de baixo — cria as "montanhas" na borda superior do preto
        for(int col = c.x; col < c.x + c.w; col++)
            for(int y = SH - eroded; y < SH; y++)
                dst[y * SW + col] = 0xFF000000;
    }
}


void RunMelt(HINSTANCE hInst) {
    BITMAPINFO bmi = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = SW;
    bmi.bmiHeader.biHeight      = -SH;
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    // orig = screenshot capturado — nunca modificado
    void*   origPixels = nullptr;
    HDC     hOrigDC    = CreateCompatibleDC(nullptr);
    HBITMAP hOrigBmp   = CreateDIBSection(hOrigDC,&bmi,DIB_RGB_COLORS,&origPixels,nullptr,0);
    SelectObject(hOrigDC,hOrigBmp);
    HDC hScrDC = GetDC(nullptr);
    BitBlt(hOrigDC,0,0,SW,SH,hScrDC,0,0,SRCCOPY);
    ReleaseDC(nullptr,hScrDC);

    // erosion = orig + preto subindo
    void*   erPixels = nullptr;
    HDC     hErDC    = CreateCompatibleDC(nullptr);
    HBITMAP hErBmp   = CreateDIBSection(hErDC,&bmi,DIB_RGB_COLORS,&erPixels,nullptr,0);
    SelectObject(hErDC,hErBmp);


    // Janela fullscreen topmost
    const wchar_t MELT_CLS[] = L"MeltCls";
    WNDCLASSW wc = {};
    wc.lpfnWndProc=MeltWndProc; wc.hInstance=hInst;
    wc.lpszClassName=MELT_CLS; wc.hbrBackground=(HBRUSH)GetStockObject(BLACK_BRUSH);
    RegisterClassW(&wc);
    g_hMeltWnd = CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW,
        MELT_CLS,L"",WS_POPUP,0,0,SW,SH,nullptr,nullptr,hInst,nullptr);
    ShowWindow(g_hMeltWnd,SW_SHOW);
    SetWindowPos(g_hMeltWnd,HWND_TOPMOST,0,0,SW,SH,SWP_SHOWWINDOW);
    SetForegroundWindow(g_hMeltWnd);
    LockInputFull();

    std::vector<MeltCol> cols;
    InitMeltCols(cols);

    const DWORD* orig = (const DWORD*)origPixels;
    DWORD*       er   = (DWORD*)erPixels;

    MSG msg = {};
    while(g_running) {
        while(PeekMessageW(&msg,nullptr,0,0,PM_REMOVE)) {
            if(msg.message==WM_QUIT){g_running=false;break;}
            TranslateMessage(&msg); DispatchMessageW(&msg);
        }
        if(!g_running) break;

        // Preto sobe por baixo de cada coluna → perfil de montanhas (sem glitch)
        ApplyBottomErosion(orig, er, cols);

        // Exibe direto — sem glitch
        HDC hWDC = GetDC(g_hMeltWnd);
        BitBlt(hWDC,0,0,SW,SH,hErDC,0,0,SRCCOPY);
        ReleaseDC(g_hMeltWnd,hWDC);
        // Melt NÃO reasserta TOPMOST — deixa o QR popup sempre acima
        Sleep(32);
    }

    DeleteObject(hErBmp);  DeleteDC(hErDC);
    DeleteObject(hOrigBmp); DeleteDC(hOrigDC);
    DestroyWindow(g_hMeltWnd);
    UnregisterClassW(MELT_CLS,hInst);
}
