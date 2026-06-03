#pragma once
#include "pch.h"

LRESULT CALLBACK MeltWndProc(HWND hWnd, UINT msg, WPARAM wp, LPARAM lp);

// Captura tela, abre janela fullscreen e roda drip+glitch até Insert ser pressionado
void RunMelt(HINSTANCE hInst);
