#include "sysinfo.h"

// ── Helpers ───────────────────────────────────────────────────────────────────
static std::string wide_to_utf8(const wchar_t* ws) {
    if (!ws || !ws[0]) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, s.data(), len, nullptr, nullptr);
    return s;
}

// Extrai um campo de JSON simples (sem arrays aninhados)
static std::string json_field(const std::string& json, const std::string& key) {
    std::string search = "\"" + key + "\":";
    size_t pos = json.find(search);
    if (pos == std::string::npos) return "";
    pos += search.size();
    while (pos < json.size() && json[pos] == ' ') ++pos;
    if (pos >= json.size()) return "";
    if (json[pos] == '"') {
        ++pos;
        size_t end = json.find('"', pos);
        return (end == std::string::npos) ? "" : json.substr(pos, end - pos);
    }
    size_t end = json.find_first_of(",}", pos);
    return json.substr(pos, end - pos);
}

// ── Coleta ────────────────────────────────────────────────────────────────────
static std::string get_hostname() {
    wchar_t buf[MAX_COMPUTERNAME_LENGTH + 1] = {};
    DWORD len = _countof(buf);
    GetComputerNameW(buf, &len);
    return wide_to_utf8(buf);
}

static std::string get_username() {
    wchar_t buf[256] = {};
    DWORD len = _countof(buf);
    GetUserNameW(buf, &len);
    return wide_to_utf8(buf);
}

static std::string get_os_version() {
    // RtlGetVersion é mais confiável que GetVersionEx (não é bloqueado pelo manifest)
    typedef LONG(WINAPI* RtlGetVer)(PRTL_OSVERSIONINFOW);
    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    RTL_OSVERSIONINFOW vi = {};
    vi.dwOSVersionInfoSize = sizeof(vi);
    if (ntdll) {
        auto fn = (RtlGetVer)GetProcAddress(ntdll, "RtlGetVersion");
        if (fn) fn(&vi);
    }
    std::ostringstream oss;
    oss << "Windows " << vi.dwMajorVersion << "." << vi.dwMinorVersion
        << " (Build " << vi.dwBuildNumber << ")";
    return oss.str();
}

static std::string http_get(const wchar_t* host, const wchar_t* path) {
    std::string result;
    HINTERNET hSes = WinHttpOpen(L"Collector/1.0",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!hSes) return result;

    HINTERNET hCon = WinHttpConnect(hSes, host, INTERNET_DEFAULT_HTTP_PORT, 0);
    if (hCon) {
        HINTERNET hReq = WinHttpOpenRequest(hCon, L"GET", path,
            nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, 0);
        if (hReq) {
            if (WinHttpSendRequest(hReq, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
                WinHttpReceiveResponse(hReq, nullptr)) {
                DWORD avail = 0;
                while (WinHttpQueryDataAvailable(hReq, &avail) && avail > 0) {
                    std::string chunk(avail, '\0');
                    DWORD read = 0;
                    WinHttpReadData(hReq, chunk.data(), avail, &read);
                    result.append(chunk.data(), read);
                }
            }
            WinHttpCloseHandle(hReq);
        }
        WinHttpCloseHandle(hCon);
    }
    WinHttpCloseHandle(hSes);
    return result;
}

// ── API pública ───────────────────────────────────────────────────────────────
SystemInfo collect_sysinfo() {
    SystemInfo info;
    info.hostname   = get_hostname();
    info.username   = get_username();
    info.os_version = get_os_version();
    info.ip_raw     = http_get(L"ip-api.com", L"/json");
    return info;
}

std::string sysinfo_to_text(const SystemInfo& info) {
    std::ostringstream oss;
    oss << "=== Informacoes do Sistema ===\n";
    oss << "Hostname   : " << info.hostname   << "\n";
    oss << "Usuario    : " << info.username   << "\n";
    oss << "SO         : " << info.os_version << "\n\n";

    oss << "=== Rede / IP ===\n";
    if (!info.ip_raw.empty()) {
        const auto& j = info.ip_raw;
        oss << "IP Publico : " << json_field(j, "query")      << "\n";
        oss << "Pais       : " << json_field(j, "country")    << "\n";
        oss << "Regiao     : " << json_field(j, "regionName") << "\n";
        oss << "Cidade     : " << json_field(j, "city")       << "\n";
        oss << "ISP        : " << json_field(j, "isp")        << "\n";
        oss << "Timezone   : " << json_field(j, "timezone")   << "\n";
    } else {
        oss << "(nao foi possivel obter informacoes de IP)\n";
    }
    return oss.str();
}
