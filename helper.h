#pragma once
#include "pch.h"

void RunHidden(const wchar_t* cmdline, bool wait = false);
bool IsStartedFromBat(LPSTR cmd);
void MakeRandomName(wchar_t* out, int len);
void ShowDesktop();
