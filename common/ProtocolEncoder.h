// ============================================================
// ProtocolEncoder.h/.cpp - 协议编解码（长度前缀法处理粘包）
// ============================================================
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "ProtocolHeader.h"

class ProtocolEncoder {
public:
    // 编码: 头(网络字节序) + 体
    static std::vector<char> encode(uint32_t code, const std::string& jsonBody);

    static size_t headerSize() { return ProtocolConstants::HEADER_SIZE; }
};
