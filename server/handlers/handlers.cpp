#include "handlers.h"
#include "../Logger.h"
#include "../../common/ProtocolHeader.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

std::string makeError(int errCode, const std::string& msg) {
    json j;
    j["err_code"] = errCode;
    j["err_msg"]  = msg;
    return j.dump();
}

// ---------------- 商品 ----------------
bool ShowProductHandler::execute(const std::string&, std::string& resp) {
    auto products = ctx_.products->findAll();
    json j;
    j["products"] = json::array();
    for (const auto& p : products) {
        json item;
        item["id"]    = p.id;
        item["name"]  = p.name;
        item["price"] = p.price;
        item["stock"] = p.stock;
        item["category"] = p.category;
        j["products"].push_back(item);
    }
    resp = j.dump();
    return true;
}

bool ProductDetailHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int pid = j.value("product_id", 0);
        auto p = ctx_.products->findById(pid);
        if (!p) { resp = makeError(ErrorCodes::PRODUCT_NOT_FOUND, "商品不存在"); return false; }
        json out;
        out["id"] = p->id; out["name"] = p->name; out["price"] = p->price;
        out["stock"] = p->stock; out["desc"] = p->description; out["category"] = p->category;
        resp = out.dump();
        return true;
    } catch (const std::exception& e) {
        resp = makeError(ErrorCodes::SYSTEM_BUSY, std::string("参数错误: ") + e.what());
        return false;
    }
}

// ---------------- 账号 ----------------
bool LoginHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        std::string username = j.value("username", "");
        std::string password = j.value("password", "");
        if (username.empty() || password.empty()) {
            resp = makeError(ErrorCodes::USER_NOT_FOUND, "用户名或密码不能为空");
            return false;
        }
        auto user = ctx_.users->findByUsername(username);
        if (!user || user->password != password) {
            resp = makeError(ErrorCodes::USER_NOT_FOUND, "用户名或密码错误");
            return false;
        }
        ctx_.users->updateLastLogin(user->id);
        json out;
        out["success"] = true;
        out["user_id"] = user->id;
        out["username"] = user->username;
        out["level"]  = user->level;
        resp = out.dump();
        return true;
    } catch (const std::exception& e) {
        resp = makeError(ErrorCodes::SYSTEM_BUSY, std::string("参数错误: ") + e.what());
        return false;
    }
}

bool RegisterHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        std::string username = j.value("username", "");
        std::string password = j.value("password", "");
        if (username.size() < 2 || password.size() < 4) {
            resp = makeError(ErrorCodes::USER_NOT_FOUND, "用户名至少2位、密码至少4位");
            return false;
        }
        if (ctx_.users->findByUsername(username)) {
            resp = makeError(ErrorCodes::USER_NOT_FOUND, "用户名已存在");
            return false;
        }
        User u;
        u.username = username;
        u.password = password;
        u.level = 1;
        if (!ctx_.users->insert(u)) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "注册失败");
            return false;
        }
        json out;
        out["success"] = true;
        out["user_id"] = u.id;
        resp = out.dump();
        return true;
    } catch (const std::exception& e) {
        resp = makeError(ErrorCodes::SYSTEM_BUSY, std::string("参数错误: ") + e.what());
        return false;
    }
}

// ---------------- 购物车（库存校验） ----------------
bool AddCartHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int pid = j.value("product_id", 0);
        int qty = j.value("quantity", 0);
        auto p = ctx_.products->findById(pid);
        if (!p) { resp = makeError(ErrorCodes::PRODUCT_NOT_FOUND, "商品不存在"); return false; }
        if (qty <= 0) { resp = makeError(ErrorCodes::STOCK_NOT_ENOUGH, "数量不合法"); return false; }
        if (p->stock < qty) {
            resp = makeError(ErrorCodes::STOCK_NOT_ENOUGH, "库存不足");
            return false;
        }
        json out;
        out["success"] = true;
        out["message"] = "库存充足";
        out["stock_left"] = p->stock;
        resp = out.dump();
        return true;
    } catch (const std::exception& e) {
        resp = makeError(ErrorCodes::SYSTEM_BUSY, std::string("参数错误: ") + e.what());
        return false;
    }
}

