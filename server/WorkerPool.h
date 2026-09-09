// ============================================================
// WorkerPool.h - 线程池（观察者模式的观察者）
// ============================================================
#pragma once
#include <vector>
#include <thread>
#include <atomic>
#include <memory>
#include "TaskQueue.h"
#include "HandlerFactory.h"

class WorkerPool {
public:
    explicit WorkerPool(int count = 4) : workerCount_(count), running_(false) {}
    ~WorkerPool() { stop(); }

    void start() {
        if (running_.load()) return;
        running_.store(true);
        for (int i = 0; i < workerCount_; ++i) {
            workers_.emplace_back([this]() { workerLoop(); });
        }
    }

    void stop() {
        if (!running_.load()) return;
        running_.store(false);
        TaskQueue::getInstance().setShutdown(true);
        for (auto& w : workers_) {
            if (w.joinable()) w.join();
        }
        workers_.clear();
    }

    void setContext(HandlerContext ctx) { ctx_ = std::move(ctx); }

private:
    void workerLoop();

    int workerCount_;
    std::atomic<bool> running_;
    std::vector<std::thread> workers_;
    HandlerContext ctx_;
};
