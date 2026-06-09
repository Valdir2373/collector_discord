#include "crypto_service.h"
#include <algorithm>
#include <bcrypt.h>
#include <wincrypt.h>

std::vector<BYTE> CryptoService::Sha256(const std::vector<BYTE> &data) {
  static BCRYPT_ALG_HANDLE hAlg = []() {
    BCRYPT_ALG_HANDLE h = NULL;
    BCryptOpenAlgorithmProvider(&h, BCRYPT_SHA256_ALGORITHM, NULL, 0);
    return h;
  }();
  if (!hAlg)
    return {};

  BCRYPT_HASH_HANDLE hHash = NULL;
  DWORD cbHashObject = 0, cbHash = 0, cbData = sizeof(DWORD);
  BCryptGetProperty(hAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&cbHashObject, cbData,
                    &cbData, 0);
  BCryptGetProperty(hAlg, BCRYPT_HASH_LENGTH, (PBYTE)&cbHash, cbData, &cbData,
                    0);

  std::vector<BYTE> hashObject(cbHashObject);
  std::vector<BYTE> hash(cbHash);

  NTSTATUS status = BCryptCreateHash(hAlg, &hHash, hashObject.data(),
                                     hashObject.size(), NULL, 0, 0);
  if (status == 0) {
    BCryptHashData(hHash, (PBYTE)data.data(), data.size(), 0);
    BCryptFinishHash(hHash, hash.data(), hash.size(), 0);
    BCryptDestroyHash(hHash);
  }
  return hash;
}

std::vector<BYTE> CryptoService::Sha256(const std::string &str) {
  std::vector<BYTE> data(str.begin(), str.end());
  return Sha256(data);
}

std::vector<BYTE> CryptoService::GenerateRandomBytes(size_t size) {
  std::vector<BYTE> buf(size);
  static BCRYPT_ALG_HANDLE hAlg = []() {
    BCRYPT_ALG_HANDLE h = NULL;
    BCryptOpenAlgorithmProvider(&h, BCRYPT_RNG_ALGORITHM, NULL, 0);
    return h;
  }();
  if (hAlg) {
    BCryptGenRandom(hAlg, buf.data(), buf.size(), 0);
  }
  return buf;
}

std::string CryptoService::Base64Encode(const std::vector<BYTE> &data) {
  DWORD needed = 0;
  if (!CryptBinaryToStringA(data.data(), data.size(),
                            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, NULL,
                            &needed)) {
    return "";
  }
  std::vector<char> buf(needed);
  if (!CryptBinaryToStringA(data.data(), data.size(),
                            CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF,
                            buf.data(), &needed)) {
    return "";
  }
  return std::string(buf.data(), needed);
}

std::vector<BYTE> CryptoService::Base64Decode(const std::string &b64) {
  DWORD needed = 0;
  if (!CryptStringToBinaryA(b64.c_str(), b64.length(), CRYPT_STRING_BASE64,
                            NULL, &needed, NULL, NULL)) {
    return {};
  }
  std::vector<BYTE> buf(needed);
  if (!CryptStringToBinaryA(b64.c_str(), b64.length(), CRYPT_STRING_BASE64,
                            buf.data(), &needed, NULL, NULL)) {
    return {};
  }
  return buf;
}

bool CryptoService::GenerateRsaKeyPair(std::string &pubKeyB64,
                                       std::string &privKeyB64) {
  static BCRYPT_ALG_HANDLE hRsaAlg = []() {
    BCRYPT_ALG_HANDLE h = NULL;
    BCryptOpenAlgorithmProvider(&h, BCRYPT_RSA_ALGORITHM, NULL, 0);
    return h;
  }();
  if (!hRsaAlg)
    return false;

  BCRYPT_KEY_HANDLE hKey = NULL;
  NTSTATUS status = BCryptGenerateKeyPair(hRsaAlg, &hKey, 2048, 0);
  if (status != 0)
    return false;

  status = BCryptFinalizeKeyPair(hKey, 0);
  if (status != 0) {
    BCryptDestroyKey(hKey);
    return false;
  }

  DWORD pubSize = 0;
  BCryptExportKey(hKey, NULL, BCRYPT_RSAPUBLIC_BLOB, NULL, 0, &pubSize, 0);
  std::vector<BYTE> pubBlob(pubSize);
  BCryptExportKey(hKey, NULL, BCRYPT_RSAPUBLIC_BLOB, pubBlob.data(),
                  pubBlob.size(), &pubSize, 0);
  pubKeyB64 = Base64Encode(pubBlob);

  DWORD privSize = 0;
  BCryptExportKey(hKey, NULL, BCRYPT_RSAPRIVATE_BLOB, NULL, 0, &privSize, 0);
  std::vector<BYTE> privBlob(privSize);
  BCryptExportKey(hKey, NULL, BCRYPT_RSAPRIVATE_BLOB, privBlob.data(),
                  privBlob.size(), &privSize, 0);
  privKeyB64 = Base64Encode(privBlob);

  BCryptDestroyKey(hKey);
  return true;
}

