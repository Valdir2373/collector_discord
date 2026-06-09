#include "worker_pool.h"

void SafeQueue::push(const fs::path& path) {
    {
        std::lock_guard<std::mutex> lock(mutex);
        queue.push(path);
    }
    cv.notify_one();
}

bool SafeQueue::pop(fs::path& path) {
    std::unique_lock<std::mutex> lock(mutex);
    cv.wait(lock, [this]() { return !queue.empty() || finished; });
    if (queue.empty() && finished) {
        return false;
    }
    path = queue.front();
    queue.pop();
    return true;
}

void SafeQueue::set_finished() {
    {
        std::lock_guard<std::mutex> lock(mutex);
        finished = true;
    }
    cv.notify_all();
}

bool SafeQueue::is_empty() {
    std::lock_guard<std::mutex> lock(mutex);
    return queue.empty();
}

WorkerPool::WorkerPool(size_t num_threads, const std::function<bool(const fs::path&)>& process_func) {
    for (size_t i = 0; i < num_threads; ++i) {
        workers.emplace_back([this, process_func]() {
            fs::path path;
            while (queue.pop(path)) {
                try {
                    bool ok = process_func(path);
                    if (ok) {
                        processed_count++;
                    } else {
                        skipped_count++;
                    }
                }
                catch (...) {
                    skipped_count++;
                }
            }
        });
    }
}

WorkerPool::~WorkerPool() {
    wait_and_shutdown();
}

void WorkerPool::add_job(const fs::path& path) {
    queue.push(path);
}

void WorkerPool::wait_and_shutdown() {
    queue.set_finished();
    for (auto& t : workers) {
        if (t.joinable()) {
            t.join();
        }
    }
    workers.clear();
}
