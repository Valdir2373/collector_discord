// melt_screen.cpp
// Compilar: cl /EHsc /O2 melt_screen.cpp user32.lib gdi32.lib shcore.lib

#include <windows.h>
#include <shellscalingapi.h>
#pragma comment(lib, "shcore.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")

#include <random>
#include <chrono>
#include <vector>

// ── Globais ───────────────────────────────────────────────────────────────────
static int   SW, SH;
static bool  g_running = true;
static std::mt19937 g_rng;

static HHOOK g_hKeyHook   = nullptr;
static HHOOK g_hMouseHook = nullptr;

// ── Hook de teclado: bloqueia TUDO exceto Insert ──────────────────────────────
LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0) {
        KBDLLHOOKSTRUCT* kbd = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        // Insert keydown → encerra o programa
        if (kbd->vkCode == VK_INSERT && wParam == WM_KEYDOWN) {
            g_running = false;
            PostQuitMessage(0);
            return 1; // consome a tecla mesmo assim
        }

        // Bloqueia TUDO o mais (Windows, Alt+Tab, Ctrl+Alt+Del não pode ser
        // bloqueado por hook, mas todo o resto sim)
        return 1;
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ── Hook de mouse: bloqueia movimento e cliques ───────────────────────────────
LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode >= 0) {
        return 1; // engole qualquer evento de mouse
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// ── WindowProc (secundário — hooks tratam input antes disso) ──────────────────
LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
        case WM_CLOSE:      return 0;
        case WM_SYSCOMMAND:
            if ((wp & 0xFFF0) == SC_CLOSE) return 0;
            break;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            BeginPaint(hWnd, &ps);
            EndPaint(hWnd, &ps);
            return 0;
        }
        case WM_DESTROY:
            PostQuitMessage(0);
            break;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

// ── WinMain ───────────────────────────────────────────────────────────────────
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int)
{
    SetProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);

    SW = GetSystemMetrics(SM_CXSCREEN);
    SH = GetSystemMetrics(SM_CYSCREEN);

    g_rng.seed(static_cast<unsigned>(
        std::chrono::high_resolution_clock::now().time_since_epoch().count()));

    // ── DIB Section ──────────────────────────────────────────────────────────
    BITMAPINFO bmi          = {};
    bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth       = SW;
    bmi.bmiHeader.biHeight      = -SH;  // top-down
    bmi.bmiHeader.biPlanes      = 1;
    bmi.bmiHeader.biBitCount    = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void*   pPixels = nullptr;
    HDC     hDIBDC  = CreateCompatibleDC(nullptr);
    HBITMAP hDIB    = CreateDIBSection(hDIBDC, &bmi, DIB_RGB_COLORS, &pPixels, nullptr, 0);
    SelectObject(hDIBDC, hDIB);

    // ── Captura a tela ANTES de criar a janela ────────────────────────────────
    HDC hScreenDC = GetDC(nullptr);
    BitBlt(hDIBDC, 0, 0, SW, SH, hScreenDC, 0, 0, SRCCOPY);
    ReleaseDC(nullptr, hScreenDC);

    // ── Instala os hooks ANTES de mostrar qualquer coisa ─────────────────────
    // Thread 0 = global (todos os processos)
    g_hKeyHook   = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKeyboardProc, nullptr, 0);
    g_hMouseHook = SetWindowsHookExW(WH_MOUSE_LL,    LowLevelMouseProc,    nullptr, 0);

    // ── Janela fullscreen sem borda, sem taskbar ──────────────────────────────
    const wchar_t CLS[] = L"MeltCls";
    WNDCLASSW wc    = {};
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = CLS;
    wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
    wc.hCursor       = nullptr;
    RegisterClassW(&wc);

    HWND hWnd = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        CLS, L"",
        WS_POPUP,
        0, 0, SW, SH,
        nullptr, nullptr, hInst, nullptr);

    // Remove botão fechar do sistema
    HMENU hSys = GetSystemMenu(hWnd, FALSE);
    if (hSys) RemoveMenu(hSys, SC_CLOSE, MF_BYCOMMAND);

    ShowWindow(hWnd, SW_SHOW);
    SetForegroundWindow(hWnd);
    SetFocus(hWnd);

    // Esconde o cursor do mouse completamente
    ShowCursor(FALSE);

    // ── Configuração das colunas de derretimento ──────────────────────────────
    struct Column {
        int   x;
        int   width;
        int   dropped;
        int   speed;
        float chance;
    };

    std::vector<Column> cols;
    {
        std::uniform_int_distribution<int>   widthDist(1, 4);
        std::uniform_int_distribution<int>   speedDist(1, 4);
        std::uniform_real_distribution<float> chanceDist(0.02f, 0.18f);

        int x = 0;
        while (x < SW) {
            Column c;
            c.x       = x;
            c.width   = widthDist(g_rng);
            if (x + c.width > SW) c.width = SW - x;
            c.dropped = 0;
            c.speed   = speedDist(g_rng);
            c.chance  = chanceDist(g_rng);
            cols.push_back(c);
            x += c.width;
        }
    }

    DWORD* pixels = static_cast<DWORD*>(pPixels);
    std::uniform_real_distribution<float> rollDist(0.0f, 1.0f);

    // ── Loop principal ────────────────────────────────────────────────────────
    MSG msg = {};
    while (g_running) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { g_running = false; break; }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        if (!g_running) break;

        // Derrete colunas
        for (auto& c : cols) {
            if (c.dropped >= SH) continue;
            if (rollDist(g_rng) > c.chance) continue;

            int shift = c.speed;
            if (c.dropped + shift > SH) shift = SH - c.dropped;

            for (int col = c.x; col < c.x + c.width; ++col) {
                for (int y = SH - 1; y >= shift; --y)
                    pixels[y * SW + col] = pixels[(y - shift) * SW + col];
                for (int y = 0; y < shift; ++y)
                    pixels[y * SW + col] = 0xFF000000;
            }
            c.dropped += shift;
        }

        // Blit
        HDC hWndDC = GetDC(hWnd);
        BitBlt(hWndDC, 0, 0, SW, SH, hDIBDC, 0, 0, SRCCOPY);
        ReleaseDC(hWnd, hWndDC);

        SetWindowPos(hWnd, HWND_TOPMOST, 0,0,0,0,
                     SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE);

        Sleep(16);
    }

    // ── Limpeza ───────────────────────────────────────────────────────────────
    if (g_hKeyHook)   UnhookWindowsHookEx(g_hKeyHook);
    if (g_hMouseHook) UnhookWindowsHookEx(g_hMouseHook);
    ShowCursor(TRUE);  // restaura o cursor

    DeleteObject(hDIB);
    DeleteDC(hDIBDC);
    DestroyWindow(hWnd);
    UnregisterClassW(CLS, hInst);
    return 0;
}