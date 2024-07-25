#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include "threadPool.h"

// Constructor
ThreadPool::ThreadPool(size_t numThreads) : stop(false)
{
    for (size_t i = 0; i < numThreads; ++i)
    {
        workers.emplace_back([this]
                             {
            while (true) {
                std::function<void()> task;

                {
                    std::unique_lock<std::mutex> lock(this->queueMutex);
                    this->condition.wait(lock, [this] { return this->stop || !this->tasks.empty(); });
                    if (this->stop && this->tasks.empty()) return;
                    task = std::move(this->tasks.front());
                    this->tasks.pop();
                }

                task();
            } });
    }
}

// Destructor
ThreadPool::~ThreadPool()
{
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stop = true;
    }
    condition.notify_all();
    for (std::thread &worker : workers)
    {
        worker.join();
    }
}

// Add new task to the pool
void ThreadPool::enqueue(std::function<void()> task)
{
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        tasks.emplace(task);
    }
    condition.notify_one();
}

void ThreadPool::shutdown() {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        stop = true;
    }
    condition.notify_all();
    for (std::thread &worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}