std::vector<BYTE>
CryptoService::RsaEncrypt(const std::string &pubKeyB64,
                          const std::vector<BYTE> &plaintext) {
  static BCRYPT_ALG_HANDLE hRsaAlg = []() {
    BCRYPT_ALG_HANDLE h = NULL;
    BCryptOpenAlgorithmProvider(&h, BCRYPT_RSA_ALGORITHM, NULL, 0);
    return h;
  }();
  if (!hRsaAlg)
    return {};

  BCRYPT_KEY_HANDLE hKey = NULL;
  std::vector<BYTE> pubBlob = Base64Decode(pubKeyB64);
  NTSTATUS status =
      BCryptImportKeyPair(hRsaAlg, NULL, BCRYPT_RSAPUBLIC_BLOB, &hKey,
                          pubBlob.data(), pubBlob.size(), 0);
  if (status != 0)
    return {};

  DWORD encSize = 0;
  status = BCryptEncrypt(hKey, (PUCHAR)plaintext.data(), plaintext.size(), NULL,
                         NULL, 0, NULL, 0, &encSize, BCRYPT_PAD_PKCS1);
  if (status != 0) {
    BCryptDestroyKey(hKey);
    return {};
  }

  std::vector<BYTE> ciphertext(encSize);
  status = BCryptEncrypt(hKey, (PUCHAR)plaintext.data(), plaintext.size(), NULL,
                         NULL, 0, ciphertext.data(), ciphertext.size(),
                         &encSize, BCRYPT_PAD_PKCS1);

  BCryptDestroyKey(hKey);

  if (status != 0)
    return {};
  return ciphertext;
}

std::vector<BYTE>
CryptoService::RsaDecrypt(const std::string &privKeyB64,
                          const std::vector<BYTE> &ciphertext) {
  static BCRYPT_ALG_HANDLE hRsaAlg = []() {
    BCRYPT_ALG_HANDLE h = NULL;
    BCryptOpenAlgorithmProvider(&h, BCRYPT_RSA_ALGORITHM, NULL, 0);
    return h;
  }();
  if (!hRsaAlg)
    return {};

  BCRYPT_KEY_HANDLE hKey = NULL;
  std::vector<BYTE> privBlob = Base64Decode(privKeyB64);
  NTSTATUS status =
      BCryptImportKeyPair(hRsaAlg, NULL, BCRYPT_RSAPRIVATE_BLOB, &hKey,
                          privBlob.data(), privBlob.size(), 0);
  if (status != 0)
    return {};

  DWORD decSize = 0;
  status = BCryptDecrypt(hKey, (PUCHAR)ciphertext.data(), ciphertext.size(),
                         NULL, NULL, 0, NULL, 0, &decSize, BCRYPT_PAD_PKCS1);
  if (status != 0) {
    BCryptDestroyKey(hKey);
    return {};
  }

  std::vector<BYTE> decrypted(decSize);
  status = BCryptDecrypt(hKey, (PUCHAR)ciphertext.data(), ciphertext.size(),
                         NULL, NULL, 0, decrypted.data(), decrypted.size(),
                         &decSize, BCRYPT_PAD_PKCS1);

  BCryptDestroyKey(hKey);

  if (status != 0)
    return {};
  decrypted.resize(decSize);
  return decrypted;
}

