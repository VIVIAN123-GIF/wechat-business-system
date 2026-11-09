// ============================================================
// TcpServer.h - TCP 服务端（接入层）
// 线程模型: Acceptor 线程 + 每客户端收发线程 + WorkerPool
// ============================================================
#pragma once
#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif
#include <thread>
#include <atomic>
#include <memory>
#include <map>
#include <mutex>
#include "TaskQueue.h"
#include "WorkerPool.h"
#include "handlers/handlers.h"

class TcpServer {
public:
    explicit TcpServer(int port = 8888) : port_(port), running_(false) {}
    ~TcpServer() { stop(); }

    static std::mutex& sharedStockMutex();

    bool start();
    void stop();

private:
    void acceptLoop();
    void clientHandler(std::shared_ptr<ConnectionState> connection);

    int port_;
    SOCKET listenFd_ = INVALID_SOCKET;
    std::atomic<bool> running_;
    std::thread acceptThread_;
    WorkerPool workerPool_{4};
    HandlerContext ctx_;
    // 活跃客户端连接集合：stop() 时统一关闭，唤醒阻塞在 recv 的线程使其退出
    std::map<SOCKET, std::shared_ptr<ConnectionState>> activeClients_;
    std::mutex clientsMtx_;
};