// ---------------- 促销 ----------------
bool PromoListHandler::execute(const std::string&, std::string& resp) {
    auto promos = ctx_.promos->findAllAvailable();
    json j;
    j["promotions"] = json::array();
    for (const auto& p : promos) {
        json item;
        item["id"]   = p.id;
        item["name"] = p.name;
        item["type"] = p.type;
        item["param1"] = p.param1;
        item["param2"] = p.param2;
        j["promotions"].push_back(item);
    }
    resp = j.dump();
    return true;
}

// ---------------- 结算 ----------------
std::string CheckoutHandler::makeOrderNo() {
    auto t = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    std::ostringstream oss;
    oss << "MS" << std::put_time(&tm, "%Y%m%d%H%M%S");
    // 追加毫秒序号避免同秒重复
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() % 1000;
    oss << std::setw(3) << std::setfill('0') << ms;
    return oss.str();
}

bool CheckoutHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int userId = j.value("user_id", 0);
        auto user = ctx_.users->findById(userId);
        if (!user) { resp = makeError(ErrorCodes::USER_NOT_FOUND, "用户不存在"); return false; }

        std::vector<int> promoIds;
        if (j.contains("promo_ids")) {
            for (auto& id : j["promo_ids"]) promoIds.push_back(id.get<int>());
        }

        Order order;
        order.userId = userId;
        order.status = 1;  // 已支付
        std::unique_ptr<IPromotion> chain;  // 促销链（作用域需覆盖响应构造）

        sqlite3* db = ctx_.db;

        // ===== 事务开始（BEGIN IMMEDIATE 避免读写锁升级死锁） =====
        char* errMsg = nullptr;
        if (sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "系统繁忙，请稍后重试");
            return false;
        }

        bool ok = true;
        std::string failMsg;

        // 内存级互斥锁 + 数据库原子扣减（双保险防超卖）
        std::lock_guard<std::mutex> lock(*ctx_.stockMutex);

        if (!j.contains("items") || j["items"].empty()) {
            ok = false; failMsg = "购物车为空";
            resp = makeError(ErrorCodes::CART_EMPTY, failMsg);
        }

        if (ok) {
            for (auto& item : j["items"]) {
                int pid  = item.value("product_id", 0);
                int qty  = item.value("quantity", 0);
                if (qty <= 0) { ok = false; failMsg = "数量不合法"; resp = makeError(ErrorCodes::STOCK_NOT_ENOUGH, failMsg); break; }
                auto prod = ctx_.products->findById(pid);
                if (!prod) { ok = false; failMsg = "商品不存在"; resp = makeError(ErrorCodes::PRODUCT_NOT_FOUND, failMsg); break; }
                // 原子扣减（条件 stock >= qty），失败即库存不足
                if (!ctx_.products->deductStock(pid, qty)) {
                    ok = false; failMsg = "库存不足"; resp = makeError(ErrorCodes::STOCK_NOT_ENOUGH, failMsg); break;
                }
                OrderItem oi;
                oi.productId   = pid;
                oi.productName = prod->name;   // 快照
                oi.price       = prod->price;  // 快照
                oi.quantity    = qty;
                order.items.push_back(oi);
                order.totalAmount += prod->price * qty;
            }
        }

        if (ok) {
            // ===== 促销链（策略+装饰+工厂） =====
            chain = PromoChainBuilder::build(promoIds, *ctx_.promoFactory);

            // VIP 会员折上折（具体装饰器演示：额外95折）
            if (user->level >= 2) {
                chain = std::make_unique<ExtraDiscountDecorator>(std::move(chain), 0.95);
            }

            double finalAmount = order.totalAmount;
            if (chain) {
                finalAmount = chain->apply(order.totalAmount);
                order.discountAmount = order.totalAmount - finalAmount;
            }
            order.finalAmount = finalAmount;
            order.orderNo = makeOrderNo();

            if (!ctx_.orders->insert(order)) {
                ok = false; failMsg = "订单保存失败";
                resp = makeError(ErrorCodes::SYSTEM_BUSY, failMsg);
            }
        }

        if (ok) {
            if (sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
                resp = makeError(ErrorCodes::SYSTEM_BUSY, "提交失败");
                return false;
            }
            // 观察者模式：广播订单创建事件
            OrderEventBus::instance().notifyOrderCreated(order);

            json out;
            out["order_id"]  = order.id;
            out["order_no"]  = order.orderNo;
            out["original"]  = order.totalAmount;
            out["discount"]  = order.discountAmount;
            out["final"]     = order.finalAmount;
            out["status"]    = "success";
            out["promo_desc"]= chain ? chain->getDescription() : "无";
            resp = out.dump();
            return true;
        } else {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            return false;
        }
    } catch (const std::exception& e) {
        char* errMsg = nullptr;
        sqlite3_exec(ctx_.db, "ROLLBACK;", nullptr, nullptr, &errMsg);
        resp = makeError(ErrorCodes::SYSTEM_BUSY, std::string("参数错误: ") + e.what());
        return false;
    }
}

