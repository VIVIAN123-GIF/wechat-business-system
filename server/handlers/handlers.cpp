#include "handlers.h"
#include "../Logger.h"
#include "../../common/ProtocolHeader.h"
#include "../../common/SHA256.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <ctime>
#include <random>
#include <map>
#include <set>
#include <unordered_map>
#include <sstream>
#include <iomanip>

using json = nlohmann::json;

std::string makeError(int errCode, const std::string& msg) {
    json j;
    j["err_code"] = errCode;
    j["err_msg"]  = msg;
    return j.dump();
}

// ---------------- 密码摘要与鉴权工具 ----------------
namespace {

// 密码归一化：已是 64 位 hex 摘要则视为协议摘要；否则视为明文并哈希（兼容老客户端）
bool isHexDigest(const std::string& s) {
    if (s.size() != 64) return false;
    return s.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos;
}

std::string normalizePassword(const std::string& username, const std::string& pwd) {
    if (isHexDigest(pwd)) return pwd;
    return hashPassword(username, pwd);
}

std::string getToken(const json& j) {
    if (j.contains("token") && j["token"].is_string()) return j["token"].get<std::string>();
    return "";
}

// 登录失败限流器：同一用户名 60 秒内失败 5 次则锁定 60 秒（防暴力破解）
struct LoginRateLimiter {
    std::mutex mtx;
    std::unordered_map<std::string, std::pair<int, std::chrono::steady_clock::time_point>> fails;
    static constexpr int MAX_FAILS = 5;
    static constexpr int WINDOW_SEC = 60;
    static constexpr int LOCK_SEC = 60;

    bool locked(const std::string& username) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = fails.find(username);
        if (it == fails.end()) return false;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.second).count();
        return it->second.first >= MAX_FAILS && elapsed < LOCK_SEC;
    }
    void recordFail(const std::string& username) {
        std::lock_guard<std::mutex> lock(mtx);
        auto& entry = fails[username];
        if (entry.first == 0) entry.second = std::chrono::steady_clock::now();
        ++entry.first;
    }
    void reset(const std::string& username) {
        std::lock_guard<std::mutex> lock(mtx);
        fails.erase(username);
    }
};
LoginRateLimiter& loginLimiter() { static LoginRateLimiter l; return l; }

} // namespace

namespace Auth {

bool requireLogin(HandlerContext& ctx, const json& j, int& outUserId, std::string& errMsg) {
    std::string token = getToken(j);
    if (token.empty()) { errMsg = "请先登录"; return false; }
    if (!ctx.sessions || !ctx.sessions->getUserId(token, outUserId)) {
        errMsg = "登录已失效，请重新登录"; return false;
    }
    return true;
}

bool requireUser(HandlerContext& ctx, const json& j, int userId, std::string& errMsg) {
    int boundUser = 0;
    if (!requireLogin(ctx, j, boundUser, errMsg)) return false;
    if (boundUser != userId) {
        errMsg = "无权操作他人账号"; return false;
    }
    return true;
}

bool requireAdmin(HandlerContext& ctx, const json& j, int& outUserId, std::string& errMsg) {
    if (!requireLogin(ctx, j, outUserId, errMsg)) return false;
    auto user = ctx.users->findById(outUserId);
    if (!user || user->level < 2) {
        errMsg = "仅管理员可执行此操作"; return false;
    }
    return true;
}

} // namespace Auth

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

