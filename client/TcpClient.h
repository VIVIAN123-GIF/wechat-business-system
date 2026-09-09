// ============================================================
// TcpClient.h - 客户端通信封装（RAII 管理 Socket）
// ============================================================
#pragma once
#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif
#include <string>

class TcpClient {
public:
    TcpClient(const std::string& ip = "127.0.0.1", int port = 8888)
        : serverIp_(ip), serverPort_(port), sockfd_(INVALID_SOCKET), connected_(false) {}
    ~TcpClient() { disconnect(); }

    // 禁止拷贝
    TcpClient(const TcpClient&) = delete;
    TcpClient& operator=(const TcpClient&) = delete;

    bool connect();
    void disconnect();
    bool isConnected() const { return connected_; }

    // 发送请求（自动加协议头）
    bool sendRequest(int protocolCode, const std::string& jsonBody);
    // 同步接收响应（含粘包处理）
    bool receiveResponse(int& outProtocolCode, std::string& outJsonBody, int timeoutSec = 5);

private:
    bool sendRaw(const char* data, int len);

    std::string serverIp_;
    int serverPort_;
    SOCKET sockfd_;
    bool connected_;
};
