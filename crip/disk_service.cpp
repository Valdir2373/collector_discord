#include "disk_service.h"
#include <windows.h>
#include <cstring>
#include <iostream>

std::vector<fs::path> DiskService::GetAvailableDrives() {
    std::vector<fs::path> drives;
    char driveBuffer[512];
    DWORD size = GetLogicalDriveStringsA(sizeof(driveBuffer), driveBuffer);
    if (size > 0 && size < sizeof(driveBuffer)) {
        char* drive = driveBuffer;
        while (*drive) {
            UINT driveType = GetDriveTypeA(drive);
            // Scan fixed drives (HDDs/SSDs) and removable drives (USBs)
            if (driveType == DRIVE_FIXED || driveType == DRIVE_REMOVABLE) {
                drives.push_back(fs::path(drive));
            }
            drive += strlen(drive) + 1;
        }
    }
    return drives;
}

void DiskService::ScanDirectory(const fs::path& dirPath, WorkerPool& pool) {
    if (!fs::exists(dirPath)) {
        return;
    }

    try {
        if (fs::is_regular_file(dirPath)) {
            pool.add_job(dirPath);
        }
        else if (fs::is_directory(dirPath)) {
            for (auto const& entry : fs::recursive_directory_iterator(dirPath, fs::directory_options::skip_permission_denied)) {
                try {
                    if (fs::is_regular_file(entry.path())) {
                        pool.add_job(entry.path());
                    }
                }
                catch (...) {
                    // Ignore individual path access exceptions
                }
            }
        }
    }
    catch (const std::exception& e) {
        std::cerr << "[-] Exception scanning: " << dirPath.string() << " - " << e.what() << std::endl;
    }
}
