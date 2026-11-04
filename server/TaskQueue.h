// ============================================================
// TaskQueue.h - 消息队列（单例 + 互斥锁 + 条件变量）
// 生产者: clientHandler 线程；消费者: Worker 线程（观察者模式）
// ============================================================
#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <cstdint>
#include <string>
#include <memory>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#endif

struct ConnectionState {
    SOCKET clientFd = INVALID_SOCKET;
    std::atomic<bool> active{true};
    std::atomic<bool> closed{false};
    std::mutex sendMutex;
};

struct Task {
    SOCKET clientFd = INVALID_SOCKET;
    std::shared_ptr<ConnectionState> connection;
    uint32_t protocolCode = 0;
    std::string jsonBody;
    long long timestamp = 0;
};

class TaskQueue {
public:
    static TaskQueue& getInstance() { static TaskQueue q; return q; }

    void push(const Task& task) {
        std::lock_guard<std::mutex> lock(mtx_);
        queue_.push(task);
        cond_.notify_one();
    }

    // 阻塞等待任务（观察者：Worker 等待被唤醒）
    Task pop() {
        std::unique_lock<std::mutex> lock(mtx_);
        cond_.wait(lock, [this]() { return !queue_.empty() || shutdown_.load(); });
        if (shutdown_.load() && queue_.empty()) {
            Task empty;
            return empty;
        }
        Task task = queue_.front();
        queue_.pop();
        return task;
    }

    size_t size() const {
        std::lock_guard<std::mutex> lock(mtx_);
        return queue_.size();
    }

    void setShutdown(bool flag) {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            shutdown_.store(flag);
        }
        cond_.notify_all();
    }

    // 重置队列：清空残留任务并解除 shutdown 状态（支持同进程 stop->start 重启）
    void reset() {
        {
            std::lock_guard<std::mutex> lock(mtx_);
            while (!queue_.empty()) queue_.pop();
            shutdown_.store(false);
        }
        cond_.notify_all();
    }

private:
    TaskQueue() = default;
    mutable std::mutex mtx_;
    std::condition_variable cond_;
    std::queue<Task> queue_;
    std::atomic<bool> shutdown_{false};
};
