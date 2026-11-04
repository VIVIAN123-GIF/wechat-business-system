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
    j["success"] = false;
    j["err_code"] = errCode;
    j["err_msg"]  = msg;
    j["error"] = {{"code", errCode}, {"message", msg}};
    j["data"] = json::object();
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

namespace {
struct ExplainResult {
    int amount = 0;
    std::vector<int> ids;
    std::string description;
};

bool parseItems(const json& j, HandlerContext& ctx, std::vector<OrderItem>& items,
                int& total, std::string& error) {
    if (!j.contains("items") || !j["items"].is_array() || j["items"].empty()) {
        error = "商品明细不能为空";
        return false;
    }
    for (const auto& raw : j["items"]) {
        if (!raw.is_object() || !raw.value("product_id", 0) || raw.value("quantity", 0) <= 0) {
            error = "商品明细参数不合法";
            return false;
        }
        auto product = ctx.products->findById(raw.value("product_id", 0));
        if (!product) { error = "商品不存在"; return false; }
        OrderItem item;
        item.productId = product->id;
        item.productName = product->name;
        item.price = product->price;
        item.quantity = raw.value("quantity", 0);
        items.push_back(item);
        total += item.price * item.quantity;
    }
    return true;
}

ExplainResult evaluatePromos(const std::vector<int>& ids, const std::vector<OrderItem>& items,
                             int total, PromotionFactory& factory) {
    ExplainResult result;
    result.amount = total;
    result.ids = ids;
    std::unique_ptr<IPromotion> chain;
    for (int id : ids) {
        auto promo = factory.create(id);
        if (!promo) continue;
        if (!chain) chain = std::move(promo);
        else chain = std::make_unique<CompositeDecorator>(std::move(chain), std::move(promo));
    }
    if (chain) {
        result.amount = chain->applyToItems(total, items);
        result.description = chain->getDescription();
    }
    return result;
}

ExplainResult evaluatePromoRecords(const std::vector<const Promotion*>& records,
                                   const std::vector<OrderItem>& items, int total,
                                   PromotionFactory& factory) {
    ExplainResult result;
    result.amount = total;
    std::unique_ptr<IPromotion> chain;
    for (const Promotion* record : records) {
        if (!record) continue;
        auto promo = factory.createFromRecord(*record);
        if (!promo) continue;
        result.ids.push_back(record->id);
        if (!chain) chain = std::move(promo);
        else chain = std::make_unique<CompositeDecorator>(std::move(chain), std::move(promo));
    }
    if (chain) {
        result.amount = chain->applyToItems(total, items);
        result.description = chain->getDescription();
    }
    return result;
}

void expireGroups(HandlerContext& ctx) {
    if (!ctx.db || !ctx.stockMutex) return;
    std::lock_guard<std::mutex> lock(*ctx.stockMutex);
    if (sqlite3_exec(ctx.db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) return;
    struct ExpiredGroup { int id; int productId; int memberCount; };
    std::vector<ExpiredGroup> expired;
    sqlite3_stmt* stmt = nullptr;
    const char* query = "SELECT id,product_id,member_count FROM groups WHERE status=0 AND deadline<=datetime('now')";
    bool ok = sqlite3_prepare_v2(ctx.db, query, -1, &stmt, nullptr) == SQLITE_OK;
    while (ok && sqlite3_step(stmt) == SQLITE_ROW) {
        expired.push_back({sqlite3_column_int(stmt, 0), sqlite3_column_int(stmt, 1), sqlite3_column_int(stmt, 2)});
    }
    if (stmt) sqlite3_finalize(stmt);
    for (const auto& group : expired) {
        if (!ctx.products->restoreStock(group.productId, group.memberCount)) { ok = false; break; }
        sqlite3_stmt* update = nullptr;
        if (sqlite3_prepare_v2(ctx.db, "UPDATE groups SET status=2 WHERE id=? AND status=0", -1, &update, nullptr) != SQLITE_OK) { ok = false; break; }
        sqlite3_bind_int(update, 1, group.id);
        ok = sqlite3_step(update) == SQLITE_DONE;
        sqlite3_finalize(update);
    }
    sqlite3_exec(ctx.db, ok ? "COMMIT" : "ROLLBACK", nullptr, nullptr, nullptr);
}

void appendAudit(sqlite3* db, int orderId, int operatorId, const std::string& action,
                 int fromStatus, int toStatus, const std::string& detail) {
    const char* sql = "INSERT INTO order_audit_logs(order_id,operator_id,action,from_status,to_status,detail) VALUES(?,?,?,?,?,?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int(stmt, 1, orderId);
    sqlite3_bind_int(stmt, 2, operatorId);
    sqlite3_bind_text(stmt, 3, action.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, fromStatus);
    sqlite3_bind_int(stmt, 5, toStatus);
    sqlite3_bind_text(stmt, 6, detail.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

bool createGroupOrders(HandlerContext& ctx, int groupId, int productId, int unitPrice) {
    auto product = ctx.products->findById(productId);
    if (!product) return false;

    std::vector<int> memberIds;
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(ctx.db, "SELECT user_id FROM group_members WHERE group_id=? ORDER BY id", -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, groupId);
    while (sqlite3_step(stmt) == SQLITE_ROW) memberIds.push_back(sqlite3_column_int(stmt, 0));
    sqlite3_finalize(stmt);

    for (int memberId : memberIds) {
        Order order;
        order.orderNo = "GP" + std::to_string(groupId) + "-" + std::to_string(memberId);
        order.userId = memberId;
        order.totalAmount = unitPrice;
        order.finalAmount = unitPrice;
        order.status = ORDER_PAID;
        OrderItem item;
        item.productId = product->id;
        item.productName = product->name;
        item.price = unitPrice;
        item.quantity = 1;
        order.items.push_back(item);
        if (!ctx.orders->insert(order)) return false;

        const char* linkSql = "INSERT INTO group_orders(group_id,user_id,order_id) VALUES(?,?,?)";
        if (sqlite3_prepare_v2(ctx.db, linkSql, -1, &stmt, nullptr) != SQLITE_OK) return false;
        sqlite3_bind_int(stmt, 1, groupId);
        sqlite3_bind_int(stmt, 2, memberId);
        sqlite3_bind_int(stmt, 3, order.id);
        bool linked = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        if (!linked) return false;
        appendAudit(ctx.db, order.id, memberId, "group_completed", -1, ORDER_PAID, "拼团成功并生成订单");
    }
    return !memberIds.empty();
}
}

bool PromoExplainHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int userId = j.value("user_id", 0);
        std::string authErr;
        if (!Auth::requireUser(ctx_, j, userId, authErr)) { resp = makeError(ErrorCodes::UNAUTHORIZED, authErr); return false; }
        std::vector<OrderItem> items; int total = 0; std::string error;
        if (!parseItems(j, ctx_, items, total, error)) { resp = makeError(ErrorCodes::CART_EMPTY, error); return false; }
        std::vector<int> currentIds;
        if (j.contains("promo_ids") && j["promo_ids"].is_array()) {
            for (const auto& id : j["promo_ids"]) {
                if (!id.is_number_integer()) { resp = makeError(ErrorCodes::PROMO_EXPIRED, "促销编号必须是整数"); return false; }
                int promoId = id.get<int>();
                if (std::find(currentIds.begin(), currentIds.end(), promoId) != currentIds.end()) continue;
                if (!ctx_.promoFactory->create(promoId)) { resp = makeError(ErrorCodes::PROMO_EXPIRED, "促销不存在或已失效"); return false; }
                currentIds.push_back(promoId);
            }
        }
        auto current = evaluatePromos(currentIds, items, total, *ctx_.promoFactory);
        const auto available = ctx_.promos->findAllAvailable();
        ExplainResult best = evaluatePromos({}, items, total, *ctx_.promoFactory);
        const size_t limit = std::min<size_t>(available.size(), 12);
        const unsigned long long combinations = 1ULL << limit;
        for (unsigned long long mask = 1; mask < combinations; ++mask) {
            std::vector<const Promotion*> candidate;
            for (size_t i = 0; i < limit; ++i) if (mask & (1ULL << i)) candidate.push_back(&available[i]);
            auto evaluated = evaluatePromoRecords(candidate, items, total, *ctx_.promoFactory);
            if (evaluated.amount < best.amount) best = std::move(evaluated);
        }
        json out;
        out["original"] = total;
        out["current"] = {{"amount", current.amount}, {"discount", total - current.amount},
                           {"promo_ids", current.ids}, {"description", current.description}};
        out["best"] = {{"amount", best.amount}, {"discount", total - best.amount},
                        {"promo_ids", best.ids}, {"description", best.description}};
        out["saving_vs_current"] = current.amount - best.amount;
        out["lines"] = json::array();
        int currentAllocated = 0;
        int bestAllocated = 0;
        for (size_t index = 0; index < items.size(); ++index) {
            const auto& item = items[index];
            int original = item.price * item.quantity;
            int currentDiscount = total > 0 ? (total - current.amount) * original / total : 0;
            int bestDiscount = total > 0 ? (total - best.amount) * original / total : 0;
            if (index + 1 == items.size()) {
                currentDiscount = (total - current.amount) - currentAllocated;
                bestDiscount = (total - best.amount) - bestAllocated;
            }
            currentAllocated += currentDiscount;
            bestAllocated += bestDiscount;
            out["lines"].push_back({{"product_id", item.productId}, {"name", item.productName},
                                     {"quantity", item.quantity}, {"original", original},
                                     {"current_discount", currentDiscount}, {"current_amount", original - currentDiscount},
                                     {"best_discount", bestDiscount}, {"best_amount", original - bestDiscount}});
        }
        out["tips"] = json::array();
        for (const auto& p : available) {
            if (p.type == 4 && p.param3 > 0 && p.param3 > items.front().quantity && p.param4 <= 0.0) {
                out["tips"].push_back("再购买 " + std::to_string(static_cast<int>(p.param3 - items.front().quantity)) + " 件可触发第3阶梯免单");
            }
        }
        resp = out.dump();
        return true;
    } catch (const std::exception& e) { resp = makeError(ErrorCodes::SYSTEM_BUSY, e.what()); return false; }
}

bool GroupCreateHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req); int userId = j.value("user_id", 0); std::string authErr;
        if (!Auth::requireUser(ctx_, j, userId, authErr)) { resp = makeError(ErrorCodes::UNAUTHORIZED, authErr); return false; }
        int productId = j.value("product_id", 0), target = j.value("target_count", 2), hours = j.value("hours", 24);
        auto product = ctx_.products->findById(productId);
        if (target < 2 || target > 100 || hours <= 0 || hours > 168 || !product) {
            resp = makeError(ErrorCodes::PRODUCT_NOT_FOUND, "拼团参数不合法"); return false;
        }
        std::lock_guard<std::mutex> stockLock(*ctx_.stockMutex);
        if (sqlite3_exec(ctx_.db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "拼团繁忙"); return false;
        }
        if (!ctx_.products->deductStock(productId, 1)) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::STOCK_NOT_ENOUGH, "拼团商品库存不足"); return false;
        }
        std::string groupNo = "G" + std::to_string(std::chrono::system_clock::now().time_since_epoch().count());
        const char* sql = "INSERT INTO groups(group_no,product_id,creator_id,unit_price,target_count,member_count,status,deadline) VALUES(?,?,?,?,?,1,0,datetime('now', ?))";
        sqlite3_stmt* stmt = nullptr; if (sqlite3_prepare_v2(ctx_.db, sql, -1, &stmt, nullptr) != SQLITE_OK) { sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr); resp = makeError(ErrorCodes::SYSTEM_BUSY, "拼团创建失败"); return false; }
        std::string modifier = "+" + std::to_string(hours) + " hours";
        sqlite3_bind_text(stmt, 1, groupNo.c_str(), -1, SQLITE_TRANSIENT); sqlite3_bind_int(stmt, 2, productId); sqlite3_bind_int(stmt, 3, userId); sqlite3_bind_int(stmt, 4, product->price); sqlite3_bind_int(stmt, 5, target); sqlite3_bind_text(stmt, 6, modifier.c_str(), -1, SQLITE_TRANSIENT);
        bool ok = sqlite3_step(stmt) == SQLITE_DONE; sqlite3_finalize(stmt); if (!ok) { sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr); resp = makeError(ErrorCodes::SYSTEM_BUSY, "拼团创建失败"); return false; }
        int groupId = static_cast<int>(sqlite3_last_insert_rowid(ctx_.db));
        const char* memberSql = "INSERT INTO group_members(group_id,user_id) VALUES(?,?)";
        if (sqlite3_prepare_v2(ctx_.db, memberSql, -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "拼团成员创建失败");
            return false;
        }
        sqlite3_bind_int(stmt, 1, groupId);
        sqlite3_bind_int(stmt, 2, userId);
        bool memberInserted = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        if (!memberInserted) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "拼团成员创建失败");
            return false;
        }
        if (sqlite3_exec(ctx_.db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "拼团创建提交失败");
            return false;
        }
        resp = json({{"success", true}, {"group_id", groupId}, {"group_no", groupNo}, {"status", 0}, {"member_count", 1}, {"target_count", target}}).dump(); return true;
    } catch (const std::exception& e) { resp = makeError(ErrorCodes::SYSTEM_BUSY, e.what()); return false; }
}

