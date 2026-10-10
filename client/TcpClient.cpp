#include "TcpClient.h"
#include "../common/ProtocolEncoder.h"
#include "../common/NetUtil.h"
#include <cstring>
#include <iostream>

bool TcpClient::connect() {
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        std::cerr << "WSAStartup 失败" << std::endl;
        return false;
    }
    sockfd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd_ == INVALID_SOCKET) return false;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(serverPort_));
    addr.sin_addr.s_addr = inet_addr(serverIp_.c_str());  // 127.0.0.1
    if (::connect(sockfd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == SOCKET_ERROR) {
        closesocket(sockfd_);
        sockfd_ = INVALID_SOCKET;
        return false;
    }
    connected_ = true;
    return true;
}

void TcpClient::disconnect() {
    if (sockfd_ != INVALID_SOCKET) {
        closesocket(sockfd_);
        sockfd_ = INVALID_SOCKET;
    }
    connected_ = false;
}

bool TcpClient::sendRaw(const char* data, int len) {
    if (sockfd_ == INVALID_SOCKET || !connected_) return false;
    int sent = 0;
    while (sent < len) {
        int ret = send(sockfd_, data + sent, len - sent, 0);
        if (ret == SOCKET_ERROR) return false;
        sent += ret;
    }
    return true;
}

bool TcpClient::sendRequest(int protocolCode, const std::string& jsonBody) {
    auto buffer = ProtocolEncoder::encode(static_cast<uint32_t>(protocolCode), jsonBody);
    return sendRaw(buffer.data(), static_cast<int>(buffer.size()));
}

bool TcpClient::receiveResponse(int& outProtocolCode, std::string& outJsonBody, int timeoutSec) {
    if (sockfd_ == INVALID_SOCKET || !connected_) return false;
    // 设置接收超时
    DWORD tv = static_cast<DWORD>(timeoutSec * 1000);
    setsockopt(sockfd_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&tv), sizeof(tv));

    char headBuf[sizeof(ProtocolHeader)];
    if (!NetUtil::recvExact(sockfd_, headBuf, sizeof(ProtocolHeader))) return false;
    ProtocolHeader header;
    std::memcpy(&header, headBuf, sizeof(header));
    if (ntohl(header.magic) != ProtocolConstants::MAGIC) return false;
    outProtocolCode = static_cast<int>(ntohl(header.code));
    uint32_t bodyLen = ntohl(header.length);
    if (bodyLen > 1024 * 1024) return false;
    outJsonBody.resize(bodyLen);
    if (bodyLen > 0 && !NetUtil::recvExact(sockfd_, &outJsonBody[0], static_cast<int>(bodyLen))) return false;
    return true;
}
