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
