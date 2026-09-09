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
#include "TaskQueue.h"
#include "WorkerPool.h"
#include "handlers/handlers.h"

class TcpServer {
public:
    explicit TcpServer(int port = 8888) : port_(port), running_(false) {}
    ~TcpServer() { stop(); }

    bool start();
    void stop();

private:
    void acceptLoop();
    void clientHandler(SOCKET clientFd);

    int port_;
    SOCKET listenFd_ = INVALID_SOCKET;
    std::atomic<bool> running_;
    std::thread acceptThread_;
    WorkerPool workerPool_{4};
    HandlerContext ctx_;
};
