#pragma once
#include <vector>
#include <filesystem>
#include "worker_pool.h"

namespace fs = std::filesystem;

class DiskService {
public:
    static std::vector<fs::path> GetAvailableDrives();
    static void ScanDirectory(const fs::path& dirPath, WorkerPool& pool);
};
