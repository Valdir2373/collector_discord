#pragma once
#include "pch.h"

LRESULT CALLBACK PopupWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);
void PopupFloodThread();  // rodar em std::thread — gera flood de popups acumulados
