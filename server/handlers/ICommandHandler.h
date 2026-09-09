// ============================================================
// ICommandHandler.h - 命令模式接口
// ============================================================
#pragma once
#include <string>
#include <cstdint>

class ICommandHandler {
public:
    virtual ~ICommandHandler() = default;
    // 执行请求，写入 JSON 响应体；返回 false 表示业务失败（响应应为 4001 错误）
    virtual bool execute(const std::string& jsonReq, std::string& jsonResp) = 0;
};