bool GroupJoinHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req); int userId=j.value("user_id",0); std::string authErr;
        if (!Auth::requireUser(ctx_, j, userId, authErr)) { resp=makeError(ErrorCodes::UNAUTHORIZED,authErr); return false; }
        int groupId=j.value("group_id",0); char* err=nullptr;
        std::lock_guard<std::mutex> stockLock(*ctx_.stockMutex);
        if (sqlite3_exec(ctx_.db,"BEGIN IMMEDIATE",nullptr,nullptr,&err)!=SQLITE_OK) { resp=makeError(ErrorCodes::SYSTEM_BUSY,"拼团繁忙"); return false; }
        sqlite3_stmt* stmt=nullptr; const char* q="SELECT product_id,target_count,member_count,status,unit_price FROM groups WHERE id=? AND deadline>datetime('now')";
        if (sqlite3_prepare_v2(ctx_.db,q,-1,&stmt,nullptr)!=SQLITE_OK) { sqlite3_exec(ctx_.db,"ROLLBACK",nullptr,nullptr,nullptr); resp=makeError(ErrorCodes::SYSTEM_BUSY,"拼团不存在"); return false; }
        sqlite3_bind_int(stmt,1,groupId); bool found=sqlite3_step(stmt)==SQLITE_ROW; int productId=found?sqlite3_column_int(stmt,0):0; int target=found?sqlite3_column_int(stmt,1):0; int members=found?sqlite3_column_int(stmt,2):0; int status=found?sqlite3_column_int(stmt,3):1; int unitPrice=found?sqlite3_column_int(stmt,4):0; sqlite3_finalize(stmt);
        if (!found || status != 0 || members >= target) { sqlite3_exec(ctx_.db,"ROLLBACK",nullptr,nullptr,nullptr); resp=makeError(ErrorCodes::ORDER_STATUS_FORBID,"拼团已结束或已满"); return false; }
        const char* member="INSERT INTO group_members(group_id,user_id) VALUES(?,?)"; if (sqlite3_prepare_v2(ctx_.db,member,-1,&stmt,nullptr)!=SQLITE_OK) { sqlite3_exec(ctx_.db,"ROLLBACK",nullptr,nullptr,nullptr); resp=makeError(ErrorCodes::SYSTEM_BUSY,"加入失败"); return false; } sqlite3_bind_int(stmt,1,groupId); sqlite3_bind_int(stmt,2,userId); bool inserted=sqlite3_step(stmt)==SQLITE_DONE; sqlite3_finalize(stmt);
        if (!inserted || !ctx_.products->deductStock(productId,1)) { sqlite3_exec(ctx_.db,"ROLLBACK",nullptr,nullptr,nullptr); resp=makeError(ErrorCodes::STOCK_NOT_ENOUGH,"库存不足或已加入"); return false; }
        const char* update="UPDATE groups SET member_count=member_count+1,status=CASE WHEN member_count+1>=target_count THEN 1 ELSE 0 END WHERE id=?";
        if (sqlite3_prepare_v2(ctx_.db, update, -1, &stmt, nullptr) != SQLITE_OK) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "拼团状态更新失败");
            return false;
        }
        sqlite3_bind_int(stmt, 1, groupId);
        bool groupUpdated = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(ctx_.db) == 1;
        sqlite3_finalize(stmt);
        if (groupUpdated && members + 1 >= target) {
            groupUpdated = createGroupOrders(ctx_, groupId, productId, unitPrice);
        }
        if (!groupUpdated || sqlite3_exec(ctx_.db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "拼团状态更新失败");
            return false;
        }
        resp=json({{"success",true},{"group_id",groupId},{"joined",true},{"member_count",members+1},{"target_count",target},{"status",members+1>=target?1:0}}).dump(); return true;
    } catch (const std::exception& e) { sqlite3_exec(ctx_.db,"ROLLBACK",nullptr,nullptr,nullptr); resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what()); return false; }
}

