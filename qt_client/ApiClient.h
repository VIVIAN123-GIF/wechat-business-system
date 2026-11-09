// ============================================================
// ApiClient.h - 业务/会话层
// 职责：封装各协议请求、维护登录会话（token/user_id/level）、
//       维护本地购物车；通过回调把响应交给 UI
// ============================================================
#pragma once
#include <QObject>
#include <QString>
#include <functional>
#include <vector>
#include <nlohmann/json.hpp>

class NetworkClient;
using json = nlohmann::json;

// 本地购物车一行
struct CartLine {
    int productId = 0;
    int skuId = 0;
    QString skuLabel;
    QString name;
    int priceCents = 0;
    int quantity = 0;
};

class ApiClient : public QObject {
    Q_OBJECT
public:
    explicit ApiClient(QObject* parent = nullptr);

    NetworkClient* network() const;

    // ---- 会话信息 ----
    int userId() const { return userId_; }
    QString username() const { return username_; }
    int level() const { return level_; }
    bool isAdmin() const { return level_ >= 2; }

    // ---- 本地购物车 ----
    const std::vector<CartLine>& cart() const { return cart_; }
    void cartAdd(int pid, const QString& name, int priceCents, int qty, int skuId = 0, const QString& skuLabel = {});
    void cartRemove(int pid);
    void cartSetQty(int pid, int qty);
    void cartClear();
    int cartCount() const;
    int cartTotalCents() const;

    // 响应回调：code 为响应协议码（2xxx 成功 / 4001 / 4002）
    using Cb = std::function<void(quint32 code, const json& j)>;

    // ---- 账号 ----
    void login(const QString& user, const QString& pwd, Cb cb);
    void registerUser(const QString& user, const QString& pwd, Cb cb);

    // ---- 商品 ----
    void fetchProducts(Cb cb);
    void fetchDetail(int pid, Cb cb);
    void search(const QString& keyword, Cb cb);
    void search(const QString& keyword, int minPrice, int maxPrice, const QString& category, const QString& sort, Cb cb);

    // ---- 加购 / 促销 / 结算 ----
    void addToCart(int pid, int qty, Cb cb);
    void fetchPromotions(Cb cb);
    void explainPromotions(const std::vector<int>& promoIds, Cb cb);
    void checkout(const std::vector<int>& promoIds, int addressId, Cb cb);
    void checkout(const std::vector<int>& promoIds, int addressId, int couponId, Cb cb);
    void fetchCoupons(Cb cb);
    void claimCoupon(int promotionId, Cb cb);
    void fetchNotifications(Cb cb);
    void markNotificationRead(int notificationId, Cb cb);
    void fetchAddresses(Cb cb);
    void createAddress(const QString& receiver, const QString& phone, const QString& detail, bool isDefault, Cb cb);
    void payOrder(int orderId, Cb cb);
    void cancelOrder(int orderId, Cb cb);
    void confirmReceipt(int orderId, Cb cb);
    void createReview(int orderId, int productId, int rating, const QString& content, Cb cb);
    void createReview(int orderId, int productId, int rating, const QString& content, const QString& imageUrl, Cb cb);
    void fetchReviews(int productId, Cb cb);
    void fetchSkus(int productId, Cb cb);
    void toggleFavorite(int productId, Cb cb);
    void fetchFavorites(Cb cb);
    void checkIn(Cb cb);
    void fetchShipment(int orderId, Cb cb);
    void saveShipment(int orderId, const QString& trackingNo, int status, Cb cb);
    void updateProduct(int productId, const QString& action, int value, Cb cb);
    void updateOrderStatus(int orderId, int status, Cb cb);

    void createGroup(int productId, int targetCount, int hours, Cb cb);
    void joinGroup(int groupId, Cb cb);
    void fetchGroups(Cb cb);

    // ---- 订单 / 售后 ----
    void fetchOrders(Cb cb);
    void fetchOrderAudit(int orderId, Cb cb);
    void applyReturn(int orderId, const QString& reason, Cb cb);
    void fetchReturns(Cb cb);
    void auditReturn(int returnId, bool approve, Cb cb);
    void completeReturn(int returnId, Cb cb);

    // ---- 统计 / 客服 ----
    void fetchStats(int days, int productId, int userId, Cb cb);
    void sendService(const QString& content, Cb cb);

private:
    NetworkClient* net_;
    Cb pendingCb_;
    int userId_ = 0;
    QString username_;
    int level_ = 1;
    std::string token_;
    std::vector<CartLine> cart_;

    void send(quint32 code, json j, Cb cb);
    std::string hashPwd(const QString& user, const QString& pwd);
};
