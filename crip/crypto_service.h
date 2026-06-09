#pragma once
#include <windows.h>
#include <vector>
#include <string>

class CryptoService {
public:
    static std::vector<BYTE> Sha256(const std::vector<BYTE>& data);
    static std::vector<BYTE> Sha256(const std::string& str);
    static std::vector<BYTE> GenerateRandomBytes(size_t size);
    static std::string Base64Encode(const std::vector<BYTE>& data);
    static std::vector<BYTE> Base64Decode(const std::string& b64);
    static bool GenerateRsaKeyPair(std::string& pubKeyB64, std::string& privKeyB64);
    static std::vector<BYTE> RsaEncrypt(const std::string& pubKeyB64, const std::vector<BYTE>& plaintext);
    static std::vector<BYTE> RsaDecrypt(const std::string& privKeyB64, const std::vector<BYTE>& ciphertext);
    static std::vector<BYTE> AesEncrypt(const std::vector<BYTE>& key, const std::vector<BYTE>& iv, const std::vector<BYTE>& plaintext);
    static std::vector<BYTE> AesDecrypt(const std::vector<BYTE>& key, const std::vector<BYTE>& iv, const std::vector<BYTE>& ciphertext);
    static std::vector<BYTE> DeriveKeyFromPassword(const std::string& password);
};
