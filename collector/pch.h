#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#include <winhttp.h>
#include <shlobj.h>
#include <wincrypt.h>

#include <string>
#include <vector>
#include <set>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <regex>
#include <iostream>
#include <cstdint>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "crypt32.lib")