bool GroupQueryHandler::execute(const std::string& req, std::string& resp) {
    try { expireGroups(ctx_); auto j=json::parse(req); int userId=j.value("user_id",0); std::string authErr; if(!Auth::requireUser(ctx_,j,userId,authErr)){resp=makeError(ErrorCodes::UNAUTHORIZED,authErr);return false;} const char* q="SELECT id,group_no,product_id,target_count,member_count,status,deadline FROM groups WHERE status=0 AND deadline>datetime('now') ORDER BY id DESC"; sqlite3_stmt* s=nullptr; json out=json::array(); if(sqlite3_prepare_v2(ctx_.db,q,-1,&s,nullptr)==SQLITE_OK){while(sqlite3_step(s)==SQLITE_ROW) out.push_back({{"group_id",sqlite3_column_int(s,0)},{"group_no",reinterpret_cast<const char*>(sqlite3_column_text(s,1))},{"product_id",sqlite3_column_int(s,2)},{"target_count",sqlite3_column_int(s,3)},{"member_count",sqlite3_column_int(s,4)},{"status",sqlite3_column_int(s,5)},{"deadline",reinterpret_cast<const char*>(sqlite3_column_text(s,6))}});sqlite3_finalize(s);} resp=json({{"groups",out}}).dump();return true;} catch(const std::exception&e){resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what());return false;}
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
            if (!j["promo_ids"].is_array()) {
                resp = makeError(ErrorCodes::PROMO_EXPIRED, "promo_ids 必须是数组");
                return false;
            }
            // 去重：防止同一促销重复叠加（如 0.9×0.9）
            std::set<int> uniqueSet;
            for (auto& id : j["promo_ids"]) {
                if (!id.is_number_integer()) {
                    resp = makeError(ErrorCodes::PROMO_EXPIRED, "促销 ID 必须是整数");
                    return false;
                }
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
        std::lock_guard<std::mutex> lock(*ctx_.stockMutex);

        // ===== 事务开始（BEGIN IMMEDIATE 避免读写锁升级死锁） =====
        char* errMsg = nullptr;
        if (sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "系统繁忙，请稍后重试");
            return false;
        }

        bool ok = true;
        std::string failMsg;

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
            } else {
                appendAudit(db, order.id, userId, "create_order", -1, order.status, "订单创建并完成结算");
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
        // 订单时间由 SQLite 以 UTC 写入，因此也由 SQLite 计算时间差，避免本地时区偏移。
        sqlite3_stmt* ageStmt = nullptr;
        const char* ageSql = "SELECT (julianday('now') - julianday(?)) * 86400.0";
        bool withinReturnWindow = false;
        if (sqlite3_prepare_v2(db, ageSql, -1, &ageStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(ageStmt, 1, order->createTime.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(ageStmt) == SQLITE_ROW) {
                double ageSeconds = sqlite3_column_double(ageStmt, 0);
                withinReturnWindow = ageSeconds >= 0.0 && ageSeconds <= 7.0 * 24.0 * 3600.0;
            }
        }
        if (ageStmt) sqlite3_finalize(ageStmt);
        if (!withinReturnWindow) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::ORDER_STATUS_FORBID, "已超过7天退货期限");
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
        appendAudit(db, orderId, userId, "apply_return", order->status, ORDER_RETURNING, "提交退货申请");

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
            if (ok) appendAudit(db, order->id, adminId, "approve_return", ORDER_RETURNING, ORDER_CANCELLED, "退货审核通过并回补库存");
        } else {
            // 拒绝：订单恢复申请前状态
            ok = ctx_.orders->updateStatus(order->id, rr->origStatus);
            if (ok) ok = ctx_.returns->updateStatus(returnId, RETURN_REJECTED);
            if (ok) appendAudit(db, order->id, adminId, "reject_return", ORDER_RETURNING, rr->origStatus, "退货审核拒绝并恢复订单状态");
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
bool OrderStatusHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req); int adminId = 0; std::string authErr;
        if (!Auth::requireAdmin(ctx_, j, adminId, authErr)) { resp = makeError(ErrorCodes::UNAUTHORIZED, authErr); return false; }
        int orderId = j.value("order_id", 0), target = j.value("status", -1);
        auto order = ctx_.orders->findById(orderId);
        if (!order || (target != ORDER_SHIPPED && target != ORDER_COMPLETED)) { resp = makeError(ErrorCodes::ORDER_NOT_FOUND, "订单或目标状态不合法"); return false; }
        bool allowed = (order->status == ORDER_PAID && target == ORDER_SHIPPED) || (order->status == ORDER_SHIPPED && target == ORDER_COMPLETED);
        if (!allowed || !ctx_.orders->updateStatusIf(orderId, target, order->status, order->status, order->status)) { resp = makeError(ErrorCodes::ORDER_STATUS_FORBID, "不允许的订单状态转换"); return false; }
        appendAudit(ctx_.db, orderId, adminId, "change_status", order->status, target, "管理员状态流转");
        resp = json({{"success", true}, {"order_id", orderId}, {"from_status", order->status}, {"status", target}}).dump(); return true;
    } catch (const std::exception& e) { resp = makeError(ErrorCodes::SYSTEM_BUSY, e.what()); return false; }
}

