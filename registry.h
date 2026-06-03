#pragma once
#include "pch.h"

void SaveWstr(const wchar_t* sub, const wchar_t* val, const wchar_t* data);
bool LoadWstr(const wchar_t* sub, const wchar_t* val, wchar_t* out, DWORD bytes);
void SaveOriginalWallpaper();
void RestoreOriginalWallpaper();
