// ============================================================
// handlers.h - 各业务 Handler（命令模式）
// ============================================================
#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include "../repository/repositories.h"
#include "../promotion/promotions.h"
#include "ICommandHandler.h"

// 观察者模式：订单事件总线（订单创建/状态变更时通知监听者）
class OrderEventListener {
public:
    virtual ~OrderEventListener() = default;
    virtual void onOrderCreated(const Order& order) = 0;
};

class OrderEventBus {
public:
    static OrderEventBus& instance() { static OrderEventBus b; return b; }
    void subscribe(OrderEventListener* listener) { listeners_.push_back(listener); }
    void notifyOrderCreated(const Order& order) {
        for (auto* l : listeners_) if (l) l->onOrderCreated(order);
    }
private:
    OrderEventBus() = default;
    std::vector<OrderEventListener*> listeners_;
};

// Handler 依赖上下文
struct HandlerContext {
    sqlite3* db = nullptr;
    std::shared_ptr<ProductRepository> products;
    std::shared_ptr<OrderRepository> orders;
    std::shared_ptr<UserRepository> users;
    std::shared_ptr<ReturnRepository> returns;
    std::shared_ptr<PromotionRepository> promos;
    std::shared_ptr<PromotionFactory> promoFactory;
    std::mutex* stockMutex = nullptr;
};

// 工具：构造错误响应
std::string makeError(int errCode, const std::string& msg);

// ---------- 商品 ----------
class ShowProductHandler : public ICommandHandler {
public:
    explicit ShowProductHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};

class ProductDetailHandler : public ICommandHandler {
public:
    explicit ProductDetailHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};

// ---------- 账号 ----------
class LoginHandler : public ICommandHandler {
public:
    explicit LoginHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};

class RegisterHandler : public ICommandHandler {
public:
    explicit RegisterHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};

// ---------- 购物车（仅库存校验） ----------
class AddCartHandler : public ICommandHandler {
public:
    explicit AddCartHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};

// ---------- 促销 ----------
class PromoListHandler : public ICommandHandler {
public:
    explicit PromoListHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};

// ---------- 结算（核心：事务 + 库存互斥 + 促销链） ----------
class CheckoutHandler : public ICommandHandler {
public:
    explicit CheckoutHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
    static std::string makeOrderNo();
};

// ---------- 订单查询 ----------
class OrderQueryHandler : public ICommandHandler {
public:
    explicit OrderQueryHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};

// ---------- 售后退货 ----------
class ReturnHandler : public ICommandHandler {
public:
    explicit ReturnHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};

// ---------- 心跳 ----------
class HeartbeatHandler : public ICommandHandler {
public:
    bool execute(const std::string& req, std::string& resp) override;
};
