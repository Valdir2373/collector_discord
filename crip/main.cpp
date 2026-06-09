#include "crypto_service.h"
#include "file_service.h"
#include "disk_service.h"
#include "worker_pool.h"
#include <iostream>
#include <fstream>
#include <algorithm>
#include <string>
#include <chrono>

// Global instantiation of self-path declared in file_service.h
fs::path g_selfExePath;

// Helper to get executable file path
fs::path GetCurrentExecutablePath() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    return fs::path(path);
}

#ifdef BUILD_UNLOCKER

void PrintUsage() {
    std::cout << "=========================================================\n";
    std::cout << "             2373 Personal Unlocker                      \n";
    std::cout << "=========================================================\n\n";
    std::cout << "Usage:\n";
    std::cout << "  Unlock Files/Folders/Drives:\n";
    std::cout << "    unlocker.exe <path|ALL> [--key <priv_key_file>] [--pass <password>]\n\n";
    std::cout << "Options:\n";
    std::cout << "  ALL            Auto-scans and unlocks all connected drives (fixed & USB)\n";
    std::cout << "  --key <file>   Use asymmetric private key (text-based Base64 key)\n";
    std::cout << "  --pass <str>   Use password/passphrase instead of key files (pure text)\n\n";
}

#else

void PrintUsage() {
    std::cout << "=========================================================\n";
    std::cout << "             2373 Personal Locker                        \n";
    std::cout << "=========================================================\n\n";
    std::cout << "Usage:\n";
    std::cout << "  Generate Keys:\n";
    std::cout << "    locker.exe genkey <key_prefix>\n\n";
    std::cout << "  Lock Files/Folders/Drives:\n";
    std::cout << "    locker.exe lock <path|ALL> [--key <pub_key_file>] [--pass <password>] [--mode <fast|full>]\n\n";
    std::cout << "Options:\n";
    std::cout << "  ALL            Auto-scans and locks all connected drives (fixed & USB)\n";
    std::cout << "  --key <file>   Use asymmetric public key (text-based Base64 key)\n";
    std::cout << "  --pass <str>   Use password/passphrase instead of key files (pure text)\n";
    std::cout << "  --mode <type>  Lock mode: 'fast' (encrypts 1KB/64KB in-place + ADS header, default) or 'full'\n\n";
}

#endif

std::string ReadTextFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return "";
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

int main(int argc, char* argv[]) {
    g_selfExePath = GetCurrentExecutablePath();

#ifdef BUILD_UNLOCKER
    if (argc < 2) {
        PrintUsage();
        return 1;
    }
    std::string target = argv[1];
    int optionStartIdx = 2;
#else
    if (argc < 3) {
        PrintUsage();
        return 1;
    }
    std::string command = argv[1];
    std::string target = argv[2];
    int optionStartIdx = 3;

    if (command == "genkey") {
        std::string pubKey, privKey;
        std::cout << "[*] Generating RSA-2048 key pair..." << std::endl;
        if (CryptoService::GenerateRsaKeyPair(pubKey, privKey)) {
            std::string pubFile = target + ".pub";
            std::string privFile = target + ".priv";
            
            std::ofstream pubOut(pubFile);
            pubOut << pubKey;
            pubOut.close();

            std::ofstream privOut(privFile);
            privOut << privKey;
            privOut.close();

            std::cout << "[+] Keys successfully generated:\n";
            std::cout << "    Public Key (Text/Base64):  " << pubFile << "\n";
            std::cout << "    Private Key (Text/Base64): " << privFile << "\n";
            return 0;
        } else {
            std::cerr << "[-] Key generation failed!" << std::endl;
            return 1;
        }
    }

    if (command != "lock") {
        PrintUsage();
        return 1;
    }
#endif

    std::string keyFile = "";
    std::string password = "";
    bool fullMode = false;

    for (int i = optionStartIdx; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--key" && i + 1 < argc) {
            keyFile = argv[++i];
        } else if (arg == "--pass" && i + 1 < argc) {
            password = argv[++i];
        } else if (arg == "--mode" && i + 1 < argc) {
            std::string modeVal = argv[++i];
            if (modeVal == "full") {
                fullMode = true;
            }
        }
    }

    if (keyFile.empty() && password.empty()) {
        std::cerr << "[-] Error: You must specify either a key file (--key) or a password (--pass)." << std::endl;
        return 1;
    }

    std::string keyContent = "";
    if (!keyFile.empty()) {
        keyContent = ReadTextFile(keyFile);
        if (keyContent.empty()) {
            std::cerr << "[-] Error: Cannot read key file or key is empty: " << keyFile << std::endl;
            return 1;
        }
        keyContent.erase(std::remove(keyContent.begin(), keyContent.end(), '\r'), keyContent.end());
        keyContent.erase(std::remove(keyContent.begin(), keyContent.end(), '\n'), keyContent.end());
    }

    std::string targetUpper = target;
    std::transform(targetUpper.begin(), targetUpper.end(), targetUpper.begin(), ::toupper);

    // Setup worker process lambda
    auto processFunc = [keyContent, password, fullMode](const fs::path& filePath) -> bool {
#ifdef BUILD_UNLOCKER
        return FileService::UnlockFile(filePath, keyContent, password);
#else
        return FileService::LockFile(filePath, keyContent, password, fullMode);
#endif
    };

    auto startTime = std::chrono::high_resolution_clock::now();
    size_t processedCount = 0;
    size_t skippedCount = 0;

    fs::path targetPath(target);
    bool isSingleFile = (targetUpper != "ALL" && fs::is_regular_file(targetPath));

    if (isSingleFile) {
        std::cout << "[*] Single file detected. Processing inline on main thread...\n";
        if (processFunc(targetPath)) {
            processedCount = 1;
        } else {
            skippedCount = 1;
        }
    } else {
        // Dynamic thread count based on hardware
        size_t numThreads = std::thread::hardware_concurrency();
        if (numThreads == 0) numThreads = 4;
        std::cout << "[*] Spawning " << numThreads << " worker threads (\"escravos\")...\n";

        // Instantiate worker pool
        WorkerPool pool(numThreads, processFunc);

        if (targetUpper == "ALL") {
            std::cout << "[*] Auto-scanning all connected writable drives..." << std::endl;
            std::vector<fs::path> drives = DiskService::GetAvailableDrives();
            std::cout << "[*] Found " << drives.size() << " active drive(s).\n";
            for (const auto& drive : drives) {
                std::cout << "[*] Scanning drive: " << drive.string() << std::endl;
                DiskService::ScanDirectory(drive, pool);
            }
        } else {
            std::cout << "[*] Scanning path: " << targetPath.string() << std::endl;
            DiskService::ScanDirectory(targetPath, pool);
        }

        // Finished scanning, tell pool to shut down once queue is drained
        std::cout << "[*] Scan complete. Draining task queue and processing remaining files...\n";
        pool.wait_and_shutdown();
        
        processedCount = pool.get_processed_count();
        skippedCount = pool.get_skipped_count();
    }

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;

    std::cout << "\n=========================================================\n";
    std::cout << "[*] Execution complete in " << elapsed.count() << " seconds.\n";
    std::cout << "    Processed: " << processedCount << " files\n";
    std::cout << "    Skipped:   " << skippedCount << " files\n";
    std::cout << "=========================================================\n";

    return 0;
}
