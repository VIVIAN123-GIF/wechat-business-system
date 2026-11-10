// ============================================================
// handlers.h - 各业务 Handler（命令模式）
// ============================================================
#pragma once
#include <memory>
#include <mutex>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../repository/repositories.h"
#include "../promotion/promotions.h"
#include "../SessionManager.h"
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
    SessionManager* sessions = nullptr;
};

// ---------- 鉴权工具（会话 token 校验） ----------
namespace Auth {
    // 请求体必须携带有效 token；成功返回绑定用户 id
    bool requireLogin(HandlerContext& ctx, const nlohmann::json& j,
                      int& outUserId, std::string& errMsg);
    // token 必须有效且绑定到指定 user_id（防伪造他人身份）
    bool requireUser(HandlerContext& ctx, const nlohmann::json& j,
                     int userId, std::string& errMsg);
    // 管理员权限：token 有效且绑定用户 role 为 admin/operator
    bool requireAdmin(HandlerContext& ctx, const nlohmann::json& j,
                      int& outUserId, std::string& errMsg);
}

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

// ---------- 商品搜索（名称/分类/描述模糊查询） ----------
class SearchProductHandler : public ICommandHandler {
public:
    explicit SearchProductHandler(HandlerContext& ctx) : ctx_(ctx) {}
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

class PromoExplainHandler : public ICommandHandler {
public:
    explicit PromoExplainHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};

class GroupCreateHandler : public ICommandHandler {
public:
    explicit GroupCreateHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};
class GroupJoinHandler : public ICommandHandler {
public:
    explicit GroupJoinHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};
class GroupQueryHandler : public ICommandHandler {
public:
    explicit GroupQueryHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};

class OrderStatusHandler : public ICommandHandler {
public:
    explicit OrderStatusHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};
class OrderAuditHandler : public ICommandHandler {
public:
    explicit OrderAuditHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};

class CartListHandler : public ICommandHandler {
public:
    explicit CartListHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};

class CartSetQuantityHandler : public ICommandHandler {
public:
    explicit CartSetQuantityHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};

class CartRemoveHandler : public ICommandHandler {
public:
    explicit CartRemoveHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};

class CartClearHandler : public ICommandHandler {
public:
    explicit CartClearHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};
class PaymentHandler : public ICommandHandler { public: explicit PaymentHandler(HandlerContext& c):ctx_(c){} bool execute(const std::string&,std::string&) override; private: HandlerContext& ctx_; };
class CancelOrderHandler : public ICommandHandler { public: explicit CancelOrderHandler(HandlerContext& c):ctx_(c){} bool execute(const std::string&,std::string&) override; private: HandlerContext& ctx_; };
class ConfirmReceiptHandler : public ICommandHandler { public: explicit ConfirmReceiptHandler(HandlerContext& c):ctx_(c){} bool execute(const std::string&,std::string&) override; private: HandlerContext& ctx_; };
class AddressListHandler : public ICommandHandler { public: explicit AddressListHandler(HandlerContext& c):ctx_(c){} bool execute(const std::string&,std::string&) override; private: HandlerContext& ctx_; };
class AddressCreateHandler : public ICommandHandler { public: explicit AddressCreateHandler(HandlerContext& c):ctx_(c){} bool execute(const std::string&,std::string&) override; private: HandlerContext& ctx_; };
class ReviewCreateHandler : public ICommandHandler { public: explicit ReviewCreateHandler(HandlerContext& c):ctx_(c){} bool execute(const std::string&,std::string&) override; private: HandlerContext& ctx_; };
class ReviewListHandler : public ICommandHandler { public: explicit ReviewListHandler(HandlerContext& c):ctx_(c){} bool execute(const std::string&,std::string&) override; private: HandlerContext& ctx_; };
class CouponListHandler : public ICommandHandler { public: explicit CouponListHandler(HandlerContext& c):ctx_(c){} bool execute(const std::string&,std::string&) override; private: HandlerContext& ctx_; };
class CouponClaimHandler : public ICommandHandler { public: explicit CouponClaimHandler(HandlerContext& c):ctx_(c){} bool execute(const std::string&,std::string&) override; private: HandlerContext& ctx_; };
class NotificationListHandler : public ICommandHandler { public: explicit NotificationListHandler(HandlerContext& c):ctx_(c){} bool execute(const std::string&,std::string&) override; private: HandlerContext& ctx_; };
class NotificationReadHandler : public ICommandHandler { public: explicit NotificationReadHandler(HandlerContext& c):ctx_(c){} bool execute(const std::string&,std::string&) override; private: HandlerContext& ctx_; };
class FavoriteListHandler : public ICommandHandler { public: explicit FavoriteListHandler(HandlerContext& c):ctx_(c){} bool execute(const std::string&,std::string&) override; private: HandlerContext& ctx_; };
class FavoriteToggleHandler : public ICommandHandler { public: explicit FavoriteToggleHandler(HandlerContext& c):ctx_(c){} bool execute(const std::string&,std::string&) override; private: HandlerContext& ctx_; };
class CheckinHandler : public ICommandHandler { public: explicit CheckinHandler(HandlerContext& c):ctx_(c){} bool execute(const std::string&,std::string&) override; private: HandlerContext& ctx_; };
class ShipmentQueryHandler : public ICommandHandler { public: explicit ShipmentQueryHandler(HandlerContext& c):ctx_(c){} bool execute(const std::string&,std::string&) override; private: HandlerContext& ctx_; };
class ShipmentCreateHandler : public ICommandHandler { public: explicit ShipmentCreateHandler(HandlerContext& c):ctx_(c){} bool execute(const std::string&,std::string&) override; private: HandlerContext& ctx_; };
class AdminProductHandler : public ICommandHandler { public: explicit AdminProductHandler(HandlerContext& c):ctx_(c){} bool execute(const std::string&,std::string&) override; private: HandlerContext& ctx_; };
class AdminProductListHandler : public ICommandHandler { public: explicit AdminProductListHandler(HandlerContext& c):ctx_(c){} bool execute(const std::string&,std::string&) override; private: HandlerContext& ctx_; };
class SkuListHandler : public ICommandHandler { public: explicit SkuListHandler(HandlerContext& c):ctx_(c){} bool execute(const std::string&,std::string&) override; private: HandlerContext& ctx_; };

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

// ---------- 销售数据统计（选对象/选区间/自动聚合） ----------
class SalesStatsHandler : public ICommandHandler {
public:
    explicit SalesStatsHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
    // 组装 WHERE 过滤子句（按用户/商品/时间区间），返回子句字符串（含 ' WHERE ' 或 ' AND '）
    std::string buildFilter(int days, int productId, int userId, bool orderLevel);
};

// ---------- 客服消息（社交：联系客服，服务端回显并自动应答） ----------
class ServiceMsgHandler : public ICommandHandler {
public:
    explicit ServiceMsgHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};

// ---------- 退货单查询（用户查自己的；管理员可查全部） ----------
class ReturnListHandler : public ICommandHandler {
public:
    explicit ReturnListHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};

// ---------- 退货审核（管理员：通过则回补库存+订单置取消；拒绝则恢复原状态） ----------
class ReturnAuditHandler : public ICommandHandler {
public:
    explicit ReturnAuditHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};
class ServiceReplyHandler : public ICommandHandler {
public:
    explicit ServiceReplyHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};
class ServiceTicketsHandler : public ICommandHandler {
public: explicit ServiceTicketsHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private: HandlerContext& ctx_;
};
class ServiceTicketUpdateHandler : public ICommandHandler {
public: explicit ServiceTicketUpdateHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private: HandlerContext& ctx_;
};
class ReferralQueryHandler : public ICommandHandler {
public:
    explicit ReferralQueryHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};
class PromoAdminHandler : public ICommandHandler {
public: explicit PromoAdminHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private: HandlerContext& ctx_;
};
class ReturnCompleteHandler : public ICommandHandler {
public:
    explicit ReturnCompleteHandler(HandlerContext& ctx) : ctx_(ctx) {}
    bool execute(const std::string& req, std::string& resp) override;
private:
    HandlerContext& ctx_;
};

void expireGroupsNow(HandlerContext& ctx);
void expirePendingOrdersNow(HandlerContext& ctx);

// ---------- 心跳 ----------
class HeartbeatHandler : public ICommandHandler {
public:
    bool execute(const std::string& req, std::string& resp) override;
};
