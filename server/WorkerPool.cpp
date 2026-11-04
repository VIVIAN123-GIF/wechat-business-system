#include "WorkerPool.h"
#include "../common/ProtocolEncoder.h"
#include "handlers/handlers.h"
#include "Logger.h"
#include "database/SQLiteHelper.h"

namespace {
int sendResponse(const Task& task, const std::vector<char>& buffer) {
    if (task.connection) {
        std::lock_guard<std::mutex> lock(task.connection->sendMutex);
        if (!task.connection->active.load() || task.connection->closed.load()) return SOCKET_ERROR;
        return send(task.clientFd, buffer.data(), static_cast<int>(buffer.size()), 0);
    }
    return send(task.clientFd, buffer.data(), static_cast<int>(buffer.size()), 0);
}

// 成功响应的协议码映射
uint32_t respCodeOf(uint32_t reqCode) {
    using namespace ProtocolConstants;
    switch (reqCode) {
        case C_REQ_PRODUCT_LIST:   return S_RES_PRODUCT_LIST;
        case C_REQ_PRODUCT_DETAIL: return S_RES_PRODUCT_DETAIL;
        case C_REQ_SEARCH:         return S_RES_SEARCH;
        case C_REQ_LOGIN:          return S_RES_LOGIN;
        case C_REQ_REGISTER:       return S_RES_REGISTER;
        case C_REQ_ADD_CART:       return S_RES_ADD_CART;
        case C_REQ_PROMO_LIST:     return S_RES_PROMO_LIST;
        case C_REQ_PROMO_EXPLAIN:  return S_RES_PROMO_EXPLAIN;
        case C_REQ_GROUP_CREATE:   return S_RES_GROUP_CREATE;
        case C_REQ_GROUP_JOIN:     return S_RES_GROUP_JOIN;
        case C_REQ_GROUP_QUERY:    return S_RES_GROUP_QUERY;
        case C_REQ_ORDER_STATUS:   return S_RES_ORDER_STATUS;
        case C_REQ_ORDER_AUDIT:    return S_RES_ORDER_AUDIT;
        case C_REQ_CHECKOUT:       return S_RES_CHECKOUT;
        case C_REQ_ORDER_QUERY:    return S_RES_ORDER_QUERY;
        case C_REQ_RETURN:         return S_RES_RETURN;
        case C_REQ_STATS:          return S_RES_STATS;
        case C_REQ_SERVICE_MSG:    return S_RES_SERVICE_MSG;
        case C_REQ_RETURN_LIST:    return S_RES_RETURN_LIST;
        case C_REQ_RETURN_AUDIT:   return S_RES_RETURN_AUDIT;
        case C_REQ_HEARTBEAT:      return S_RES_HEARTBEAT;
        default:                   return S_ERR_GENERAL;
    }
}
}

void WorkerPool::workerLoop() {
    LOG_INFO("Worker 线程启动");
    while (running_.load()) {
        Task task = TaskQueue::getInstance().pop();
        if (task.protocolCode == 0) continue;  // 关闭哨兵
        if (task.connection && !task.connection->active.load()) continue;

        sqlite3* workerDb = DatabaseManager::instance().getConnection();
        if (!workerDb) {
            auto err = makeError(ErrorCodes::SYSTEM_BUSY, "无法获取数据库连接");
            auto buf = ProtocolEncoder::encode(ProtocolConstants::S_ERR_GENERAL, err);
            sendResponse(task, buf);
            continue;
        }

        HandlerContext workerCtx = ctx_;
        workerCtx.db = workerDb;
        workerCtx.products = std::make_shared<ProductRepository>(workerDb);
        workerCtx.orders = std::make_shared<OrderRepository>(workerDb);
        workerCtx.users = std::make_shared<UserRepository>(workerDb);
        workerCtx.returns = std::make_shared<ReturnRepository>(workerDb);
        workerCtx.promos = std::make_shared<PromotionRepository>(workerDb);
        workerCtx.promoFactory = std::make_shared<PromotionFactory>(workerCtx.promos);

        auto handler = HandlerFactory::create(task.protocolCode, workerCtx);
        if (!handler) {
            std::string err = makeError(ErrorCodes::SYSTEM_BUSY, "未知协议码");
            auto buf = ProtocolEncoder::encode(ProtocolConstants::S_ERR_GENERAL, err);
            sendResponse(task, buf);
            continue;
        }

        std::string jsonResp;
        bool ok = false;
        try {
            ok = handler->execute(task.jsonBody, jsonResp);
        } catch (const std::exception& e) {
            jsonResp = makeError(ErrorCodes::SYSTEM_BUSY, std::string("服务异常: ") + e.what());
            ok = false;
        }

        uint32_t respCode = ok ? respCodeOf(task.protocolCode)
                               : ProtocolConstants::S_ERR_GENERAL;
        // 业务错误码为"未登录"时使用协议码 4002（会话鉴权专用）
        if (!ok) {
            try {
                auto jerr = nlohmann::json::parse(jsonResp);
                if (jerr.value("err_code", 0) == ErrorCodes::UNAUTHORIZED) {
                    respCode = ProtocolConstants::S_ERR_UNAUTH;
                }
            } catch (...) { /* 非 JSON 错误体，保持 4001 */ }
        }
        auto buffer = ProtocolEncoder::encode(respCode, jsonResp);
        int sent = sendResponse(task, buffer);
        if (sent == SOCKET_ERROR) {
            LOG_WARN("响应发送失败（客户端可能已断开）: code=" +
                     std::to_string(task.protocolCode));
        }
    }
    LOG_INFO("Worker 线程退出");
}
