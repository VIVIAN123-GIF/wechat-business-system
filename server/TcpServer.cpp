#include "TcpServer.h"
#include "../common/ProtocolEncoder.h"
#include "../common/NetUtil.h"
#include "database/SQLiteHelper.h"
#include "Logger.h"
#include <iostream>

bool TcpServer::start() {
    // 支持同进程 stop->start 重启：清空上次残留任务并解除 shutdown
    TaskQueue::getInstance().reset();

    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        LOG_ERROR("WSAStartup 失败");
        return false;
    }

    listenFd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd_ == INVALID_SOCKET) {
        LOG_ERROR("socket() 失败");
        WSACleanup();
        return false;
    }

    // 端口复用：避免上次异常退出后 TIME_WAIT 导致 bind 失败
    int opt = 1;
    setsockopt(listenFd_, SOL_SOCKET, SO_REUSEADDR, reinterpret_cast<const char*>(&opt), sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(static_cast<u_short>(port_));
    if (bind(listenFd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        LOG_ERROR("bind() 失败: 端口 " + std::to_string(port_) + " 被占用");
        closesocket(listenFd_);
        listenFd_ = INVALID_SOCKET;
        WSACleanup();
        return false;
    }
    if (listen(listenFd_, SOMAXCONN) == SOCKET_ERROR) {
        LOG_ERROR("listen() 失败");
        closesocket(listenFd_);
        listenFd_ = INVALID_SOCKET;
        WSACleanup();
        return false;
    }

    // 构造依赖上下文
    ctx_.db = DatabaseManager::instance().getConnection();
    ctx_.products      = std::make_shared<ProductRepository>(ctx_.db);
    ctx_.orders        = std::make_shared<OrderRepository>(ctx_.db);
    ctx_.users         = std::make_shared<UserRepository>(ctx_.db);
    ctx_.returns       = std::make_shared<ReturnRepository>(ctx_.db);
    ctx_.promos        = std::make_shared<PromotionRepository>(ctx_.db);
    ctx_.promoFactory  = std::make_shared<PromotionFactory>(ctx_.promos);
    static std::mutex stockMutex;
    ctx_.stockMutex = &stockMutex;
    ctx_.sessions   = &SessionManager::instance();

    workerPool_.setContext(ctx_);
    workerPool_.start();

    running_.store(true);
    acceptThread_ = std::thread(&TcpServer::acceptLoop, this);
    LOG_INFO("微商系统服务端已启动，监听端口 " + std::to_string(port_));
    return true;
}

void TcpServer::acceptLoop() {
    while (running_.load()) {
        SOCKET clientFd = accept(listenFd_, nullptr, nullptr);
        if (clientFd == INVALID_SOCKET) {
            if (running_.load()) LOG_WARN("accept() 失败");
            continue;
        }
        // 每客户端一个收发线程（业务处理由 WorkerPool 完成）
        std::thread(&TcpServer::clientHandler, this, clientFd).detach();
    }
}

void TcpServer::clientHandler(SOCKET clientFd) {
    {
        std::lock_guard<std::mutex> lock(clientsMtx_);
        activeClients_.insert(clientFd);
    }
    LOG_INFO("客户端连接接入");
    while (running_.load()) {
        uint32_t code = 0;
        std::string body;
        if (!NetUtil::recvFullPacket(clientFd, code, body)) {
            break;  // 客户端断开或错误
        }
        Task task;
        task.clientFd = clientFd;
        task.protocolCode = code;
        task.jsonBody = body;
        task.timestamp = 0;
        TaskQueue::getInstance().push(task);  // 生产：入队并唤醒 Worker
    }
    closesocket(clientFd);
    {
        std::lock_guard<std::mutex> lock(clientsMtx_);
        activeClients_.erase(clientFd);
    }
    LOG_INFO("客户端连接断开");
}

void TcpServer::stop() {
    if (!running_.load()) return;
    running_.store(false);
    if (listenFd_ != INVALID_SOCKET) {
        closesocket(listenFd_);
        listenFd_ = INVALID_SOCKET;
    }
    // 关闭所有活跃客户端连接：阻塞在 recv 的 clientHandler 线程立即返回并退出
    {
        std::lock_guard<std::mutex> lock(clientsMtx_);
        for (SOCKET fd : activeClients_) closesocket(fd);
        activeClients_.clear();
    }
    TaskQueue::getInstance().setShutdown(true);
    workerPool_.stop();
    if (acceptThread_.joinable()) acceptThread_.join();
    WSACleanup();
    LOG_INFO("服务端已停止");
}
