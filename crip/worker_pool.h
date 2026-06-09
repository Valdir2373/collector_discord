#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <filesystem>
#include <vector>
#include <thread>
#include <functional>
#include <atomic>

namespace fs = std::filesystem;

class SafeQueue {
private:
    std::queue<fs::path> queue;
    std::mutex mutex;
    std::condition_variable cv;
    bool finished = false;

public:
    void push(const fs::path& path);
    bool pop(fs::path& path);
    void set_finished();
    bool is_empty();
};

class WorkerPool {
private:
    SafeQueue queue;
    std::vector<std::thread> workers;
    std::atomic<size_t> processed_count{0};
    std::atomic<size_t> skipped_count{0};

public:
    WorkerPool(size_t num_threads, const std::function<bool(const fs::path&)>& process_func);
    ~WorkerPool();
    void add_job(const fs::path& path);
    void wait_and_shutdown();
    size_t get_processed_count() const { return processed_count; }
    size_t get_skipped_count() const { return skipped_count; }
};
