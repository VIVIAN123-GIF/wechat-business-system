#include "ApiClient.h"
#include "NetworkClient.h"
#include "common/SHA256.h"
#include <QByteArray>
#include <algorithm>

ApiClient::ApiClient(QObject* parent)
    : QObject(parent), net_(new NetworkClient(this)) {
    // 收到完整响应：解析为 JSON，交给当前等待的回调
    connect(net_, &NetworkClient::packetReceived, this,
            [this](quint32 code, const QByteArray& body) {
        json j;
        try {
            j = json::parse(body.toStdString());
        } catch (...) {
            j = json::object();
        }
        if (pendingCb_) {
            Cb cb = pendingCb_;
            pendingCb_ = nullptr;
            cb(code, j);
        }
    });
}

NetworkClient* ApiClient::network() const { return net_; }

std::string ApiClient::hashPwd(const QString& user, const QString& pwd) {
    // 与服务端一致：SHA256("微商系统$" + username + "#" + password)
    return hashPassword(user.toStdString(), pwd.toStdString());
}

void ApiClient::send(quint32 code, json j, Cb cb) {
    // 已登录则自动注入会话 token（鉴权）
    if (!token_.empty() && !j.contains("token")) {
        j["token"] = token_;
    }
    pendingCb_ = cb;
    net_->sendPacket(code, j.dump());
}

// ---------------- 账号 ----------------
void ApiClient::login(const QString& user, const QString& pwd, Cb cb) {
    json j;
    j["username"] = user.toStdString();
    j["password"] = hashPwd(user, pwd);
    send(1006, j, [this, cb, user](quint32 code, const json& r) {
        if (code == 2006) {  // 登录成功：更新会话状态
            userId_   = r.value("user_id", 0);
            username_ = user;
            level_    = r.value("level", 1);
            token_    = r.value("token", "");
        }
        if (cb) cb(code, r);
    });
}

void ApiClient::registerUser(const QString& user, const QString& pwd, Cb cb) {
    json j;
    j["username"] = user.toStdString();
    j["password"] = hashPwd(user, pwd);
    send(1007, j, cb);
}

// ---------------- 商品 ----------------
void ApiClient::fetchProducts(Cb cb) {
    send(1001, json::object(), cb);
}

void ApiClient::fetchDetail(int pid, Cb cb) {
    json j;
    j["product_id"] = pid;
    send(1002, j, cb);
}

void ApiClient::search(const QString& keyword, Cb cb) {
    search(keyword, 0, 0, {}, {}, cb);
}

void ApiClient::search(const QString& keyword, int minPrice, int maxPrice, const QString& category, const QString& sort, Cb cb) {
    json j;
    j["keyword"] = keyword.toStdString();
    if (minPrice > 0) j["min_price"] = minPrice;
    if (maxPrice > 0) j["max_price"] = maxPrice;
    if (!category.trimmed().isEmpty()) j["category"] = category.toStdString();
    if (!sort.isEmpty()) j["sort"] = sort.toStdString();
    send(1005, j, cb);
}

// ---------------- 加购 / 促销 / 结算 ----------------
void ApiClient::addToCart(int pid, int qty, Cb cb) {
    json j;
    j["user_id"] = userId_;
    j["product_id"] = pid;
    j["quantity"] = qty;
    send(1010, j, cb);
}

void ApiClient::fetchPromotions(Cb cb) {
    send(1020, json::object(), cb);
}

void ApiClient::explainPromotions(const std::vector<int>& promoIds, Cb cb) {
    json j;
    j["user_id"] = userId_;
    j["promo_ids"] = promoIds;
    json items = json::array();
    for (const auto& line : cart_) {
        items.push_back({{"product_id", line.productId}, {"quantity", line.quantity}, {"sku_id", line.skuId}});
    }
    j["items"] = items;
    send(1090, j, cb);
}

void ApiClient::checkout(const std::vector<int>& promoIds, int addressId, Cb cb) {
    checkout(promoIds, addressId, 0, cb);
}

void ApiClient::checkout(const std::vector<int>& promoIds, int addressId, int couponId, Cb cb) {
    json j;
    j["user_id"] = userId_;
    json items = json::array();
    for (const auto& line : cart_) {
        json it;
        it["product_id"] = line.productId;
        it["quantity"] = line.quantity;
        if (line.skuId > 0) it["sku_id"] = line.skuId;
        items.push_back(it);
    }
    j["items"] = items;
    j["promo_ids"] = promoIds;
    j["address_id"] = addressId;
    if (couponId > 0) j["coupon_id"] = couponId;
    send(1030, j, cb);
}

void ApiClient::fetchCoupons(Cb cb) { send(1140, {{"user_id", userId_}}, cb); }
void ApiClient::claimCoupon(int promotionId, Cb cb) { send(1141, {{"user_id", userId_}, {"promotion_id", promotionId}}, cb); }
void ApiClient::fetchNotifications(Cb cb) { send(1150, {{"user_id", userId_}}, cb); }
void ApiClient::markNotificationRead(int notificationId, Cb cb) { send(1151, {{"user_id", userId_}, {"notification_id", notificationId}}, cb); }

void ApiClient::fetchAddresses(Cb cb) { send(1120, {{"user_id", userId_}}, cb); }

void ApiClient::createAddress(const QString& receiver, const QString& phone, const QString& detail, bool isDefault, Cb cb) {
    send(1121, {{"user_id", userId_}, {"receiver_name", receiver.toStdString()}, {"phone", phone.toStdString()}, {"detail", detail.toStdString()}, {"is_default", isDefault}}, cb);
}

