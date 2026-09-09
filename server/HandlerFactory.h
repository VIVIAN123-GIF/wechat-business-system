// ============================================================
// HandlerFactory.h - 工厂模式：按协议码创建 Handler
// ============================================================
#pragma once
#include <memory>
#include <cstdint>
#include "handlers/ICommandHandler.h"
#include "handlers/handlers.h"

class HandlerFactory {
public:
    static std::unique_ptr<ICommandHandler> create(uint32_t code, HandlerContext& ctx);
};