std::vector<BYTE>
CryptoService::AesEncrypt(const std::vector<BYTE> &key,
                          const std::vector<BYTE> &iv,
                          const std::vector<BYTE> &plaintext) {
  static BCRYPT_ALG_HANDLE hAesAlg = []() {
    BCRYPT_ALG_HANDLE h = NULL;
    NTSTATUS status =
        BCryptOpenAlgorithmProvider(&h, BCRYPT_AES_ALGORITHM, NULL, 0);
    if (status == 0) {
      BCryptSetProperty(h, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                        sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    }
    return h;
  }();
  if (!hAesAlg)
    return {};

  BCRYPT_KEY_HANDLE hKey = NULL;
  DWORD keyObjSize = 0, cbData = sizeof(DWORD);
  BCryptGetProperty(hAesAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&keyObjSize, cbData,
                    &cbData, 0);
  std::vector<BYTE> keyObject(keyObjSize);

  NTSTATUS status = BCryptGenerateSymmetricKey(
      hAesAlg, &hKey, keyObject.data(), keyObject.size(), (PUCHAR)key.data(),
      key.size(), 0);
  if (status != 0)
    return {};

  std::vector<BYTE> ivCopy = iv;
  DWORD encSize = 0;
  status = BCryptEncrypt(hKey, (PUCHAR)plaintext.data(), plaintext.size(), NULL,
                         ivCopy.data(), ivCopy.size(), NULL, 0, &encSize,
                         BCRYPT_BLOCK_PADDING);
  if (status != 0) {
    BCryptDestroyKey(hKey);
    return {};
  }

  std::vector<BYTE> ciphertext(encSize);
  ivCopy = iv;
  status = BCryptEncrypt(hKey, (PUCHAR)plaintext.data(), plaintext.size(), NULL,
                         ivCopy.data(), ivCopy.size(), ciphertext.data(),
                         ciphertext.size(), &encSize, BCRYPT_BLOCK_PADDING);

  BCryptDestroyKey(hKey);

  if (status != 0)
    return {};
  return ciphertext;
}

std::vector<BYTE>
CryptoService::AesDecrypt(const std::vector<BYTE> &key,
                          const std::vector<BYTE> &iv,
                          const std::vector<BYTE> &ciphertext) {
  static BCRYPT_ALG_HANDLE hAesAlg = []() {
    BCRYPT_ALG_HANDLE h = NULL;
    NTSTATUS status =
        BCryptOpenAlgorithmProvider(&h, BCRYPT_AES_ALGORITHM, NULL, 0);
    if (status == 0) {
      BCryptSetProperty(h, BCRYPT_CHAINING_MODE, (PUCHAR)BCRYPT_CHAIN_MODE_CBC,
                        sizeof(BCRYPT_CHAIN_MODE_CBC), 0);
    }
    return h;
  }();
  if (!hAesAlg)
    return {};

  BCRYPT_KEY_HANDLE hKey = NULL;
  DWORD keyObjSize = 0, cbData = sizeof(DWORD);
  BCryptGetProperty(hAesAlg, BCRYPT_OBJECT_LENGTH, (PBYTE)&keyObjSize, cbData,
                    &cbData, 0);
  std::vector<BYTE> keyObject(keyObjSize);

  NTSTATUS status = BCryptGenerateSymmetricKey(
      hAesAlg, &hKey, keyObject.data(), keyObject.size(), (PUCHAR)key.data(),
      key.size(), 0);
  if (status != 0)
    return {};

  std::vector<BYTE> ivCopy = iv;
  DWORD decSize = 0;
  status = BCryptDecrypt(hKey, (PUCHAR)ciphertext.data(), ciphertext.size(),
                         NULL, ivCopy.data(), ivCopy.size(), NULL, 0, &decSize,
                         BCRYPT_BLOCK_PADDING);
  if (status != 0) {
    BCryptDestroyKey(hKey);
    return {};
  }

  std::vector<BYTE> decrypted(decSize);
  ivCopy = iv;
  status = BCryptDecrypt(hKey, (PUCHAR)ciphertext.data(), ciphertext.size(),
                         NULL, ivCopy.data(), ivCopy.size(), decrypted.data(),
                         decrypted.size(), &decSize, BCRYPT_BLOCK_PADDING);

  BCryptDestroyKey(hKey);

  if (status != 0)
    return {};
  decrypted.resize(decSize);
  return decrypted;
}

std::vector<BYTE>
CryptoService::DeriveKeyFromPassword(const std::string &password) {
  return Sha256(password + "Saltgm2373Key");
}
