// ============================================================
// NetUtil.h - 网络收发公共工具（长度前缀法处理粘包/半包）
// 服务端接入层 / 客户端 / 集成测试共用，消除三处重复实现
// ============================================================
#pragma once
#ifdef _WIN32
#include <winsock2.h>
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#endif
#include <string>
#include <cstring>
#include "ProtocolHeader.h"

namespace NetUtil {

// 循环读满指定字节
inline bool recvExact(SOCKET fd, char* buf, int len) {
    int received = 0;
    while (received < len) {
        int ret = recv(fd, buf + received, len - received, 0);
        if (ret <= 0) return false;  // 连接关闭或错误
        received += ret;
    }
    return true;
}

// 读取完整报文：先读满 16 字节头，再按 Length 读满数据体（粘包/半包处理）
inline bool recvFullPacket(SOCKET fd, uint32_t& outCode, std::string& outBody) {
    char headBuf[sizeof(ProtocolHeader)];
    if (!recvExact(fd, headBuf, static_cast<int>(sizeof(ProtocolHeader)))) return false;
    ProtocolHeader header;
    std::memcpy(&header, headBuf, sizeof(header));
    if (ntohl(header.magic) != ProtocolConstants::MAGIC) return false;
    if (ntohl(header.version) != ProtocolConstants::VERSION) return false;
    outCode = ntohl(header.code);
    uint32_t bodyLen = ntohl(header.length);
    if (bodyLen > 1024 * 1024) return false;  // 单报文上限 1MB
    outBody.resize(bodyLen);
    if (bodyLen > 0 && !recvExact(fd, &outBody[0], static_cast<int>(bodyLen))) return false;
    return true;
}

} // namespace NetUtil