// ---------------- 商品搜索 ----------------
bool SearchProductHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        std::string keyword = j.value("keyword", "");
        if (keyword.size() > 100) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "搜索关键词过长（最多100字）");
            return false;
        }
        auto products = ctx_.products->findByKeyword(keyword);
        json out;
        out["keyword"] = keyword;
        out["count"]   = static_cast<int>(products.size());
        out["products"] = json::array();
        for (const auto& p : products) {
            json item;
            item["id"]    = p.id;
            item["name"]  = p.name;
            item["price"] = p.price;
            item["stock"] = p.stock;
            item["category"] = p.category;
            out["products"].push_back(item);
        }
        resp = out.dump();
        LOG_INFO("商品搜索: \"" + keyword + "\" 命中 " +
                 std::to_string(products.size()) + " 条");
        return true;
    } catch (const std::exception& e) {
        resp = makeError(ErrorCodes::SYSTEM_BUSY, std::string("搜索参数错误: ") + e.what());
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
        // 频率限制：同一用户名连续失败 5 次锁定 60 秒
        if (loginLimiter().locked(username)) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "尝试次数过多，请60秒后再试");
            return false;
        }
        auto user = ctx_.users->findByUsername(username);
        // 密码比较：协议摘要（客户端 SHA-256）或明文（自动归一化）
        if (!user || user->password != normalizePassword(username, password)) {
            loginLimiter().recordFail(username);
            resp = makeError(ErrorCodes::USER_NOT_FOUND, "用户名或密码错误");
            return false;
        }
        loginLimiter().reset(username);
        ctx_.users->updateLastLogin(user->id);
        // 创建会话 token（后续业务请求凭 token 鉴权）
        std::string token = ctx_.sessions ? ctx_.sessions->create(user->id) : "";
        json out;
        out["success"] = true;
        out["user_id"] = user->id;
        out["username"] = user->username;
        out["level"]  = user->level;
        out["token"]  = token;
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
        // 密码摘要存储：明文自动哈希，摘要原样入库（杜绝明文落库）
        u.password = normalizePassword(username, password);
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
        int userId = j.value("user_id", 0);
        std::string authErr;
        if (!Auth::requireUser(ctx_, j, userId, authErr)) {
            resp = makeError(ErrorCodes::UNAUTHORIZED, authErr);
            return false;
        }
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
        item["param3"] = p.param3;
        item["param4"] = p.param4;
        item["param5"] = p.param5;
        item["param6"] = p.param6;
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
    // 毫秒 + 随机后缀：规避同毫秒并发撞 UNIQUE 约束
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count() % 1000;
    oss << std::setw(3) << std::setfill('0') << ms;
    static std::mt19937 rng{ std::random_device{}() };
    oss << std::setw(3) << std::setfill('0') << (rng() % 1000);
    return oss.str();
}

