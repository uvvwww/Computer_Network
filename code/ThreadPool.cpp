#include "ThreadPool.hpp"

ThreadPool::ThreadPool(size_t n) {
    for (size_t i = 0; i < n; ++i) {
        workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {   // critical section
                    std::unique_lock<std::mutex> lock(mtx);
                    cv.wait(lock, [this]{ return stop || !tasks.empty(); });
                    if (stop && tasks.empty()) return;
                    task = std::move(tasks.front());
                    tasks.pop();
                }
                task(); // execute
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    { std::unique_lock<std::mutex> lock(mtx);
      stop = true; }
    cv.notify_all();
    for (auto& w : workers) w.join();
}

void ThreadPool::enqueue(std::function<void()> f) {
        { std::unique_lock<std::mutex> lock(mtx);
            tasks.emplace(std::move(f)); }
        cv.notify_one();
}