void ApiClient::payOrder(int orderId, Cb cb) { send(1100, {{"user_id", userId_}, {"order_id", orderId}}, cb); }

void ApiClient::cancelOrder(int orderId, Cb cb) { send(1101, {{"user_id", userId_}, {"order_id", orderId}}, cb); }

void ApiClient::confirmReceipt(int orderId, Cb cb) { send(1110, {{"user_id", userId_}, {"order_id", orderId}}, cb); }

void ApiClient::createReview(int orderId, int productId, int rating, const QString& content, Cb cb) {
    createReview(orderId, productId, rating, content, QString(), cb);
}

void ApiClient::createReview(int orderId, int productId, int rating, const QString& content, const QString& imageUrl, Cb cb) {
    send(1130, {{"user_id", userId_}, {"order_id", orderId}, {"product_id", productId}, {"rating", rating}, {"content", content.toStdString()}, {"image_url", imageUrl.toStdString()}}, cb);
}

void ApiClient::fetchReviews(int productId, Cb cb) { send(1131, {{"product_id", productId}}, cb); }
void ApiClient::fetchSkus(int productId, Cb cb) { send(1200, {{"product_id", productId}}, cb); }
void ApiClient::toggleFavorite(int productId, Cb cb) { send(1161, {{"user_id", userId_}, {"product_id", productId}}, cb); }
void ApiClient::fetchFavorites(Cb cb) { send(1160, {{"user_id", userId_}}, cb); }
void ApiClient::checkIn(Cb cb) { send(1170, {{"user_id", userId_}}, cb); }
void ApiClient::fetchShipment(int orderId, Cb cb) { send(1180, {{"user_id", userId_}, {"order_id", orderId}}, cb); }
void ApiClient::saveShipment(int orderId, const QString& trackingNo, int status, Cb cb) { send(1181, {{"user_id", userId_}, {"order_id", orderId}, {"tracking_no", trackingNo.toStdString()}, {"status", status}}, cb); }
void ApiClient::updateProduct(int productId, const QString& action, int value, Cb cb) { send(1190, {{"user_id", userId_}, {"product_id", productId}, {"action", action.toStdString()}, {"value", value}}, cb); }
void ApiClient::updateOrderStatus(int orderId, int status, Cb cb) { send(1095, {{"user_id", userId_}, {"order_id", orderId}, {"status", status}}, cb); }

void ApiClient::createGroup(int productId, int targetCount, int hours, Cb cb) {
    send(1091, {{"user_id", userId_}, {"product_id", productId},
                {"target_count", targetCount}, {"hours", hours}}, cb);
}

void ApiClient::joinGroup(int groupId, Cb cb) {
    send(1092, {{"user_id", userId_}, {"group_id", groupId}}, cb);
}

void ApiClient::fetchGroups(Cb cb) {
    send(1093, {{"user_id", userId_}}, cb);
}

// ---------------- 订单 / 售后 ----------------
void ApiClient::fetchOrders(Cb cb) {
    json j;
    j["user_id"] = userId_;
    send(1040, j, cb);
}

void ApiClient::fetchOrderAudit(int orderId, Cb cb) {
    send(1096, {{"user_id", userId_}, {"order_id", orderId}}, cb);
}

void ApiClient::applyReturn(int orderId, const QString& reason, Cb cb) {
    json j;
    j["user_id"] = userId_;
    j["order_id"] = orderId;
    j["reason"] = reason.toStdString();
    send(1050, j, cb);
}

void ApiClient::fetchReturns(Cb cb) {
    json j;
    j["user_id"] = userId_;
    send(1070, j, cb);
}

void ApiClient::auditReturn(int returnId, bool approve, Cb cb) {
    json j;
    j["user_id"] = userId_;
    j["return_id"] = returnId;
    j["approve"] = approve;
    send(1080, j, cb);
}

// ---------------- 统计 / 客服 ----------------
void ApiClient::fetchStats(int days, int productId, int userId, Cb cb) {
    json j;
    j["days"] = days;
    j["product_id"] = productId;
    j["user_id"] = userId;
    send(1060, j, cb);
}

void ApiClient::sendService(const QString& content, Cb cb) {
    json j;
    j["user_id"] = userId_;
    j["content"] = content.toStdString();
    send(1065, j, cb);
}

// ---------------- 本地购物车 ----------------
void ApiClient::cartAdd(int pid, const QString& name, int priceCents, int qty, int skuId, const QString& skuLabel) {
    for (auto& line : cart_) {
        if (line.productId == pid && line.skuId == skuId) {
            line.quantity += qty;
            return;
        }
    }
    cart_.push_back(CartLine{pid, skuId, skuLabel, name, priceCents, qty});
}

void ApiClient::completeReturn(int returnId, Cb cb) {
    send(1081, {{"user_id", userId_}, {"return_id", returnId}}, cb);
}

void ApiClient::cartRemove(int pid) {
    cart_.erase(std::remove_if(cart_.begin(), cart_.end(),
                [pid](const CartLine& l) { return l.productId == pid; }),
                cart_.end());
}

void ApiClient::cartSetQty(int pid, int qty) {
    for (auto& line : cart_) {
        if (line.productId == pid) {
            if (qty <= 0) cartRemove(pid);
            else line.quantity = qty;
            return;
        }
    }
}

void ApiClient::cartClear() { cart_.clear(); }

int ApiClient::cartCount() const {
    int n = 0;
    for (const auto& l : cart_) n += l.quantity;
    return n;
}

int ApiClient::cartTotalCents() const {
    int t = 0;
    for (const auto& l : cart_) t += l.priceCents * l.quantity;
    return t;
}
