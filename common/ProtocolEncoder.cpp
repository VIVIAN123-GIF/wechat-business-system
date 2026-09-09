#include "ProtocolEncoder.h"

#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

std::vector<char> ProtocolEncoder::encode(uint32_t code, const std::string& jsonBody) {
    ProtocolHeader header;
    std::memset(&header, 0, sizeof(header));
    header.magic   = htonl(ProtocolConstants::MAGIC);
    header.version = htonl(ProtocolConstants::VERSION);
    header.code    = htonl(code);
    header.length  = htonl(static_cast<uint32_t>(jsonBody.size()));

    std::vector<char> buffer(sizeof(header) + jsonBody.size());
    std::memcpy(buffer.data(), &header, sizeof(header));
    if (!jsonBody.empty()) {
        std::memcpy(buffer.data() + sizeof(header), jsonBody.data(), jsonBody.size());
    }
    return buffer;
}

bool ProtocolEncoder::decode(const std::vector<char>& buffer,
                             uint32_t& outCode, std::string& outBody) {
    if (buffer.size() < sizeof(ProtocolHeader)) return false;
    ProtocolHeader header;
    std::memcpy(&header, buffer.data(), sizeof(header));
    if (ntohl(header.magic) != ProtocolConstants::MAGIC) return false;
    if (ntohl(header.version) != ProtocolConstants::VERSION) return false;
    outCode = ntohl(header.code);
    uint32_t len = ntohl(header.length);
    if (buffer.size() < sizeof(header) + len) return false;
    outBody.assign(buffer.data() + sizeof(header), len);
    return true;
}