bool CheckoutHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int userId = j.value("user_id", 0);
        std::string authErr;
        if (!Auth::requireUser(ctx_, j, userId, authErr)) {
            resp = makeError(ErrorCodes::UNAUTHORIZED, authErr);
            return false;
        }
        auto user = ctx_.users->findById(userId);
        if (!user) { resp = makeError(ErrorCodes::USER_NOT_FOUND, "用户不存在"); return false; }

        std::vector<int> promoIds;
        if (j.contains("promo_ids")) {
            // 去重：防止同一促销重复叠加（如 0.9×0.9）
            std::set<int> uniqueSet;
            for (auto& id : j["promo_ids"]) {
                int pid = id.get<int>();
                if (uniqueSet.insert(pid).second) promoIds.push_back(pid);
            }
        }
        // 促销校验：不存在或已过期直接报错（不再静默跳过）
        for (int pid : promoIds) {
            auto record = ctx_.promos->findById(pid);
            if (!record || record->status != 1) {
                resp = makeError(ErrorCodes::PROMO_EXPIRED,
                                 "促销 #" + std::to_string(pid) + " 不存在或已过期");
                return false;
            }
        }

        Order order;
        order.userId = userId;
        order.status = ORDER_PAID;  // 已支付
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

            int finalAmount = order.totalAmount;
            if (chain) {
                // 明细感知：阶梯折扣等按"同一商品数量"计价，其余促销按总额计算（默认兼容）
                finalAmount = chain->applyToItems(order.totalAmount, order.items);
                order.discountAmount = order.totalAmount - finalAmount;
            }
            order.finalAmount = finalAmount;
            // 订单号生成 + 插入：UNIQUE 冲突时更换单号重试（最多 3 次）
            bool inserted = false;
            for (int attempt = 0; attempt < 3 && !inserted; ++attempt) {
                order.id = 0;
                order.orderNo = makeOrderNo();
                inserted = ctx_.orders->insert(order);
            }
            if (!inserted) {
                ok = false; failMsg = "订单保存失败，请重试";
                resp = makeError(ErrorCodes::SYSTEM_BUSY, failMsg);
            }
        }

        if (ok) {
            if (sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
                // COMMIT 失败必须回滚，否则事务悬空污染后续操作
                sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
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
        std::string authErr;
        if (!Auth::requireUser(ctx_, j, userId, authErr)) {
            resp = makeError(ErrorCodes::UNAUTHORIZED, authErr);
            return false;
        }
        auto orders = ctx_.orders->findByUserId(userId);
        // 附加退货状态：order_id -> 最新退货单状态（0 无退货）
        std::map<int, int> returnStatus;
        for (const auto& r : ctx_.returns->findByUserId(userId)) {
            if (returnStatus.find(r.orderId) == returnStatus.end()) {
                returnStatus[r.orderId] = r.status + 1;  // 1待审核 2已通过 3已拒绝
            }
        }
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
            item["return_status"] = returnStatus.count(o.id) ? returnStatus[o.id] : 0;
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
// 流程：校验订单归属与状态 → 事务内原子置为"退货中" → 写入退货单（记录原状态）
// 审核通过时回补库存并置订单"已取消"；拒绝时恢复订单原状态（见 ReturnAuditHandler）
bool ReturnHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int userId  = j.value("user_id", 0);
        int orderId = j.value("order_id", 0);
        std::string reason = j.value("reason", "");
        std::string authErr;
        if (!Auth::requireUser(ctx_, j, userId, authErr)) {
            resp = makeError(ErrorCodes::UNAUTHORIZED, authErr);
            return false;
        }

        sqlite3* db = ctx_.db;
        if (sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "系统繁忙，请稍后重试");
            return false;
        }

        auto order = ctx_.orders->findById(orderId);
        if (!order) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::ORDER_NOT_FOUND, "订单不存在");
            return false;
        }
        if (order->userId != userId) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::NO_PERMISSION, "无权操作该订单");
            return false;
        }
        // 仅已支付/已发货/已完成状态可退货
        if (order->status != ORDER_PAID && order->status != ORDER_SHIPPED && order->status != ORDER_COMPLETED) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::ORDER_STATUS_FORBID, "订单状态不允许退货");
            return false;
        }

        // 原子置为"退货中"（条件更新防并发重复申请）
        if (!ctx_.orders->updateStatusIf(orderId, ORDER_RETURNING, ORDER_PAID, ORDER_SHIPPED, ORDER_COMPLETED)) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::DUPLICATE_SUBMIT, "该订单已提交退货申请");
            return false;
        }

        ReturnRequest rr;
        rr.orderId = orderId;
        rr.userId  = userId;
        rr.reason  = reason;
        rr.status  = RETURN_PENDING;   // 待审核
        rr.origStatus = order->status; // 记录原状态，审核拒绝时恢复
        if (!ctx_.returns->insert(rr)) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "退货申请失败");
            return false;
        }

        if (sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "提交失败");
            return false;
        }

        json out;
        out["success"]   = true;
        out["return_id"] = rr.id;
        out["status"]    = 0;
        out["message"]   = "退货申请已提交，等待审核";
        resp = out.dump();
        return true;
    } catch (const std::exception& e) {
        sqlite3_exec(ctx_.db, "ROLLBACK;", nullptr, nullptr, nullptr);
        resp = makeError(ErrorCodes::SYSTEM_BUSY, std::string("参数错误: ") + e.what());
        return false;
    }
}