// ---------------- 订单查询 ----------------
bool OrderQueryHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int userId = j.value("user_id", 0);
        auto orders = ctx_.orders->findByUserId(userId);
        json out;
        out["orders"] = json::array();
        for (const auto& o : orders) {
            json item;
            item["order_id"]  = o.id;
            item["order_no"]  = o.orderNo;
            item["original"]  = o.totalAmount;
            item["discount"]  = o.discountAmount;
            item["final"]     = o.finalAmount;
            item["status"]    = o.status;
            item["create_time"] = o.createTime;
            item["items"]     = json::array();
            for (const auto& it : o.items) {
                json ii;
                ii["product_id"]   = it.productId;
                ii["product_name"] = it.productName;
                ii["price"]        = it.price;
                ii["quantity"]     = it.quantity;
                item["items"].push_back(ii);
            }
            out["orders"].push_back(item);
        }
        resp = out.dump();
        return true;
    } catch (const std::exception& e) {
        resp = makeError(ErrorCodes::SYSTEM_BUSY, std::string("参数错误: ") + e.what());
        return false;
    }
}

// ---------------- 售后退货 ----------------
bool ReturnHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int userId  = j.value("user_id", 0);
        int orderId = j.value("order_id", 0);
        std::string reason = j.value("reason", "");

        auto order = ctx_.orders->findById(orderId);
        if (!order) { resp = makeError(ErrorCodes::ORDER_NOT_FOUND, "订单不存在"); return false; }
        if (order->userId != userId) {
            resp = makeError(ErrorCodes::ORDER_NOT_FOUND, "无权操作该订单");
            return false;
        }
        // 仅已支付/已发货/已完成状态可退货
        if (order->status != 1 && order->status != 2 && order->status != 3) {
            resp = makeError(ErrorCodes::ORDER_STATUS_FORBID, "订单状态不允许退货");
            return false;
        }

        ReturnRequest rr;
        rr.orderId = orderId;
        rr.userId  = userId;
        rr.reason  = reason;
        rr.status  = 0;   // 待审核
        if (!ctx_.returns->insert(rr)) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "退货申请失败");
            return false;
        }
        // 订单标记为退货中
        ctx_.orders->updateStatus(orderId, 5);

        json out;
        out["success"]   = true;
        out["return_id"] = rr.id;
        out["status"]    = 0;
        out["message"]   = "退货申请已提交，等待审核";
        resp = out.dump();
        return true;
    } catch (const std::exception& e) {
        resp = makeError(ErrorCodes::SYSTEM_BUSY, std::string("参数错误: ") + e.what());
        return false;
    }
}

// ---------------- 心跳 ----------------
bool HeartbeatHandler::execute(const std::string& req, std::string& resp) {
    json out;
    out["server_time"] = std::time(nullptr);
    try {
        auto j = json::parse(req);
        out["timestamp"] = j.value("timestamp", 0);
    } catch (...) {
        out["timestamp"] = 0;
    }
    resp = out.dump();
    return true;
}