bool OrderAuditHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req); int userId = j.value("user_id", 0); std::string authErr;
        if (!Auth::requireUser(ctx_, j, userId, authErr)) { resp = makeError(ErrorCodes::UNAUTHORIZED, authErr); return false; }
        int orderId = j.value("order_id", 0); auto order = ctx_.orders->findById(orderId); auto viewer = ctx_.users->findById(userId);
        if (!order || (order->userId != userId && (!viewer || viewer->level < 2))) { resp = makeError(ErrorCodes::NO_PERMISSION, "无权查看该订单审计记录"); return false; }
        const char* sql = "SELECT id,operator_id,action,from_status,to_status,detail,created_at FROM order_audit_logs WHERE order_id=? ORDER BY id";
        sqlite3_stmt* stmt=nullptr; json logs=json::array();
        if (sqlite3_prepare_v2(ctx_.db, sql, -1, &stmt, nullptr)==SQLITE_OK) { sqlite3_bind_int(stmt,1,orderId); while(sqlite3_step(stmt)==SQLITE_ROW) logs.push_back({{"id",sqlite3_column_int(stmt,0)},{"operator_id",sqlite3_column_int(stmt,1)},{"action",reinterpret_cast<const char*>(sqlite3_column_text(stmt,2))},{"from_status",sqlite3_column_int(stmt,3)},{"to_status",sqlite3_column_int(stmt,4)},{"detail",reinterpret_cast<const char*>(sqlite3_column_text(stmt,5))},{"created_at",reinterpret_cast<const char*>(sqlite3_column_text(stmt,6))}}); sqlite3_finalize(stmt); }
        resp=json({{"order_id",orderId},{"audit",logs}}).dump(); return true;
    } catch(const std::exception& e){resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what());return false;}
}

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