// ---------------- 退货单查询 ----------------
// 普通用户：仅返回自己的退货单；管理员（level>=2）：返回全部（含他人）
bool ReturnListHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int userId = 0;
        std::string authErr;
        if (!Auth::requireLogin(ctx_, j, userId, authErr)) {
            resp = makeError(ErrorCodes::UNAUTHORIZED, authErr);
            return false;
        }
        auto user = ctx_.users->findById(userId);
        bool isAdmin = user && user->level >= 2;
        auto list = isAdmin ? ctx_.returns->findAll() : ctx_.returns->findByUserId(userId);

        json out;
        out["returns"] = json::array();
        for (auto& r : list) {
            json item;
            item["return_id"]  = r.id;
            item["order_id"]   = r.orderId;
            item["user_id"]    = r.userId;
            item["reason"]     = r.reason;
            item["status"]     = r.status;      // 0待审核 1已通过 2已拒绝
            item["apply_time"] = r.applyTime;
            item["audit_time"] = r.auditTime;
            out["returns"].push_back(item);
        }
        resp = out.dump();
        return true;
    } catch (const std::exception& e) {
        resp = makeError(ErrorCodes::SYSTEM_BUSY, std::string("参数错误: ") + e.what());
        return false;
    }
}

// ---------------- 退货审核（管理员） ----------------
// 通过：回补库存 + 订单置"已取消(4)" + 退货单置"已通过(1)"
// 拒绝：订单恢复申请前状态 + 退货单置"已拒绝(2)"
bool ReturnAuditHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int returnId = j.value("return_id", 0);
        bool approve = j.value("approve", false);
        int adminId = 0;
        std::string authErr;
        if (!Auth::requireAdmin(ctx_, j, adminId, authErr)) {
            resp = makeError(ErrorCodes::UNAUTHORIZED, authErr);
            return false;
        }

        sqlite3* db = ctx_.db;
        if (sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", nullptr, nullptr, nullptr) != SQLITE_OK) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "系统繁忙，请稍后重试");
            return false;
        }

        auto rr = ctx_.returns->findById(returnId);
        if (!rr || rr->status != RETURN_PENDING) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::RETURN_NOT_FOUND, "退货单不存在或已审核");
            return false;
        }
        auto order = ctx_.orders->findById(rr->orderId);
        if (!order) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::ORDER_NOT_FOUND, "关联订单不存在");
            return false;
        }

        bool ok = true;
        std::string failMsg;
        if (approve) {
            // 通过：回补库存（订单明细快照逐项恢复）
            for (const auto& it : order->items) {
                if (!ctx_.products->restoreStock(it.productId, it.quantity)) {
                    ok = false; failMsg = "库存回补失败";
                    break;
                }
            }
            if (ok) ok = ctx_.orders->updateStatus(order->id, ORDER_CANCELLED);  // 已取消（已退款）
            if (ok) ok = ctx_.returns->updateStatus(returnId, RETURN_APPROVED);
        } else {
            // 拒绝：订单恢复申请前状态
            ok = ctx_.orders->updateStatus(order->id, rr->origStatus);
            if (ok) ok = ctx_.returns->updateStatus(returnId, RETURN_REJECTED);
        }

        if (!ok) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::SYSTEM_BUSY, failMsg.empty() ? "审核处理失败" : failMsg);
            return false;
        }
        if (sqlite3_exec(db, "COMMIT;", nullptr, nullptr, nullptr) != SQLITE_OK) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "提交失败");
            return false;
        }

        json out;
        out["success"]    = true;
        out["return_id"]  = returnId;
        out["status"]     = approve ? 1 : 2;
        out["message"]    = approve ? "退货已通过，库存已回补" : "退货已拒绝，订单状态已恢复";
        resp = out.dump();
        LOG_INFO(std::string("退货审核: 单号#") + std::to_string(returnId) +
                 (approve ? " 通过（库存回补）" : " 拒绝"));
        return true;
    } catch (const std::exception& e) {
        sqlite3_exec(ctx_.db, "ROLLBACK;", nullptr, nullptr, nullptr);
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

// ---------------- 销售数据统计（服务端 UI 层：数据统计分析界面） ----------------
// 请求: {"days":30, "product_id":0, "user_id":0}
//   days>0 限定最近 N 天；product_id>0 限定某商品；user_id>0 限定某用户
std::string SalesStatsHandler::buildFilter(int days, int productId, int userId,
                                           bool orderLevel) {
    std::string f = orderLevel ? " WHERE o.status != 4" : " WHERE status != 4";
    if (userId > 0) {
        f += orderLevel ? " AND o.user_id=" + std::to_string(userId)
                        : " AND user_id=" + std::to_string(userId);
    }
    if (days > 0) {
        f += orderLevel
            ? " AND o.create_time >= datetime('now','-" + std::to_string(days) + " days')"
            : " AND create_time >= datetime('now','-" + std::to_string(days) + " days')";
    }
    if (productId > 0 && orderLevel) {
        f += " AND EXISTS(SELECT 1 FROM order_items oi WHERE oi.order_id=o.id"
             " AND oi.product_id=" + std::to_string(productId) + ")";
    }
    return f;
}

bool SalesStatsHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        // 统计含全店销售数据，要求有效登录态
        int loginUserId = 0;
        std::string authErr;
        if (!Auth::requireLogin(ctx_, j, loginUserId, authErr)) {
            resp = makeError(ErrorCodes::UNAUTHORIZED, authErr);
            return false;
        }
        int days      = j.value("days", 0);
        int productId = j.value("product_id", 0);
        int userId    = j.value("user_id", 0);

        json out;
        out["summary"]    = json::object();
        out["by_day"]     = json::array();
        out["by_product"] = json::array();
        out["by_user"]    = json::array();

        // ---- 1. 汇总 ----
        {
            std::string sql = "SELECT COUNT(*), COALESCE(SUM(total_amount),0),"
                              " COALESCE(SUM(discount_amount),0),"
                              " COALESCE(SUM(final_amount),0),"
                              " COALESCE(AVG(final_amount),0)"
                              " FROM orders o" + buildFilter(days, productId, userId, true);
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(ctx_.db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK &&
                sqlite3_step(stmt) == SQLITE_ROW) {
                out["summary"]["order_count"]  = sqlite3_column_int(stmt, 0);
                out["summary"]["total_amount"] = sqlite3_column_double(stmt, 1);
                out["summary"]["discount"]     = sqlite3_column_double(stmt, 2);
                out["summary"]["final_amount"] = sqlite3_column_double(stmt, 3);
                out["summary"]["avg_order"]    = sqlite3_column_double(stmt, 4);
            }
            if (stmt) sqlite3_finalize(stmt);
            // 退货申请数（不受订单过滤，仅随区间）
            std::string rSql = "SELECT COUNT(*) FROM returns"
                             + buildFilter(days, 0, userId, false);
            stmt = nullptr;
            if (sqlite3_prepare_v2(ctx_.db, rSql.c_str(), -1, &stmt, nullptr) == SQLITE_OK &&
                sqlite3_step(stmt) == SQLITE_ROW) {
                out["summary"]["return_count"] = sqlite3_column_int(stmt, 0);
            }
            if (stmt) sqlite3_finalize(stmt);
        }

        // ---- 2. 按日销售趋势 ----
        {
            std::string sql = "SELECT date(create_time) AS d, COUNT(*),"
                              " COALESCE(SUM(final_amount),0)"
                              " FROM orders o" + buildFilter(days, productId, userId, true) +
                              " GROUP BY d ORDER BY d";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(ctx_.db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    json item;
                    item["date"]   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
                    item["orders"] = sqlite3_column_int(stmt, 1);
                    item["amount"] = sqlite3_column_double(stmt, 2);
                    out["by_day"].push_back(item);
                }
            }
            if (stmt) sqlite3_finalize(stmt);
        }

        // ---- 3. 热销商品排行（按销售额 Top10） ----
        {
            std::string sql =
                "SELECT oi.product_id, MAX(oi.product_name),"
                " SUM(oi.quantity), SUM(oi.price*oi.quantity)"
                " FROM order_items oi JOIN orders o ON oi.order_id = o.id"
                + buildFilter(days, productId, userId, true) +
                " GROUP BY oi.product_id ORDER BY SUM(oi.price*oi.quantity) DESC LIMIT 10";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(ctx_.db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    json item;
                    item["product_id"] = sqlite3_column_int(stmt, 0);
                    item["name"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                    item["quantity"] = sqlite3_column_int(stmt, 2);
                    item["amount"] = sqlite3_column_double(stmt, 3);
                    out["by_product"].push_back(item);
                }
            }
            if (stmt) sqlite3_finalize(stmt);
        }

        // ---- 4. 用户贡献排行（按实收金额 Top10） ----
        {
            std::string sql =
                "SELECT o.user_id, MAX(u.username), COUNT(*), COALESCE(SUM(o.final_amount),0)"
                " FROM orders o JOIN users u ON o.user_id = u.id"
                + buildFilter(days, productId, userId, true) +
                " GROUP BY o.user_id ORDER BY SUM(o.final_amount) DESC LIMIT 10";
            sqlite3_stmt* stmt = nullptr;
            if (sqlite3_prepare_v2(ctx_.db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW) {
                    json item;
                    item["user_id"]  = sqlite3_column_int(stmt, 0);
                    item["username"] = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
                    item["orders"]   = sqlite3_column_int(stmt, 2);
                    item["amount"]   = sqlite3_column_double(stmt, 3);
                    out["by_user"].push_back(item);
                }
            }
            if (stmt) sqlite3_finalize(stmt);
        }

        resp = out.dump();

        // 服务端控制台同步输出（服务端 UI 层职责：交易结果/统计展示）
        std::ostringstream log;
        log << "销售统计(近" << (days > 0 ? std::to_string(days) + "天" : "全部")
            << (userId > 0 ? ", 用户" + std::to_string(userId) : "")
            << (productId > 0 ? ", 商品" + std::to_string(productId) : "")
            << "): 订单=" << out["summary"]["order_count"]
            << " 实收=" << std::fixed << std::setprecision(2)
            << out["summary"]["final_amount"]
            << " 优惠=" << out["summary"]["discount"];
        LOG_INFO(log.str());
        return true;
    } catch (const std::exception& e) {
        resp = makeError(ErrorCodes::SYSTEM_BUSY, std::string("统计参数错误: ") + e.what());
        return false;
    }
}

