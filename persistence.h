#pragma once
#include "pch.h"

// Método 1: HKCU\Winlogon\Shell (inicia junto com Explorer — imediato)
// Método 2: HKCU\Run + StartupDelayInMSec=0 (backup)
void RegisterPersistence(const wchar_t* exePath);
void UnregisterPersistence();