// ---------------- 客服消息（社交：联系客服） ----------------
// 最简无状态实现：客户端发消息 -> 服务端控制台回显 + 自动应答
bool ServiceMsgHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int userId = j.value("user_id", 0);
        std::string authErr;
        if (!Auth::requireUser(ctx_, j, userId, authErr)) {
            resp = makeError(ErrorCodes::UNAUTHORIZED, authErr);
            return false;
        }
        std::string content = j.value("content", "");
        if (content.empty()) {
            resp = makeError(ErrorCodes::USER_NOT_FOUND, "消息内容不能为空");
            return false;
        }
        // 消息长度上限（防日志注入/刷屏）
        if (content.size() > 500) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "消息过长（最多500字）");
            return false;
        }
        // 取用户名（未知则显示用户ID）
        std::string who = "用户#" + std::to_string(userId);
        if (userId > 0) {
            auto u = ctx_.users->findById(userId);
            if (u) who = u->username;
        }
        // 服务端控制台回显（客服侧可见）
        LOG_INFO("[客服消息] " + who + ": " + content);

        // 自动应答（模拟客服回复，避免长连接状态管理）
        std::string reply = "客服小助手：已收到您的消息，我们会尽快处理（工单已登记）。";
        json out;
        out["success"]  = true;
        out["received"] = content;
        out["reply"]    = reply;
        resp = out.dump();
        return true;
    } catch (const std::exception& e) {
        resp = makeError(ErrorCodes::SYSTEM_BUSY, std::string("消息参数错误: ") + e.what());
        return false;
    }
}
