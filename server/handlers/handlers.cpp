// ============================================================
// handlers.cpp - 各业务 Handler 实现（命令模式，按协议码分发）
// ============================================================
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
#include <tuple>

using json = nlohmann::json;

void appendNotification(sqlite3* db, int userId, const std::string& type, const std::string& content);

namespace {
std::string safeText(sqlite3_stmt* stmt, int column) {
    const auto* value = sqlite3_column_text(stmt, column);
    return value ? reinterpret_cast<const char*>(value) : std::string();
}
}

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
    if (!user || (user->role != "admin" && user->role != "operator")) {
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
        out["reviews"] = json::array();
        out["review_count"] = 0;
        out["review_average"] = 0.0;
        sqlite3_stmt* reviewStmt = nullptr;
        const char* reviewSql = "SELECT r.rating,r.content,r.image_url,r.created_at,u.username FROM reviews r JOIN users u ON u.id=r.user_id WHERE r.product_id=? ORDER BY r.id DESC";
        if (sqlite3_prepare_v2(ctx_.db, reviewSql, -1, &reviewStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(reviewStmt, 1, pid);
            int totalRating = 0;
            while (sqlite3_step(reviewStmt) == SQLITE_ROW) {
                int rating = sqlite3_column_int(reviewStmt, 0);
                totalRating += rating;
                out["reviews"].push_back({{"rating", rating}, {"content", safeText(reviewStmt, 1)}, {"image_url", safeText(reviewStmt, 2)}, {"created_at", safeText(reviewStmt, 3)}, {"username", safeText(reviewStmt, 4)}});
            }
            sqlite3_finalize(reviewStmt);
            out["review_count"] = out["reviews"].size();
            out["review_average"] = out["reviews"].empty() ? 0.0 : static_cast<double>(totalRating) / out["reviews"].size();
        }
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
        int minPrice = std::max(0, j.value("min_price", 0));
        int maxPrice = j.value("max_price", 0);
        std::string category = j.value("category", "");
        std::string sort = j.value("sort", "");
        products.erase(std::remove_if(products.begin(), products.end(), [minPrice, maxPrice, &category](const Product& p) {
            return p.price < minPrice || (maxPrice > 0 && p.price > maxPrice) || (!category.empty() && p.category != category);
        }), products.end());
        if (sort == "price_asc") std::sort(products.begin(), products.end(), [](const Product& a, const Product& b) { return a.price < b.price; });
        else if (sort == "price_desc") std::sort(products.begin(), products.end(), [](const Product& a, const Product& b) { return a.price > b.price; });
        else if (sort == "stock_desc") std::sort(products.begin(), products.end(), [](const Product& a, const Product& b) { return a.stock > b.stock; });
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
        out["role"]   = user->role;
        out["points"] = user->points;
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
        int inviterId = 0;
        std::string inviteCode = j.value("invite_code", "");
        if (!inviteCode.empty()) {
            sqlite3_stmt* inviterStmt = nullptr;
            bool valid = sqlite3_prepare_v2(ctx_.db,
                "SELECT id FROM users WHERE invite_code=?", -1, &inviterStmt, nullptr) == SQLITE_OK;
            if (valid) {
                sqlite3_bind_text(inviterStmt, 1, inviteCode.c_str(), -1, SQLITE_TRANSIENT);
                valid = sqlite3_step(inviterStmt) == SQLITE_ROW;
                if (valid) inviterId = sqlite3_column_int(inviterStmt, 0);
            }
            if (inviterStmt) sqlite3_finalize(inviterStmt);
            if (!valid || inviterId <= 0) {
                resp = makeError(ErrorCodes::USER_NOT_FOUND, "invalid invite code");
                return false;
            }
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
        sqlite3_stmt* inviterStmt = nullptr;
        if (!inviteCode.empty() && sqlite3_prepare_v2(ctx_.db,
                "SELECT id FROM users WHERE invite_code=?", -1, &inviterStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(inviterStmt, 1, inviteCode.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(inviterStmt) == SQLITE_ROW) inviterId = sqlite3_column_int(inviterStmt, 0);
        }
        if (inviterStmt) sqlite3_finalize(inviterStmt);
        if (inviterId == u.id) inviterId = 0;
        std::string generatedCode = "U" + std::to_string(u.id) + "M" + std::to_string((u.id * 7919) % 100000);
        sqlite3_stmt* relationStmt = nullptr;
        if (sqlite3_prepare_v2(ctx_.db,
                "UPDATE users SET inviter_id=?, invite_code=? WHERE id=?",
                -1, &relationStmt, nullptr) == SQLITE_OK) {
            if (inviterId > 0) sqlite3_bind_int(relationStmt, 1, inviterId);
            else sqlite3_bind_null(relationStmt, 1);
            sqlite3_bind_text(relationStmt, 2, generatedCode.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(relationStmt, 3, u.id);
            sqlite3_step(relationStmt);
        }
        if (relationStmt) sqlite3_finalize(relationStmt);
        json out;
        out["success"] = true;
        out["user_id"] = u.id;
        out["invite_code"] = generatedCode;
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
        sqlite3_stmt* cartStmt = nullptr;
        sqlite3_exec(ctx_.db, "BEGIN IMMEDIATE;", nullptr, nullptr, nullptr);
        bool saved = sqlite3_prepare_v2(ctx_.db,
            "INSERT INTO cart_items(user_id,product_id,sku_id,quantity) VALUES(?,?,?,?) "
            "ON CONFLICT(user_id,product_id,sku_id) DO UPDATE SET quantity=quantity+excluded.quantity,updated_at=datetime('now')",
            -1, &cartStmt, nullptr) == SQLITE_OK;
        if (saved) {
            sqlite3_bind_int(cartStmt, 1, userId);
            sqlite3_bind_int(cartStmt, 2, pid);
            sqlite3_bind_int(cartStmt, 3, j.value("sku_id", 0));
            sqlite3_bind_int(cartStmt, 4, qty);
            saved = sqlite3_step(cartStmt) == SQLITE_DONE;
        }
        if (cartStmt) sqlite3_finalize(cartStmt);
        if (saved) saved = sqlite3_exec(ctx_.db, "COMMIT;", nullptr, nullptr, nullptr) == SQLITE_OK;
        else sqlite3_exec(ctx_.db, "ROLLBACK;", nullptr, nullptr, nullptr);
        if (!saved) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "购物车保存失败");
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
namespace {
bool cartAuth(const json& j, HandlerContext& ctx, int& userId, int& productId, int& skuId, std::string& error) {
    userId = j.value("user_id", 0); productId = j.value("product_id", 0); skuId = j.value("sku_id", 0);
    if (!Auth::requireUser(ctx, j, userId, error)) return false;
    if (productId <= 0 || skuId < 0) { error = "购物车商品参数无效"; return false; }
    return true;
}

bool cartSku(sqlite3* db, int productId, int skuId, int& price, int& stock, std::string& label) {
    if (skuId <= 0) return true;
    sqlite3_stmt* s = nullptr;
    bool ok = sqlite3_prepare_v2(db, "SELECT color,size,price_delta,stock,status FROM product_skus WHERE id=? AND product_id=?", -1, &s, nullptr) == SQLITE_OK;
    if (ok) {
        sqlite3_bind_int(s, 1, skuId); sqlite3_bind_int(s, 2, productId);
        ok = sqlite3_step(s) == SQLITE_ROW && sqlite3_column_int(s, 4) == 1;
        if (ok) { price += sqlite3_column_int(s, 2); stock = sqlite3_column_int(s, 3); label = safeText(s, 0) + " / " + safeText(s, 1); }
    }
    if (s) sqlite3_finalize(s);
    return ok;
}
}

bool CartListHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j=json::parse(req); int userId=j.value("user_id",0); std::string error;
        if(!Auth::requireUser(ctx_,j,userId,error)){resp=makeError(ErrorCodes::UNAUTHORIZED,error);return false;}
        sqlite3_stmt* s=nullptr; if(sqlite3_prepare_v2(ctx_.db,"SELECT product_id,sku_id,quantity FROM cart_items WHERE user_id=? ORDER BY id",-1,&s,nullptr)!=SQLITE_OK){resp=makeError(ErrorCodes::SYSTEM_BUSY,"购物车查询失败");return false;}
        sqlite3_bind_int(s, 1, userId);
        std::vector<std::tuple<int,int,int>> rows;
        while(sqlite3_step(s)==SQLITE_ROW) rows.emplace_back(sqlite3_column_int(s,0), sqlite3_column_int(s,1), sqlite3_column_int(s,2));
        sqlite3_finalize(s);
        json items=json::array();
        for(const auto& row: rows){int pid=std::get<0>(row),sid=std::get<1>(row),qty=std::get<2>(row);auto p=ctx_.products->findById(pid);int price=p?p->price:0,stock=p?p->stock:0;std::string label;bool valid=p&&p->status==1&&cartSku(ctx_.db,pid,sid,price,stock,label);if(!valid||qty<=0){sqlite3_stmt* d=nullptr;if(sqlite3_prepare_v2(ctx_.db,"DELETE FROM cart_items WHERE user_id=? AND product_id=? AND sku_id=?",-1,&d,nullptr)==SQLITE_OK){sqlite3_bind_int(d,1,userId);sqlite3_bind_int(d,2,pid);sqlite3_bind_int(d,3,sid);sqlite3_step(d);}if(d)sqlite3_finalize(d);continue;}items.push_back({{"product_id",pid},{"sku_id",sid},{"sku_label",label},{"name",p->name},{"price",price},{"quantity",qty},{"subtotal",price*qty}});}
        resp=json({{"success",true},{"items",items}}).dump();return true;
    } catch(const std::exception& e){resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what());return false;}
}

bool CartSetQuantityHandler::execute(const std::string& req, std::string& resp) {
    try {auto j=json::parse(req);int uid=0,pid=0,sid=0;std::string error;if(!cartAuth(j,ctx_,uid,pid,sid,error)){resp=makeError(ErrorCodes::UNAUTHORIZED,error);return false;}int qty=j.value("quantity",0);if(qty<=0){sqlite3_stmt*s=nullptr;bool ok=sqlite3_prepare_v2(ctx_.db,"DELETE FROM cart_items WHERE user_id=? AND product_id=? AND sku_id=?",-1,&s,nullptr)==SQLITE_OK;if(ok){sqlite3_bind_int(s,1,uid);sqlite3_bind_int(s,2,pid);sqlite3_bind_int(s,3,sid);ok=sqlite3_step(s)==SQLITE_DONE;}if(s)sqlite3_finalize(s);if(!ok){resp=makeError(ErrorCodes::SYSTEM_BUSY,"购物车更新失败");return false;}resp=json({{"success",true},{"quantity",0}}).dump();return true;}auto p=ctx_.products->findById(pid);int price=p?p->price:0,stock=p?p->stock:0;std::string label;if(!p||p->status!=1||!cartSku(ctx_.db,pid,sid,price,stock,label)){resp=makeError(ErrorCodes::PRODUCT_NOT_FOUND,"商品或规格不存在");return false;}if(qty>stock){resp=makeError(ErrorCodes::STOCK_NOT_ENOUGH,"库存不足");return false;}sqlite3_stmt*s=nullptr;bool ok=sqlite3_prepare_v2(ctx_.db,"INSERT INTO cart_items(user_id,product_id,sku_id,quantity) VALUES(?,?,?,?) ON CONFLICT(user_id,product_id,sku_id) DO UPDATE SET quantity=excluded.quantity,updated_at=datetime('now')",-1,&s,nullptr)==SQLITE_OK;if(ok){sqlite3_bind_int(s,1,uid);sqlite3_bind_int(s,2,pid);sqlite3_bind_int(s,3,sid);sqlite3_bind_int(s,4,qty);ok=sqlite3_step(s)==SQLITE_DONE;}if(s)sqlite3_finalize(s);if(!ok){resp=makeError(ErrorCodes::SYSTEM_BUSY,"购物车更新失败");return false;}resp=json({{"success",true},{"quantity",qty}}).dump();return true;}catch(const std::exception&e){resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what());return false;}
}

bool CartRemoveHandler::execute(const std::string& req, std::string& resp) {
    try{auto j=json::parse(req);int uid=0,pid=0,sid=0;std::string error;if(!cartAuth(j,ctx_,uid,pid,sid,error)){resp=makeError(ErrorCodes::UNAUTHORIZED,error);return false;}sqlite3_stmt*s=nullptr;bool ok=sqlite3_prepare_v2(ctx_.db,"DELETE FROM cart_items WHERE user_id=? AND product_id=? AND sku_id=?",-1,&s,nullptr)==SQLITE_OK;if(ok){sqlite3_bind_int(s,1,uid);sqlite3_bind_int(s,2,pid);sqlite3_bind_int(s,3,sid);ok=sqlite3_step(s)==SQLITE_DONE;}if(s)sqlite3_finalize(s);if(!ok){resp=makeError(ErrorCodes::SYSTEM_BUSY,"购物车删除失败");return false;}resp=json({{"success",true}}).dump();return true;}catch(const std::exception&e){resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what());return false;}
}

bool CartClearHandler::execute(const std::string& req, std::string& resp) {
    try{auto j=json::parse(req);int uid=j.value("user_id",0);std::string error;if(!Auth::requireUser(ctx_,j,uid,error)){resp=makeError(ErrorCodes::UNAUTHORIZED,error);return false;}sqlite3_stmt*s=nullptr;bool ok=sqlite3_prepare_v2(ctx_.db,"DELETE FROM cart_items WHERE user_id=?",-1,&s,nullptr)==SQLITE_OK;if(ok){sqlite3_bind_int(s,1,uid);ok=sqlite3_step(s)==SQLITE_DONE;}if(s)sqlite3_finalize(s);if(!ok){resp=makeError(ErrorCodes::SYSTEM_BUSY,"购物车清空失败");return false;}resp=json({{"success",true}}).dump();return true;}catch(const std::exception&e){resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what());return false;}
}

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
        item["exclusive_group"] = p.exclusiveGroup;
        item["max_uses"] = p.maxUses;
        item["per_user_limit"] = p.perUserLimit;
        item["start_time"] = p.startTime;
        item["end_time"] = p.endTime;
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
    std::set<std::string> exclusiveGroups;
    for (const Promotion* record : records) {
        if (!record) continue;
        if (!record->exclusiveGroup.empty() && !exclusiveGroups.insert(record->exclusiveGroup).second) continue;
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
    if (!ctx.db || !ctx.products || !ctx.stockMutex) return;
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
        order.status = ORDER_PENDING;
        sqlite3_stmt* addressStmt = nullptr;
        if (sqlite3_prepare_v2(ctx.db,
                "SELECT receiver_name,phone,detail FROM addresses WHERE user_id=? ORDER BY is_default DESC,id DESC LIMIT 1",
                -1, &addressStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(addressStmt, 1, memberId);
            if (sqlite3_step(addressStmt) == SQLITE_ROW) {
                order.addressReceiver = safeText(addressStmt, 0);
                order.addressPhone = safeText(addressStmt, 1);
                order.addressDetail = safeText(addressStmt, 2);
            }
        }
        if (addressStmt) sqlite3_finalize(addressStmt);
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
        appendNotification(ctx.db, memberId, "group", "拼团成功，订单已生成，请完成支付");
        appendAudit(ctx.db, order.id, memberId, "group_completed", -1, ORDER_PENDING, "拼团成功并生成待支付订单");
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

void expireGroupsNow(HandlerContext& ctx) {
    expireGroups(ctx);
}

void expirePendingOrdersNow(HandlerContext& ctx) {
    if (!ctx.db || !ctx.products || !ctx.orders || !ctx.stockMutex) return;
    std::lock_guard<std::mutex> lock(*ctx.stockMutex);
    if (sqlite3_exec(ctx.db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) return;
    bool ok = true;
    sqlite3_stmt* stmt = nullptr;
    std::vector<int> expiredIds;
    if (sqlite3_prepare_v2(ctx.db,
            "SELECT id FROM orders WHERE status=0 AND create_time <= datetime('now','-30 minutes')",
            -1, &stmt, nullptr) != SQLITE_OK) {
        ok = false;
    } else {
        while (sqlite3_step(stmt) == SQLITE_ROW) expiredIds.push_back(sqlite3_column_int(stmt, 0));
    }
    if (stmt) sqlite3_finalize(stmt);

    for (int orderId : expiredIds) {
        bool groupOrder = false;
        if (sqlite3_prepare_v2(ctx.db, "SELECT 1 FROM group_orders WHERE order_id=? LIMIT 1", -1, &stmt, nullptr) != SQLITE_OK) {
            ok = false;
            break;
        }
        sqlite3_bind_int(stmt, 1, orderId);
        groupOrder = sqlite3_step(stmt) == SQLITE_ROW;
        sqlite3_finalize(stmt);
        if (groupOrder) {
            auto order = ctx.orders->findById(orderId);
            if (!order) { ok = false; break; }
            for (const auto& item : order->items) {
                bool restored = false;
                if (item.skuId > 0) {
                    if (sqlite3_prepare_v2(ctx.db,
                            "UPDATE product_skus SET stock=stock+? WHERE id=? AND product_id=?",
                            -1, &stmt, nullptr) == SQLITE_OK) {
                        sqlite3_bind_int(stmt, 1, item.quantity);
                        sqlite3_bind_int(stmt, 2, item.skuId);
                        sqlite3_bind_int(stmt, 3, item.productId);
                        restored = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(ctx.db) == 1;
                    }
                } else {
                    restored = ctx.products && ctx.products->restoreStock(item.productId, item.quantity);
                }
                if (stmt) sqlite3_finalize(stmt);
                stmt = nullptr;
                if (!restored) { ok = false; break; }
            }
            if (!ok) break;
        }
        if (sqlite3_prepare_v2(ctx.db,
                "UPDATE orders SET status=4 WHERE id=? AND status=0", -1, &stmt, nullptr) != SQLITE_OK) {
            ok = false;
            break;
        }
        sqlite3_bind_int(stmt, 1, orderId);
        ok = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(ctx.db) == 1;
        sqlite3_finalize(stmt);
        stmt = nullptr;
        if (!ok) break;

        if (sqlite3_prepare_v2(ctx.db,
                "UPDATE user_coupons SET status=0,used_at=NULL WHERE id=(SELECT coupon_id FROM orders WHERE id=?) AND status=2",
                -1, &stmt, nullptr) != SQLITE_OK) {
            ok = false;
            break;
        }
        sqlite3_bind_int(stmt, 1, orderId);
        ok = sqlite3_step(stmt) == SQLITE_DONE;
        sqlite3_finalize(stmt);
        stmt = nullptr;
        if (!ok) break;
    }
    if (ok) sqlite3_exec(ctx.db, "COMMIT", nullptr, nullptr, nullptr);
    else sqlite3_exec(ctx.db, "ROLLBACK", nullptr, nullptr, nullptr);
}

void appendNotification(sqlite3* db, int userId, const std::string& type, const std::string& content) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO notifications(user_id,type,content) VALUES(?,?,?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_int(stmt, 1, userId);
    sqlite3_bind_text(stmt, 2, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

void appendAdminNotification(sqlite3* db, const std::string& type, const std::string& content) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO notifications(user_id,type,content) SELECT id,?,? FROM users WHERE role IN ('admin','operator')";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return;
    sqlite3_bind_text(stmt, 1, type.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, content.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

bool appendInventoryAudit(sqlite3* db, int productId, int skuId, int operatorId,
                          const std::string& action, int beforeValue, int afterValue,
                          const std::string& detail) {
    sqlite3_stmt* stmt = nullptr;
    const char* sql = "INSERT INTO inventory_audit_logs(product_id,sku_id,operator_id,action,before_value,after_value,detail) VALUES(?,?,?,?,?,?,?)";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    if (productId > 0) sqlite3_bind_int(stmt, 1, productId); else sqlite3_bind_null(stmt, 1);
    if (skuId > 0) sqlite3_bind_int(stmt, 2, skuId); else sqlite3_bind_null(stmt, 2);
    sqlite3_bind_int(stmt, 3, operatorId);
    sqlite3_bind_text(stmt, 4, action.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, beforeValue);
    sqlite3_bind_int(stmt, 6, afterValue);
    sqlite3_bind_text(stmt, 7, detail.c_str(), -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool PaymentHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int userId = j.value("user_id", 0), orderId = j.value("order_id", 0);
        std::string authErr;
        if (!Auth::requireUser(ctx_, j, userId, authErr)) { resp = makeError(ErrorCodes::UNAUTHORIZED, authErr); return false; }
        std::lock_guard<std::mutex> lock(*ctx_.stockMutex);
        if (sqlite3_exec(ctx_.db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "支付繁忙，请稍后重试"); return false;
        }
        auto order = ctx_.orders->findById(orderId);
        if (!order || order->userId != userId) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::ORDER_NOT_FOUND, "订单不存在"); return false;
        }
        if (order->status != ORDER_PENDING) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::ORDER_STATUS_FORBID, "订单不是待支付状态"); return false;
        }
        if (order->addressReceiver.empty() || order->addressPhone.empty() || order->addressDetail.empty()) {
            sqlite3_stmt* addressStmt = nullptr;
            bool addressFound = sqlite3_prepare_v2(ctx_.db,
                "SELECT receiver_name,phone,detail FROM addresses WHERE user_id=? ORDER BY is_default DESC,id DESC LIMIT 1",
                -1, &addressStmt, nullptr) == SQLITE_OK;
            if (addressFound) {
                sqlite3_bind_int(addressStmt, 1, userId);
                addressFound = sqlite3_step(addressStmt) == SQLITE_ROW;
                if (addressFound) {
                    order->addressReceiver = safeText(addressStmt, 0);
                    order->addressPhone = safeText(addressStmt, 1);
                    order->addressDetail = safeText(addressStmt, 2);
                }
            }
            if (addressStmt) sqlite3_finalize(addressStmt);
            if (!addressFound) {
                sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
                resp = makeError(ErrorCodes::SYSTEM_BUSY, "请先设置收货地址"); return false;
            }
            sqlite3_stmt* addressUpdate = nullptr;
            bool addressUpdated = sqlite3_prepare_v2(ctx_.db,
                "UPDATE orders SET address_receiver=?,address_phone=?,address_detail=? WHERE id=? AND status=?",
                -1, &addressUpdate, nullptr) == SQLITE_OK;
            if (addressUpdated) {
                sqlite3_bind_text(addressUpdate, 1, order->addressReceiver.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(addressUpdate, 2, order->addressPhone.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(addressUpdate, 3, order->addressDetail.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(addressUpdate, 4, orderId);
                sqlite3_bind_int(addressUpdate, 5, ORDER_PENDING);
                addressUpdated = sqlite3_step(addressUpdate) == SQLITE_DONE && sqlite3_changes(ctx_.db) == 1;
            }
            if (addressUpdate) sqlite3_finalize(addressUpdate);
            if (!addressUpdated) {
                sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
                resp = makeError(ErrorCodes::SYSTEM_BUSY, "收货地址保存失败"); return false;
            }
        }
        bool groupStockReserved = false;
        sqlite3_stmt* groupStmt = nullptr;
        if (sqlite3_prepare_v2(ctx_.db,
                "SELECT 1 FROM group_orders WHERE order_id=? LIMIT 1",
                -1, &groupStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(groupStmt, 1, orderId);
            groupStockReserved = sqlite3_step(groupStmt) == SQLITE_ROW;
        }
        if (groupStmt) sqlite3_finalize(groupStmt);
        for (const auto& item : order->items) {
            if (groupStockReserved) break;
            bool stockOk = false;
            if (item.skuId > 0) {
                sqlite3_stmt* sku = nullptr;
                if (sqlite3_prepare_v2(ctx_.db, "UPDATE product_skus SET stock=stock-? WHERE id=? AND product_id=? AND status=1 AND stock>=?", -1, &sku, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int(sku, 1, item.quantity); sqlite3_bind_int(sku, 2, item.skuId);
                    sqlite3_bind_int(sku, 3, item.productId); sqlite3_bind_int(sku, 4, item.quantity);
                    stockOk = sqlite3_step(sku) == SQLITE_DONE && sqlite3_changes(ctx_.db) == 1;
                }
                if (sku) sqlite3_finalize(sku);
            } else {
                stockOk = ctx_.products->deductStock(item.productId, item.quantity);
            }
            if (!stockOk) {
                sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
                resp = makeError(ErrorCodes::STOCK_NOT_ENOUGH, "商品库存不足，支付已回滚"); return false;
            }
        }
        sqlite3_stmt* stmt = nullptr;
        const char* sql = "UPDATE orders SET status=?, pay_time=datetime('now') WHERE id=? AND user_id=? AND status=?";
        bool updated = sqlite3_prepare_v2(ctx_.db, sql, -1, &stmt, nullptr) == SQLITE_OK;
        if (updated) {
            sqlite3_bind_int(stmt, 1, ORDER_PAID); sqlite3_bind_int(stmt, 2, orderId);
            sqlite3_bind_int(stmt, 3, userId); sqlite3_bind_int(stmt, 4, ORDER_PENDING);
            updated = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(ctx_.db) == 1;
        }
        if (stmt) sqlite3_finalize(stmt);
        if (!updated) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::ORDER_STATUS_FORBID, "订单状态已变化"); return false;
        }
        appendAudit(ctx_.db, orderId, userId, "pay_order", ORDER_PENDING, ORDER_PAID, "模拟支付成功");
        std::string transactionNo = "MOCK-" + order->orderNo + "-" + std::to_string(orderId);
        sqlite3_stmt* paymentStmt = nullptr;
        bool paymentRecorded = sqlite3_prepare_v2(ctx_.db,
            "INSERT INTO payment_records(order_id,user_id,transaction_no,amount) VALUES(?,?,?,?)",
            -1, &paymentStmt, nullptr) == SQLITE_OK;
        if (paymentRecorded) {
            sqlite3_bind_int(paymentStmt, 1, orderId);
            sqlite3_bind_int(paymentStmt, 2, userId);
            sqlite3_bind_text(paymentStmt, 3, transactionNo.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(paymentStmt, 4, order->finalAmount);
            paymentRecorded = sqlite3_step(paymentStmt) == SQLITE_DONE;
        }
        if (paymentStmt) sqlite3_finalize(paymentStmt);
        if (!paymentRecorded) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "支付流水保存失败");
            return false;
        }
        if (order->pointsUsed > 0) {
            sqlite3_stmt* pointsStmt = nullptr;
            bool pointsOk = sqlite3_prepare_v2(ctx_.db,
                "UPDATE users SET points=points-? WHERE id=? AND points>=?",
                -1, &pointsStmt, nullptr) == SQLITE_OK;
            if (pointsOk) {
                sqlite3_bind_int(pointsStmt, 1, order->pointsUsed);
                sqlite3_bind_int(pointsStmt, 2, userId);
                sqlite3_bind_int(pointsStmt, 3, order->pointsUsed);
                pointsOk = sqlite3_step(pointsStmt) == SQLITE_DONE && sqlite3_changes(ctx_.db) == 1;
            }
            if (pointsStmt) sqlite3_finalize(pointsStmt);
            if (!pointsOk) {
                sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
                resp = makeError(ErrorCodes::SYSTEM_BUSY, "积分余额不足，支付已回滚");
                return false;
            }
            sqlite3_stmt* pointLog = nullptr;
            if (sqlite3_prepare_v2(ctx_.db,
                "INSERT INTO point_transactions(user_id,order_id,amount,balance_after,reason) "
                "SELECT ?,?,?,points,'订单抵扣' FROM users WHERE id=?",
                -1, &pointLog, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(pointLog, 1, userId);
                sqlite3_bind_int(pointLog, 2, orderId);
                sqlite3_bind_int(pointLog, 3, -order->pointsUsed);
                sqlite3_bind_int(pointLog, 4, userId);
                sqlite3_step(pointLog);
            }
            if (pointLog) sqlite3_finalize(pointLog);
        }
        constexpr int COMMISSION_RATE_BPS = 500;
        int commissionBase = std::max(0, order->finalAmount - order->shippingFee);
        sqlite3_stmt* commissionStmt = nullptr;
        if (sqlite3_prepare_v2(ctx_.db,
                "INSERT INTO commission_records(inviter_id,invitee_id,order_id,amount) "
                "SELECT inviter_id,?,?,CAST(? * ? / 10000 AS INTEGER) FROM users "
                "WHERE id=? AND inviter_id IS NOT NULL AND inviter_id<>?",
                -1, &commissionStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(commissionStmt, 1, userId);
            sqlite3_bind_int(commissionStmt, 2, orderId);
            sqlite3_bind_int(commissionStmt, 3, commissionBase);
            sqlite3_bind_int(commissionStmt, 4, COMMISSION_RATE_BPS);
            sqlite3_bind_int(commissionStmt, 5, userId);
            sqlite3_bind_int(commissionStmt, 6, userId);
            sqlite3_step(commissionStmt);
        }
        if (commissionStmt) sqlite3_finalize(commissionStmt);
        if (order->couponId > 0) {
            sqlite3_stmt* coupon = nullptr;
            bool couponOk = sqlite3_prepare_v2(ctx_.db, "UPDATE user_coupons SET status=1,used_at=datetime('now') WHERE id=? AND user_id=? AND status=2", -1, &coupon, nullptr) == SQLITE_OK;
            if (couponOk) { sqlite3_bind_int(coupon, 1, order->couponId); sqlite3_bind_int(coupon, 2, userId); couponOk = sqlite3_step(coupon) == SQLITE_DONE && sqlite3_changes(ctx_.db) == 1; }
            if (coupon) sqlite3_finalize(coupon);
            if (!couponOk) { sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr); resp=makeError(ErrorCodes::PROMO_EXPIRED,"优惠券已失效"); return false; }
        }
        appendNotification(ctx_.db, userId, "order", "订单支付成功");
        // 同步通知所有管理员/客服：有新订单支付，请及时发货
        sqlite3_stmt* adminStmt = nullptr;
        if (sqlite3_prepare_v2(ctx_.db,
                "SELECT id FROM users WHERE role IN ('admin','operator') AND id<>?",
                -1, &adminStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(adminStmt, 1, userId);
            while (sqlite3_step(adminStmt) == SQLITE_ROW) {
                appendNotification(ctx_.db, sqlite3_column_int(adminStmt, 0),
                    "order", "新订单已支付（订单号 " + order->orderNo + "），请及时处理发货");
            }
        }
        if (adminStmt) sqlite3_finalize(adminStmt);
        sqlite3_exec(ctx_.db, ("UPDATE user_coupons SET status=1,used_at=datetime('now') WHERE id IN (SELECT coupon_id FROM order_coupons WHERE order_id=" + std::to_string(orderId) + ") AND user_id=" + std::to_string(userId) + " AND status=2").c_str(), nullptr, nullptr, nullptr);
        if (sqlite3_exec(ctx_.db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "支付提交失败"); return false;
        }
        resp = json({{"success", true}, {"order_id", orderId}, {"status", ORDER_PAID}, {"message", "支付成功"}}).dump();
        return true;
    } catch (const std::exception& e) {
        sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
        resp = makeError(ErrorCodes::SYSTEM_BUSY, e.what()); return false;
    }
}

bool CancelOrderHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int userId = j.value("user_id", 0), orderId = j.value("order_id", 0);
        std::string authErr;
        if (!Auth::requireUser(ctx_, j, userId, authErr)) {
            resp = makeError(ErrorCodes::UNAUTHORIZED, authErr);
            return false;
        }
        if (orderId <= 0) {
            resp = makeError(ErrorCodes::ORDER_NOT_FOUND, "订单不存在");
            return false;
        }
        std::lock_guard<std::mutex> stockLock(*ctx_.stockMutex);
        if (sqlite3_exec(ctx_.db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "取消订单繁忙，请稍后重试");
            return false;
        }

        auto order = ctx_.orders->findById(orderId);
        if (!order || order->userId != userId) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::ORDER_NOT_FOUND, "订单不存在");
            return false;
        }
        if (order->status != ORDER_PENDING) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::ORDER_STATUS_FORBID, "只有待支付订单可以取消");
            return false;
        }

        bool groupOrder = false;
        sqlite3_stmt* groupStmt = nullptr;
        if (sqlite3_prepare_v2(ctx_.db,
                "SELECT 1 FROM group_orders WHERE order_id=? LIMIT 1",
                -1, &groupStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(groupStmt, 1, orderId);
            groupOrder = sqlite3_step(groupStmt) == SQLITE_ROW;
        }
        if (groupStmt) sqlite3_finalize(groupStmt);
        if (groupOrder) {
            for (const auto& item : order->items) {
                bool restored = false;
                if (item.skuId > 0) {
                    sqlite3_stmt* skuStmt = nullptr;
                    if (sqlite3_prepare_v2(ctx_.db,
                            "UPDATE product_skus SET stock=stock+? WHERE id=? AND product_id=?",
                            -1, &skuStmt, nullptr) == SQLITE_OK) {
                        sqlite3_bind_int(skuStmt, 1, item.quantity);
                        sqlite3_bind_int(skuStmt, 2, item.skuId);
                        sqlite3_bind_int(skuStmt, 3, item.productId);
                        restored = sqlite3_step(skuStmt) == SQLITE_DONE && sqlite3_changes(ctx_.db) == 1;
                    }
                    if (skuStmt) sqlite3_finalize(skuStmt);
                } else {
                    restored = ctx_.products && ctx_.products->restoreStock(item.productId, item.quantity);
                }
                if (!restored) {
                    sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
                    resp = makeError(ErrorCodes::SYSTEM_BUSY, "库存回补失败，订单未取消");
                    return false;
                }
            }
            sqlite3_stmt* unlinkStmt = nullptr;
            bool unlinked = sqlite3_prepare_v2(ctx_.db,
                "DELETE FROM group_orders WHERE order_id=?", -1, &unlinkStmt, nullptr) == SQLITE_OK;
            if (unlinked) {
                sqlite3_bind_int(unlinkStmt, 1, orderId);
                unlinked = sqlite3_step(unlinkStmt) == SQLITE_DONE && sqlite3_changes(ctx_.db) == 1;
            }
            if (unlinkStmt) sqlite3_finalize(unlinkStmt);
            if (!unlinked) {
                sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
                resp = makeError(ErrorCodes::SYSTEM_BUSY, "拼团关联清理失败，订单未取消");
                return false;
            }
        }

        sqlite3_stmt* stmt = nullptr;
        bool updated = sqlite3_prepare_v2(ctx_.db,
            "UPDATE orders SET status=? WHERE id=? AND user_id=? AND status=?",
            -1, &stmt, nullptr) == SQLITE_OK;
        if (updated) {
            sqlite3_bind_int(stmt, 1, ORDER_CANCELLED);
            sqlite3_bind_int(stmt, 2, orderId);
            sqlite3_bind_int(stmt, 3, userId);
            sqlite3_bind_int(stmt, 4, ORDER_PENDING);
            updated = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(ctx_.db) == 1;
        }
        if (stmt) sqlite3_finalize(stmt);
        if (!updated) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::ORDER_STATUS_FORBID, "订单状态已发生变化");
            return false;
        }

        if (order->couponId > 0) {
            stmt = nullptr;
            bool released = sqlite3_prepare_v2(ctx_.db,
                "UPDATE user_coupons SET status=0 WHERE id=? AND user_id=? AND status=2",
                -1, &stmt, nullptr) == SQLITE_OK;
            if (released) {
                sqlite3_bind_int(stmt, 1, order->couponId);
                sqlite3_bind_int(stmt, 2, userId);
                released = sqlite3_step(stmt) == SQLITE_DONE && sqlite3_changes(ctx_.db) == 1;
            }
            if (stmt) sqlite3_finalize(stmt);
            if (!released) {
                sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
                resp = makeError(ErrorCodes::PROMO_EXPIRED, "优惠券释放失败，订单未取消");
                return false;
            }
        }

        appendAudit(ctx_.db, orderId, userId, "cancel_order", ORDER_PENDING,
                    ORDER_CANCELLED, "用户取消待支付订单");
        appendNotification(ctx_.db, userId, "order", "待支付订单已取消");
        if (sqlite3_exec(ctx_.db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "取消订单提交失败");
            return false;
        }
        resp = json({{"success", true}, {"order_id", orderId},
                     {"status", ORDER_CANCELLED}, {"message", "订单已取消"}}).dump();
        return true;
    } catch (const std::exception& e) {
        sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
        resp = makeError(ErrorCodes::SYSTEM_BUSY, e.what());
        return false;
    }
}

bool ConfirmReceiptHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int userId = j.value("user_id", 0), orderId = j.value("order_id", 0);
        std::string authErr;
        if (!Auth::requireUser(ctx_, j, userId, authErr)) { resp = makeError(ErrorCodes::UNAUTHORIZED, authErr); return false; }
        if (sqlite3_exec(ctx_.db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "确认收货繁忙");
            return false;
        }
        auto order = ctx_.orders->findById(orderId);
        if (!order || order->userId != userId) { sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr); resp = makeError(ErrorCodes::ORDER_NOT_FOUND, "订单不存在"); return false; }
        if (!ctx_.orders->updateStatusIf(orderId, ORDER_COMPLETED, ORDER_SHIPPED, -1, -1)) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::ORDER_STATUS_FORBID, "只有已发货订单可以确认收货"); return false;
        }
        appendAudit(ctx_.db, orderId, userId, "confirm_receipt", ORDER_SHIPPED, ORDER_COMPLETED, "买家确认收货");
        appendNotification(ctx_.db, userId, "order", "订单已确认收货");
        sqlite3_stmt* receiveStmt = nullptr;
        bool receiveUpdated = sqlite3_prepare_v2(ctx_.db,
            "UPDATE orders SET receive_time=datetime('now') WHERE id=? AND status=?",
            -1, &receiveStmt, nullptr) == SQLITE_OK;
        if (receiveUpdated) {
            sqlite3_bind_int(receiveStmt, 1, orderId);
            sqlite3_bind_int(receiveStmt, 2, ORDER_COMPLETED);
            receiveUpdated = sqlite3_step(receiveStmt) == SQLITE_DONE && sqlite3_changes(ctx_.db) == 1;
        }
        if (receiveStmt) sqlite3_finalize(receiveStmt);
        if (!receiveUpdated) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "签收时间保存失败");
            return false;
        }
        if (sqlite3_exec(ctx_.db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "确认收货提交失败");
            return false;
        }
        resp = json({{"success", true}, {"order_id", orderId}, {"status", ORDER_COMPLETED}, {"message", "确认收货成功"}}).dump();
        return true;
    } catch (const std::exception& e) { sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr); resp = makeError(ErrorCodes::SYSTEM_BUSY, e.what()); return false; }
}

bool AddressListHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req); int userId = j.value("user_id", 0); std::string authErr;
        if (!Auth::requireUser(ctx_, j, userId, authErr)) { resp = makeError(ErrorCodes::UNAUTHORIZED, authErr); return false; }
        sqlite3_stmt* stmt = nullptr; json addresses = json::array();
        const char* sql = "SELECT id,receiver_name,phone,detail,is_default FROM addresses WHERE user_id=? ORDER BY is_default DESC,id DESC";
        if (sqlite3_prepare_v2(ctx_.db, sql, -1, &stmt, nullptr) != SQLITE_OK) { resp = makeError(ErrorCodes::SYSTEM_BUSY, "地址查询失败"); return false; }
        sqlite3_bind_int(stmt, 1, userId);
        while (sqlite3_step(stmt) == SQLITE_ROW) addresses.push_back({{"id",sqlite3_column_int(stmt,0)}, {"receiver_name",safeText(stmt,1)}, {"phone",safeText(stmt,2)}, {"detail",safeText(stmt,3)}, {"is_default",sqlite3_column_int(stmt,4) != 0}});
        sqlite3_finalize(stmt); resp = json({{"addresses", addresses}}).dump(); return true;
    } catch (const std::exception& e) { resp = makeError(ErrorCodes::SYSTEM_BUSY, e.what()); return false; }
}

bool AddressCreateHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req); int userId = j.value("user_id", 0); std::string authErr;
        if (!Auth::requireUser(ctx_, j, userId, authErr)) { resp = makeError(ErrorCodes::UNAUTHORIZED, authErr); return false; }
        std::string receiver = j.value("receiver_name", ""), phone = j.value("phone", ""), detail = j.value("detail", "");
        if (receiver.empty() || phone.empty() || detail.empty() || receiver.size() > 100 || phone.size() > 32 || detail.size() > 500) { resp = makeError(ErrorCodes::SYSTEM_BUSY, "地址信息不完整或过长"); return false; }
        if (sqlite3_exec(ctx_.db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) { resp = makeError(ErrorCodes::SYSTEM_BUSY, "地址保存繁忙"); return false; }
        int makeDefault = j.value("is_default", false) ? 1 : 0;
        sqlite3_stmt* stmt = nullptr;
        if (!makeDefault && sqlite3_prepare_v2(ctx_.db, "SELECT COUNT(*) FROM addresses WHERE user_id=?", -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, userId);
            if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0) makeDefault = 1;
            sqlite3_finalize(stmt);
            stmt = nullptr;
        }
        if (makeDefault) {
            if (sqlite3_prepare_v2(ctx_.db, "UPDATE addresses SET is_default=0 WHERE user_id=?", -1, &stmt, nullptr) != SQLITE_OK) {
                sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
                resp = makeError(ErrorCodes::SYSTEM_BUSY, "默认地址更新失败"); return false;
            }
            sqlite3_bind_int(stmt, 1, userId);
            bool defaultsCleared = sqlite3_step(stmt) == SQLITE_DONE;
            sqlite3_finalize(stmt);
            if (!defaultsCleared) {
                sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
                resp = makeError(ErrorCodes::SYSTEM_BUSY, "默认地址更新失败"); return false;
            }
        }
        stmt = nullptr; const char* sql = "INSERT INTO addresses(user_id,receiver_name,phone,detail,is_default) VALUES(?,?,?,?,?)";
        bool ok = sqlite3_prepare_v2(ctx_.db, sql, -1, &stmt, nullptr) == SQLITE_OK;
        if (ok) { sqlite3_bind_int(stmt,1,userId); sqlite3_bind_text(stmt,2,receiver.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,3,phone.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_text(stmt,4,detail.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_int(stmt,5,makeDefault); ok = sqlite3_step(stmt)==SQLITE_DONE; }
        if (stmt) sqlite3_finalize(stmt);
        if (!ok || sqlite3_exec(ctx_.db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) { sqlite3_exec(ctx_.db,"ROLLBACK",nullptr,nullptr,nullptr); resp = makeError(ErrorCodes::SYSTEM_BUSY,"地址保存失败"); return false; }
        resp = json({{"success",true},{"address_id",static_cast<int>(sqlite3_last_insert_rowid(ctx_.db))}}).dump(); return true;
    } catch (const std::exception& e) { sqlite3_exec(ctx_.db,"ROLLBACK",nullptr,nullptr,nullptr); resp = makeError(ErrorCodes::SYSTEM_BUSY,e.what()); return false; }
}

bool ReviewCreateHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req); int userId=j.value("user_id",0), orderId=j.value("order_id",0), productId=j.value("product_id",0), rating=j.value("rating",0); std::string authErr;
        if (!Auth::requireUser(ctx_,j,userId,authErr)) { resp=makeError(ErrorCodes::UNAUTHORIZED,authErr); return false; }
        if (rating < 1 || rating > 5) { resp=makeError(ErrorCodes::SYSTEM_BUSY,"评分必须为1到5分"); return false; }
        auto order=ctx_.orders->findById(orderId); if(!order || order->userId!=userId) { resp=makeError(ErrorCodes::ORDER_NOT_FOUND,"订单不存在"); return false; }
        if(order->status!=ORDER_COMPLETED) { resp=makeError(ErrorCodes::ORDER_STATUS_FORBID,"完成订单后才能评价"); return false; }
        bool included=false; for(const auto& item:order->items) if(item.productId==productId) included=true;
        if(!included) { resp=makeError(ErrorCodes::PRODUCT_NOT_FOUND,"商品不在该订单中"); return false; }
        std::string content=j.value("content",""); std::string imageUrl=j.value("image_url","");
        if(content.size()>1000 || imageUrl.size()>500) { resp=makeError(ErrorCodes::SYSTEM_BUSY,"评价内容或图片地址过长"); return false; }
        if(!imageUrl.empty() && imageUrl.rfind("https://", 0) != 0 && imageUrl.rfind("http://", 0) != 0) { resp=makeError(ErrorCodes::SYSTEM_BUSY,"图片凭证必须使用 http(s) 地址"); return false; }
        if (sqlite3_exec(ctx_.db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) { resp=makeError(ErrorCodes::SYSTEM_BUSY,"评价提交繁忙"); return false; }
        sqlite3_stmt* stmt=nullptr; const char* sql="INSERT INTO reviews(order_id,product_id,user_id,rating,content,image_url) VALUES(?,?,?,?,?,?)";
        bool ok=sqlite3_prepare_v2(ctx_.db,sql,-1,&stmt,nullptr)==SQLITE_OK; if(ok){sqlite3_bind_int(stmt,1,orderId);sqlite3_bind_int(stmt,2,productId);sqlite3_bind_int(stmt,3,userId);sqlite3_bind_int(stmt,4,rating);sqlite3_bind_text(stmt,5,content.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(stmt,6,imageUrl.c_str(),-1,SQLITE_TRANSIENT);ok=sqlite3_step(stmt)==SQLITE_DONE;} if(stmt)sqlite3_finalize(stmt);
        if(!ok){sqlite3_exec(ctx_.db,"ROLLBACK",nullptr,nullptr,nullptr);resp=makeError(ErrorCodes::DUPLICATE_SUBMIT,"该商品已评价或评价保存失败");return false;}
        int reviewId = static_cast<int>(sqlite3_last_insert_rowid(ctx_.db));
        // 图片当前只是评价快照，服务端没有上传/对象存储校验，不能据客户端字段发放积分。
        appendAudit(ctx_.db,orderId,userId,"create_review",ORDER_COMPLETED,ORDER_COMPLETED,"创建商品评价");
        appendNotification(ctx_.db, userId, "review", "商品评价已发布");
        if (sqlite3_exec(ctx_.db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) { sqlite3_exec(ctx_.db,"ROLLBACK",nullptr,nullptr,nullptr); resp=makeError(ErrorCodes::SYSTEM_BUSY,"评价提交失败"); return false; }
        resp=json({{"success",true},{"review_id",reviewId},{"points_awarded",0}}).dump();return true;
    } catch(const std::exception&e){resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what());return false;}
}

bool ReviewListHandler::execute(const std::string& req, std::string& resp) {
    try { auto j=json::parse(req); int productId=j.value("product_id",0); if(!ctx_.products->findById(productId)){resp=makeError(ErrorCodes::PRODUCT_NOT_FOUND,"商品不存在");return false;} sqlite3_stmt* stmt=nullptr;json reviews=json::array();const char*sql="SELECT r.id,r.rating,r.content,r.image_url,r.created_at,r.user_id,u.username FROM reviews r JOIN users u ON u.id=r.user_id WHERE r.product_id=? ORDER BY r.id DESC";if(sqlite3_prepare_v2(ctx_.db,sql,-1,&stmt,nullptr)!=SQLITE_OK){resp=makeError(ErrorCodes::SYSTEM_BUSY,"评价查询失败");return false;}sqlite3_bind_int(stmt,1,productId);while(sqlite3_step(stmt)==SQLITE_ROW)reviews.push_back({{"review_id",sqlite3_column_int(stmt,0)},{"rating",sqlite3_column_int(stmt,1)},{"content",safeText(stmt,2)},{"image_url",safeText(stmt,3)},{"created_at",safeText(stmt,4)},{"user_id",sqlite3_column_int(stmt,5)},{"username",safeText(stmt,6)}});sqlite3_finalize(stmt);resp=json({{"product_id",productId},{"reviews",reviews}}).dump();return true;}catch(const std::exception&e){resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what());return false;}
}

bool CouponListHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j=json::parse(req); int userId=j.value("user_id",0); std::string authErr;
        if(!Auth::requireUser(ctx_,j,userId,authErr)){resp=makeError(ErrorCodes::UNAUTHORIZED,authErr);return false;}
        const char* sql="SELECT uc.id,uc.promotion_id,uc.status,p.name,p.type,p.param1,p.param2,p.param3,p.param4,p.param5,p.param6 FROM user_coupons uc JOIN promotions p ON p.id=uc.promotion_id WHERE uc.user_id=? ORDER BY uc.status,uc.id DESC";
        sqlite3_stmt* stmt=nullptr; if(sqlite3_prepare_v2(ctx_.db,sql,-1,&stmt,nullptr)!=SQLITE_OK){resp=makeError(ErrorCodes::SYSTEM_BUSY,"券包查询失败");return false;}
        sqlite3_bind_int(stmt,1,userId); json coupons=json::array();
        while(sqlite3_step(stmt)==SQLITE_ROW) coupons.push_back({{"coupon_id",sqlite3_column_int(stmt,0)},{"promotion_id",sqlite3_column_int(stmt,1)},{"status",sqlite3_column_int(stmt,2)},{"name",safeText(stmt,3)},{"type",sqlite3_column_int(stmt,4)},{"param1",sqlite3_column_double(stmt,5)},{"param2",sqlite3_column_double(stmt,6)},{"param3",sqlite3_column_double(stmt,7)},{"param4",sqlite3_column_double(stmt,8)},{"param5",sqlite3_column_double(stmt,9)},{"param6",sqlite3_column_double(stmt,10)}});
        sqlite3_finalize(stmt); resp=json({{"coupons",coupons}}).dump(); return true;
    } catch(const std::exception&e){resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what());return false;}
}

bool CouponClaimHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j=json::parse(req); int userId=j.value("user_id",0), promoId=j.value("promotion_id",0); std::string authErr;
        if(!Auth::requireUser(ctx_,j,userId,authErr)){resp=makeError(ErrorCodes::UNAUTHORIZED,authErr);return false;}
        auto promo=ctx_.promos->findById(promoId); if(!promo || promo->status!=1){resp=makeError(ErrorCodes::PROMO_EXPIRED,"促销不存在或已失效");return false;}
        sqlite3_stmt* stmt=nullptr; const char* sql="INSERT INTO user_coupons(user_id,promotion_id) VALUES(?,?)"; bool ok=sqlite3_prepare_v2(ctx_.db,sql,-1,&stmt,nullptr)==SQLITE_OK;
        if(ok){sqlite3_bind_int(stmt,1,userId);sqlite3_bind_int(stmt,2,promoId);ok=sqlite3_step(stmt)==SQLITE_DONE;} if(stmt)sqlite3_finalize(stmt);
        if(!ok){resp=makeError(ErrorCodes::DUPLICATE_SUBMIT,"该优惠券已领取");return false;}
        int couponId = static_cast<int>(sqlite3_last_insert_rowid(ctx_.db));
        appendNotification(ctx_.db,userId,"coupon","已领取优惠券："+promo->name);
        resp=json({{"success",true},{"coupon_id",couponId},{"promotion_id",promoId}}).dump();return true;
    }catch(const std::exception&e){resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what());return false;}
}

bool NotificationListHandler::execute(const std::string& req, std::string& resp) {
    try { auto j=json::parse(req);int userId=j.value("user_id",0);std::string authErr;if(!Auth::requireUser(ctx_,j,userId,authErr)){resp=makeError(ErrorCodes::UNAUTHORIZED,authErr);return false;}int limit=std::clamp(j.value("limit",50),1,100);sqlite3_stmt*stmt=nullptr;const char*sql="SELECT id,type,content,is_read,created_at FROM notifications WHERE user_id=? ORDER BY id DESC LIMIT ?";if(sqlite3_prepare_v2(ctx_.db,sql,-1,&stmt,nullptr)!=SQLITE_OK){resp=makeError(ErrorCodes::SYSTEM_BUSY,"通知查询失败");return false;}sqlite3_bind_int(stmt,1,userId);sqlite3_bind_int(stmt,2,limit);json list=json::array();while(sqlite3_step(stmt)==SQLITE_ROW)list.push_back({{"notification_id",sqlite3_column_int(stmt,0)},{"type",safeText(stmt,1)},{"content",safeText(stmt,2)},{"is_read",sqlite3_column_int(stmt,3)!=0},{"created_at",safeText(stmt,4)}});sqlite3_finalize(stmt);sqlite3_stmt*count=nullptr;int unread=0;if(sqlite3_prepare_v2(ctx_.db,"SELECT COUNT(*) FROM notifications WHERE user_id=? AND is_read=0",-1,&count,nullptr)==SQLITE_OK){sqlite3_bind_int(count,1,userId);if(sqlite3_step(count)==SQLITE_ROW)unread=sqlite3_column_int(count,0);sqlite3_finalize(count);}resp=json({{"notifications",list},{"unread_count",unread}}).dump();return true;}catch(const std::exception&e){resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what());return false;}
}

bool NotificationReadHandler::execute(const std::string& req, std::string& resp) {
    try {auto j=json::parse(req);int userId=j.value("user_id",0),id=j.value("notification_id",0);std::string authErr;if(!Auth::requireUser(ctx_,j,userId,authErr)){resp=makeError(ErrorCodes::UNAUTHORIZED,authErr);return false;}sqlite3_stmt*stmt=nullptr;if(sqlite3_prepare_v2(ctx_.db,"UPDATE notifications SET is_read=1 WHERE id=? AND user_id=?",-1,&stmt,nullptr)!=SQLITE_OK){resp=makeError(ErrorCodes::SYSTEM_BUSY,"通知更新失败");return false;}sqlite3_bind_int(stmt,1,id);sqlite3_bind_int(stmt,2,userId);bool ok=sqlite3_step(stmt)==SQLITE_DONE&&sqlite3_changes(ctx_.db)==1;sqlite3_finalize(stmt);if(!ok){resp=makeError(ErrorCodes::ORDER_NOT_FOUND,"通知不存在");return false;}resp=json({{"success",true},{"notification_id",id}}).dump();return true;}catch(const std::exception&e){resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what());return false;}
}

bool FavoriteListHandler::execute(const std::string& req, std::string& resp) {
    try { auto j=json::parse(req); int userId=j.value("user_id",0); std::string e; if(!Auth::requireUser(ctx_,j,userId,e)){resp=makeError(ErrorCodes::UNAUTHORIZED,e);return false;} sqlite3_stmt*s=nullptr;json a=json::array();if(sqlite3_prepare_v2(ctx_.db,"SELECT f.id,p.id,p.name,p.price,p.stock FROM favorites f JOIN products p ON p.id=f.product_id WHERE f.user_id=? ORDER BY f.id DESC",-1,&s,nullptr)!=SQLITE_OK){resp=makeError(ErrorCodes::SYSTEM_BUSY,"收藏查询失败");return false;}sqlite3_bind_int(s,1,userId);while(sqlite3_step(s)==SQLITE_ROW)a.push_back({{"favorite_id",sqlite3_column_int(s,0)},{"product_id",sqlite3_column_int(s,1)},{"name",safeText(s,2)},{"price",sqlite3_column_int(s,3)},{"stock",sqlite3_column_int(s,4)}});sqlite3_finalize(s);resp=json({{"favorites",a}}).dump();return true;}catch(const std::exception&e){resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what());return false;}
}

bool FavoriteToggleHandler::execute(const std::string& req, std::string& resp) {
    try { auto j=json::parse(req);int userId=j.value("user_id",0),productId=j.value("product_id",0);std::string e;if(!Auth::requireUser(ctx_,j,userId,e)){resp=makeError(ErrorCodes::UNAUTHORIZED,e);return false;}if(!ctx_.products->findById(productId)){resp=makeError(ErrorCodes::PRODUCT_NOT_FOUND,"商品不存在");return false;}sqlite3_stmt*s=nullptr;bool exists=false;if(sqlite3_prepare_v2(ctx_.db,"SELECT 1 FROM favorites WHERE user_id=? AND product_id=?",-1,&s,nullptr)==SQLITE_OK){sqlite3_bind_int(s,1,userId);sqlite3_bind_int(s,2,productId);exists=sqlite3_step(s)==SQLITE_ROW;sqlite3_finalize(s);}const char*sql=exists?"DELETE FROM favorites WHERE user_id=? AND product_id=?":"INSERT INTO favorites(user_id,product_id) VALUES(?,?)";if(sqlite3_prepare_v2(ctx_.db,sql,-1,&s,nullptr)!=SQLITE_OK){resp=makeError(ErrorCodes::SYSTEM_BUSY,"收藏更新失败");return false;}sqlite3_bind_int(s,1,userId);sqlite3_bind_int(s,2,productId);bool ok=sqlite3_step(s)==SQLITE_DONE;sqlite3_finalize(s);if(!ok){resp=makeError(ErrorCodes::SYSTEM_BUSY,"收藏更新失败");return false;}resp=json({{"success",true},{"favorite",!exists}}).dump();return true;}catch(const std::exception&e){resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what());return false;}
}

bool CheckinHandler::execute(const std::string& req, std::string& resp) {
    try {auto j=json::parse(req);int userId=j.value("user_id",0);std::string e;if(!Auth::requireUser(ctx_,j,userId,e)){resp=makeError(ErrorCodes::UNAUTHORIZED,e);return false;}if(sqlite3_exec(ctx_.db,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)!=SQLITE_OK){resp=makeError(ErrorCodes::SYSTEM_BUSY,"签到繁忙");return false;}sqlite3_stmt*s=nullptr;bool ok=sqlite3_prepare_v2(ctx_.db,"INSERT INTO user_checkins(user_id,checkin_date) VALUES(?,date('now','localtime'))",-1,&s,nullptr)==SQLITE_OK;if(ok){sqlite3_bind_int(s,1,userId);ok=sqlite3_step(s)==SQLITE_DONE;}if(s)sqlite3_finalize(s);if(!ok){sqlite3_exec(ctx_.db,"ROLLBACK",nullptr,nullptr,nullptr);resp=makeError(ErrorCodes::DUPLICATE_SUBMIT,"今天已经签到");return false;}if(sqlite3_prepare_v2(ctx_.db,"UPDATE users SET points=points+10 WHERE id=?",-1,&s,nullptr)!=SQLITE_OK){sqlite3_exec(ctx_.db,"ROLLBACK",nullptr,nullptr,nullptr);resp=makeError(ErrorCodes::SYSTEM_BUSY,"积分更新失败");return false;}sqlite3_bind_int(s,1,userId);ok=sqlite3_step(s)==SQLITE_DONE;sqlite3_finalize(s);if(!ok){sqlite3_exec(ctx_.db,"ROLLBACK",nullptr,nullptr,nullptr);resp=makeError(ErrorCodes::SYSTEM_BUSY,"积分更新失败");return false;}appendNotification(ctx_.db,userId,"checkin","签到成功，获得10积分");sqlite3_exec(ctx_.db,"COMMIT",nullptr,nullptr,nullptr);resp=json({{"success",true},{"points_awarded",10}}).dump();return true;}catch(const std::exception&e){sqlite3_exec(ctx_.db,"ROLLBACK",nullptr,nullptr,nullptr);resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what());return false;}
}

bool ShipmentQueryHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int userId = j.value("user_id", 0), orderId = j.value("order_id", 0);
        std::string err;
        if (!Auth::requireUser(ctx_, j, userId, err)) {
            resp = makeError(ErrorCodes::UNAUTHORIZED, err);
            return false;
        }
        auto order = ctx_.orders->findById(orderId);
        auto viewer = ctx_.users->findById(userId);
        const bool isOperator = viewer && (viewer->role == "admin" || viewer->role == "operator");
        if (!order || (order->userId != userId && !isOperator)) {
            resp = makeError(ErrorCodes::NO_PERMISSION, "无权查看物流");
            return false;
        }
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(ctx_.db, "SELECT tracking_no,status,updated_at FROM shipments WHERE order_id=?", -1, &stmt, nullptr) != SQLITE_OK) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "物流查询失败");
            return false;
        }
        sqlite3_bind_int(stmt, 1, orderId);
        json out = {{"order_id", orderId}, {"tracking_no", ""}, {"status", -1}};
        if (sqlite3_step(stmt) == SQLITE_ROW) {
            out["tracking_no"] = safeText(stmt, 0);
            out["status"] = sqlite3_column_int(stmt, 1);
            out["updated_at"] = safeText(stmt, 2);
        }
        sqlite3_finalize(stmt);
        resp = out.dump();
        return true;
    } catch (const std::exception& e) {
        resp = makeError(ErrorCodes::SYSTEM_BUSY, e.what());
        return false;
    }
}

bool ShipmentCreateHandler::execute(const std::string& req, std::string& resp) {
    try {auto j=json::parse(req);int admin=0;std::string e;if(!Auth::requireAdmin(ctx_,j,admin,e)){resp=makeError(ErrorCodes::UNAUTHORIZED,e);return false;}int orderId=j.value("order_id",0),status=j.value("status",0);std::string no=j.value("tracking_no","");if(no.empty()||no.size()>64||status<0||status>2){resp=makeError(ErrorCodes::SYSTEM_BUSY,"物流参数不合法");return false;}auto o=ctx_.orders->findById(orderId);if(!o||o->status!=ORDER_SHIPPED){resp=makeError(ErrorCodes::ORDER_STATUS_FORBID,"订单必须先标记为已发货");return false;}sqlite3_stmt*s=nullptr;const char*sql="INSERT INTO shipments(order_id,tracking_no,status) VALUES(?,?,?) ON CONFLICT(order_id) DO UPDATE SET tracking_no=excluded.tracking_no,status=excluded.status,updated_at=datetime('now')";bool ok=sqlite3_prepare_v2(ctx_.db,sql,-1,&s,nullptr)==SQLITE_OK;if(ok){sqlite3_bind_int(s,1,orderId);sqlite3_bind_text(s,2,no.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(s,3,status);ok=sqlite3_step(s)==SQLITE_DONE;}if(s)sqlite3_finalize(s);if(!ok){resp=makeError(ErrorCodes::SYSTEM_BUSY,"物流保存失败");return false;}appendNotification(ctx_.db,o->userId,"shipment","物流信息已更新");resp=json({{"success",true},{"order_id",orderId},{"tracking_no",no},{"status",status}}).dump();return true;}catch(const std::exception&e){resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what());return false;}
}

bool AdminProductHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int admin = 0;
        std::string authErr;
        if (!Auth::requireAdmin(ctx_, j, admin, authErr)) {
            resp = makeError(ErrorCodes::UNAUTHORIZED, authErr);
            return false;
        }
        int productId = j.value("product_id", 0);
        std::string action = j.value("action", "");
        auto product = ctx_.products->findById(productId);
        if (!product) { resp = makeError(ErrorCodes::PRODUCT_NOT_FOUND, "商品不存在"); return false; }
        if (action == "sku_create") {
            std::string code = j.value("sku_code", "");
            std::string color = j.value("color", "");
            std::string size = j.value("size", "");
            int delta = j.value("price_delta", 0), stock = j.value("stock", 0);
            if (code.empty() || code.size() > 80 || delta < -100000 || stock < 0) {
                resp = makeError(ErrorCodes::SYSTEM_BUSY, "SKU参数不合法"); return false;
            }
            std::lock_guard<std::mutex> stockLock(*ctx_.stockMutex);
            if (sqlite3_exec(ctx_.db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
                resp = makeError(ErrorCodes::SYSTEM_BUSY, "商品管理繁忙"); return false;
            }
            sqlite3_stmt* stmt = nullptr;
            bool ok = sqlite3_prepare_v2(ctx_.db,
                "INSERT INTO product_skus(product_id,sku_code,color,size,price_delta,stock) VALUES(?,?,?,?,?,?)",
                -1, &stmt, nullptr) == SQLITE_OK;
            if (ok) {
                sqlite3_bind_int(stmt, 1, productId);
                sqlite3_bind_text(stmt, 2, code.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 3, color.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_text(stmt, 4, size.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int(stmt, 5, delta);
                sqlite3_bind_int(stmt, 6, stock);
                ok = sqlite3_step(stmt) == SQLITE_DONE;
            }
            if (stmt) sqlite3_finalize(stmt);
            int skuId = static_cast<int>(sqlite3_last_insert_rowid(ctx_.db));
            if (!ok || !appendInventoryAudit(ctx_.db, productId, skuId, admin, "sku_create", 0, stock, code)) {
                sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
                resp = makeError(ErrorCodes::DUPLICATE_SUBMIT, "SKU编码已存在或审计保存失败"); return false;
            }
            if (sqlite3_exec(ctx_.db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
                sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
                resp = makeError(ErrorCodes::SYSTEM_BUSY, "商品管理提交失败"); return false;
            }
            resp = json({{"success", true}, {"sku_id", skuId}}).dump();
            return true;
        }
        int value = j.value("value", -1);
        if ((action != "status" && action != "stock") || value < 0 || (action == "status" && value > 1)) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "参数不合法"); return false;
        }
        const char* sql = action == "status"
            ? "UPDATE products SET status=?,update_time=datetime('now') WHERE id=?"
            : "UPDATE products SET stock=?,update_time=datetime('now') WHERE id=?";
        sqlite3_stmt* stmt = nullptr;
        std::lock_guard<std::mutex> stockLock(*ctx_.stockMutex);
        if (sqlite3_exec(ctx_.db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "商品管理繁忙"); return false;
        }
        bool ok = sqlite3_prepare_v2(ctx_.db, sql, -1, &stmt, nullptr) == SQLITE_OK;
        if (ok) { sqlite3_bind_int(stmt, 1, value); sqlite3_bind_int(stmt, 2, productId); ok = sqlite3_step(stmt) == SQLITE_DONE; }
        if (stmt) sqlite3_finalize(stmt);
        int before = action == "status" ? product->status : product->stock;
        if (!ok || !appendInventoryAudit(ctx_.db, productId, 0, admin, action, before, value, "管理员商品管理")) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "商品更新或审计保存失败"); return false;
        }
        if (sqlite3_exec(ctx_.db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "商品管理提交失败"); return false;
        }
        resp = json({{"success", true}, {"product_id", productId}, {"action", action}, {"value", value}}).dump();
        return true;
    } catch (const std::exception& e) { resp = makeError(ErrorCodes::SYSTEM_BUSY, e.what()); return false; }
}

bool AdminProductListHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int adminId = 0;
        std::string authErr;
        if (!Auth::requireAdmin(ctx_, j, adminId, authErr)) {
            resp = makeError(ErrorCodes::UNAUTHORIZED, authErr);
            return false;
        }

        // 管理员需要看到全部商品，否则下架商品无法再次上架。
        const char* sql = "SELECT id, name, price, stock, category, status "
                          "FROM products ORDER BY id";
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(ctx_.db, sql, -1, &stmt, nullptr) != SQLITE_OK) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "管理员商品列表查询失败");
            return false;
        }
        json products = json::array();
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            products.push_back({
                {"id", sqlite3_column_int(stmt, 0)},
                {"name", safeText(stmt, 1)},
                {"price", sqlite3_column_int(stmt, 2)},
                {"stock", sqlite3_column_int(stmt, 3)},
                {"category", safeText(stmt, 4)},
                {"status", sqlite3_column_int(stmt, 5)}
            });
        }
        sqlite3_finalize(stmt);
        resp = json({{"products", products}}).dump();
        return true;
    } catch (const std::exception& e) {
        resp = makeError(ErrorCodes::SYSTEM_BUSY, e.what());
        return false;
    }
}

bool SkuListHandler::execute(const std::string& req, std::string& resp) {
    try {auto j=json::parse(req);int pid=j.value("product_id",0);if(!ctx_.products->findById(pid)){resp=makeError(ErrorCodes::PRODUCT_NOT_FOUND,"商品不存在");return false;}sqlite3_stmt*s=nullptr;json a=json::array();if(sqlite3_prepare_v2(ctx_.db,"SELECT id,sku_code,color,size,price_delta,stock FROM product_skus WHERE product_id=? AND status=1 ORDER BY id",-1,&s,nullptr)!=SQLITE_OK){resp=makeError(ErrorCodes::SYSTEM_BUSY,"SKU查询失败");return false;}sqlite3_bind_int(s,1,pid);while(sqlite3_step(s)==SQLITE_ROW)a.push_back({{"sku_id",sqlite3_column_int(s,0)},{"sku_code",safeText(s,1)},{"color",safeText(s,2)},{"size",safeText(s,3)},{"price_delta",sqlite3_column_int(s,4)},{"stock",sqlite3_column_int(s,5)}});sqlite3_finalize(s);resp=json({{"product_id",pid},{"skus",a}}).dump();return true;}catch(const std::exception&e){resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what());return false;}
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
        order.status = ORDER_PENDING;
        int addressId = j.value("address_id", 0);
        std::vector<int> couponIds;
        if (j.contains("coupon_ids") && j["coupon_ids"].is_array()) {
            for (const auto& value : j["coupon_ids"]) if (value.is_number_integer()) couponIds.push_back(value.get<int>());
        }
        if (couponIds.empty()) {
            const int legacyCouponId = j.value("coupon_id", 0);
            if (legacyCouponId > 0) couponIds.push_back(legacyCouponId);
        }
        std::sort(couponIds.begin(), couponIds.end());
        couponIds.erase(std::unique(couponIds.begin(), couponIds.end()), couponIds.end());
        int couponId = couponIds.empty() ? 0 : couponIds.front();
        int pointsUsed = j.value("points", 0);
        if (pointsUsed < 0 || pointsUsed % 100 != 0 || pointsUsed > user->points) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "积分必须是100的整数倍且不能超过余额");
            return false;
        }
        order.couponId = couponId;
        order.pointsUsed = pointsUsed;
        std::unique_ptr<IPromotion> chain;  // 促销链（作用域需覆盖响应构造）
        std::vector<std::tuple<int,int,int>> purchasedCartItems;

        sqlite3* db = ctx_.db;
        std::lock_guard<std::mutex> lock(*ctx_.stockMutex);

        // ===== 事务开始（BEGIN IMMEDIATE 避免读写锁升级死锁） =====
        char* errMsg = nullptr;
        if (sqlite3_exec(db, "BEGIN IMMEDIATE TRANSACTION;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "系统繁忙，请稍后重试");
            return false;
        }
        sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS order_promotions (order_id INTEGER NOT NULL, promotion_id INTEGER NOT NULL, user_id INTEGER NOT NULL, created_at DATETIME DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY(order_id,promotion_id));", nullptr, nullptr, nullptr);
        sqlite3_exec(db, "CREATE TABLE IF NOT EXISTS order_coupons (order_id INTEGER NOT NULL, coupon_id INTEGER NOT NULL, user_id INTEGER NOT NULL, PRIMARY KEY(order_id,coupon_id));", nullptr, nullptr, nullptr);

        // 服务端购物车是结算权威来源；兼容旧客户端时也忽略其本地 items 快照。
        {
            sqlite3_stmt* cartStmt = nullptr;
            if (sqlite3_prepare_v2(db, "SELECT product_id,sku_id,quantity FROM cart_items WHERE user_id=? ORDER BY id", -1, &cartStmt, nullptr) != SQLITE_OK) {
                sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
                resp = makeError(ErrorCodes::SYSTEM_BUSY, "购物车读取失败"); return false;
            }
            sqlite3_bind_int(cartStmt, 1, userId);
            std::vector<std::tuple<int,int,int>> cartRows;
            while (sqlite3_step(cartStmt) == SQLITE_ROW) cartRows.emplace_back(sqlite3_column_int(cartStmt, 0), sqlite3_column_int(cartStmt, 1), sqlite3_column_int(cartStmt, 2));
            sqlite3_finalize(cartStmt);
            json serverItems = json::array();
            const bool hasSelection = j.contains("items") && j["items"].is_array() && !j["items"].empty();
            if (hasSelection) {
                for (const auto& requested : j["items"]) {
                    int pid = requested.value("product_id", 0), sid = requested.value("sku_id", 0), qty = requested.value("quantity", 0);
                    auto it = std::find_if(cartRows.begin(), cartRows.end(), [&](const auto& row) { return std::get<0>(row) == pid && std::get<1>(row) == sid; });
                    if (qty <= 0 || it == cartRows.end() || qty > std::get<2>(*it)) continue;
                    purchasedCartItems.emplace_back(pid, sid, qty);
                    serverItems.push_back({{"product_id", pid}, {"sku_id", sid}, {"quantity", qty}});
                }
            } else {
                for (const auto& row : cartRows) {
                    int pid=std::get<0>(row), sid=std::get<1>(row), qty=std::get<2>(row); auto product=ctx_.products->findById(pid); int price=product?product->price:0, stock=product?product->stock:0; std::string label;
                    if (!product || product->status != 1 || !cartSku(db, pid, sid, price, stock, label) || qty <= 0) continue;
                    purchasedCartItems.emplace_back(pid, sid, qty);
                    serverItems.push_back({{"product_id", pid}, {"sku_id", sid}, {"quantity", qty}});
                }
            }
            j["items"] = std::move(serverItems);
        }

        bool ok = true;
        std::string failMsg;

        // 在同一事务内校验互斥组、促销总次数和单用户次数，避免并发结算绕过约束。
        std::set<std::string> selectedExclusiveGroups;
        for (int promoId : promoIds) {
            auto promo = ctx_.promos->findById(promoId);
            if (!promo) { ok = false; failMsg = "促销不存在"; break; }
            if (!promo->exclusiveGroup.empty() &&
                !selectedExclusiveGroups.insert(promo->exclusiveGroup).second) {
                ok = false;
                failMsg = "同一互斥组的促销不能同时使用";
                break;
            }
            sqlite3_stmt* usage = nullptr;
            const char* usageSql =
                "SELECT COUNT(*), COALESCE(SUM(CASE WHEN op.user_id=? THEN 1 ELSE 0 END),0) "
                "FROM order_promotions op JOIN orders o ON o.id=op.order_id "
                "WHERE op.promotion_id=? AND o.status<>4";
            if (sqlite3_prepare_v2(db, usageSql, -1, &usage, nullptr) != SQLITE_OK) {
                ok = false; failMsg = std::string("促销使用次数查询失败: ") + sqlite3_errmsg(db);
            } else {
                sqlite3_bind_int(usage, 1, userId);
                sqlite3_bind_int(usage, 2, promoId);
                if (sqlite3_step(usage) != SQLITE_ROW) {
                    ok = false; failMsg = "促销使用次数查询失败";
                } else {
                    const int totalUses = sqlite3_column_int(usage, 0);
                    const int userUses = sqlite3_column_int(usage, 1);
                    if (promo->maxUses > 0 && totalUses >= promo->maxUses) {
                        ok = false; failMsg = "促销已达到总使用次数上限";
                    } else if (promo->perUserLimit > 0 && userUses >= promo->perUserLimit) {
                        ok = false; failMsg = "您已达到该促销的使用次数上限";
                    }
                }
            }
            if (usage) sqlite3_finalize(usage);
            if (!ok) break;
        }
        if (!ok) resp = makeError(ErrorCodes::PROMO_EXPIRED, failMsg);

        for (int selectedCouponId : couponIds) {
            sqlite3_stmt* couponStmt = nullptr;
            if (sqlite3_prepare_v2(db, "SELECT promotion_id,status FROM user_coupons WHERE id=? AND user_id=?", -1, &couponStmt, nullptr) != SQLITE_OK) {
                ok = false; failMsg = "优惠券查询失败";
            } else {
                sqlite3_bind_int(couponStmt, 1, selectedCouponId); sqlite3_bind_int(couponStmt, 2, userId);
                if (sqlite3_step(couponStmt) != SQLITE_ROW || sqlite3_column_int(couponStmt, 1) != 0) {
                    ok = false; failMsg = "优惠券不存在或已使用";
                } else {
                    int couponPromoId = sqlite3_column_int(couponStmt, 0);
                    auto couponPromo = ctx_.promos->findById(couponPromoId);
                    if (!couponPromo || couponPromo->status != 1) { ok = false; failMsg = "优惠券已失效"; }
                    if (std::find(promoIds.begin(), promoIds.end(), couponPromoId) == promoIds.end()) promoIds.push_back(couponPromoId);
                }
                sqlite3_finalize(couponStmt);
            }
            if (ok) {
                sqlite3_stmt* lockCoupon = nullptr;
                bool locked = sqlite3_prepare_v2(db, "UPDATE user_coupons SET status=2 WHERE id=? AND user_id=? AND status=0", -1, &lockCoupon, nullptr) == SQLITE_OK;
                if (locked) {
                    sqlite3_bind_int(lockCoupon, 1, selectedCouponId);
                    sqlite3_bind_int(lockCoupon, 2, userId);
                    locked = sqlite3_step(lockCoupon) == SQLITE_DONE && sqlite3_changes(db) == 1;
                }
                if (lockCoupon) sqlite3_finalize(lockCoupon);
                if (!locked) { ok = false; failMsg = "优惠券已被其他待支付订单占用"; }
            }
            if (!ok) resp = makeError(ErrorCodes::PROMO_EXPIRED, failMsg);
        }

        if (addressId <= 0) {
            ok = false; failMsg = "请选择收货地址";
            resp = makeError(ErrorCodes::SYSTEM_BUSY, failMsg);
        } else {
            sqlite3_stmt* addressStmt = nullptr;
            if (sqlite3_prepare_v2(db, "SELECT receiver_name,phone,detail FROM addresses WHERE id=? AND user_id=?", -1, &addressStmt, nullptr) != SQLITE_OK) {
                ok = false; failMsg = "收货地址查询失败";
                resp = makeError(ErrorCodes::SYSTEM_BUSY, failMsg);
            } else {
                sqlite3_bind_int(addressStmt, 1, addressId);
                sqlite3_bind_int(addressStmt, 2, userId);
                if (sqlite3_step(addressStmt) != SQLITE_ROW) {
                    ok = false; failMsg = "收货地址不存在";
                    resp = makeError(ErrorCodes::SYSTEM_BUSY, failMsg);
                } else {
                    order.addressReceiver = safeText(addressStmt, 0);
                    order.addressPhone = safeText(addressStmt, 1);
                    order.addressDetail = safeText(addressStmt, 2);
                }
                sqlite3_finalize(addressStmt);
            }
        }

        if (!j.contains("items") || j["items"].empty()) {
            ok = false; failMsg = "购物车为空";
            resp = makeError(ErrorCodes::CART_EMPTY, failMsg);
        }

        if (ok) {
            std::map<std::pair<int, int>, int> requestedStock;
            for (auto& item : j["items"]) {
                int pid  = item.value("product_id", 0);
                int qty  = item.value("quantity", 0);
                int skuId = item.value("sku_id", 0);
                if (qty <= 0) { ok = false; failMsg = "数量不合法"; resp = makeError(ErrorCodes::STOCK_NOT_ENOUGH, failMsg); break; }
                auto prod = ctx_.products->findById(pid);
                if (!prod) { ok = false; failMsg = "商品不存在"; resp = makeError(ErrorCodes::PRODUCT_NOT_FOUND, failMsg); break; }
                // 原子扣减（条件 stock >= qty），失败即库存不足
                int unitPrice = prod->price;
                std::string skuLabel;
                if (skuId > 0) {
                    sqlite3_stmt* skuStmt = nullptr;
                    bool skuOk = sqlite3_prepare_v2(db, "SELECT color,size,price_delta,status FROM product_skus WHERE id=? AND product_id=?", -1, &skuStmt, nullptr) == SQLITE_OK;
                    if (skuOk) {
                        sqlite3_bind_int(skuStmt, 1, skuId);
                        sqlite3_bind_int(skuStmt, 2, pid);
                        skuOk = sqlite3_step(skuStmt) == SQLITE_ROW && sqlite3_column_int(skuStmt, 3) == 1;
                        if (skuOk) {
                            skuLabel = safeText(skuStmt, 0) + " / " + safeText(skuStmt, 1);
                            unitPrice += sqlite3_column_int(skuStmt, 2);
                        }
                    }
                    if (skuStmt) sqlite3_finalize(skuStmt);
                    if (!skuOk || unitPrice < 0) { ok = false; failMsg = "SKU invalid"; resp = makeError(ErrorCodes::PRODUCT_NOT_FOUND, failMsg); break; }
                }
                requestedStock[{pid, skuId}] += qty;
                OrderItem oi;
                oi.productId   = pid;
                oi.productName = prod->name;   // 快照
                oi.price       = unitPrice;  // 快照
                oi.quantity    = qty;
                oi.skuId       = skuId;
                oi.skuLabel    = skuLabel;
                order.items.push_back(oi);
                order.totalAmount += unitPrice * qty;
            }
            if (ok) {
                for (const auto& requested : requestedStock) {
                    sqlite3_stmt* stockStmt = nullptr;
                    const char* stockSql = requested.first.second > 0
                        ? "SELECT stock FROM product_skus WHERE id=? AND product_id=? AND status=1 AND stock>=?"
                        : "SELECT stock FROM products WHERE id=? AND status=1 AND stock>=?";
                    bool stockOk = sqlite3_prepare_v2(db, stockSql, -1, &stockStmt, nullptr) == SQLITE_OK;
                    if (stockOk) {
                        sqlite3_bind_int(stockStmt, 1, requested.first.second > 0 ? requested.first.second : requested.first.first);
                        if (requested.first.second > 0) sqlite3_bind_int(stockStmt, 2, requested.first.first);
                        sqlite3_bind_int(stockStmt, requested.first.second > 0 ? 3 : 2, requested.second);
                        stockOk = sqlite3_step(stockStmt) == SQLITE_ROW;
                    }
                    if (stockStmt) sqlite3_finalize(stockStmt);
                    if (!stockOk) { ok = false; failMsg = "库存不足"; resp = makeError(ErrorCodes::STOCK_NOT_ENOUGH, failMsg); break; }
                }
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
            order.shippingFee = finalAmount >= 9900 ? 0 : 800;
            finalAmount += order.shippingFee;
            order.finalAmount = std::max(0, finalAmount - pointsUsed);
            order.discountAmount = order.totalAmount + order.shippingFee - order.finalAmount;
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
                sqlite3_stmt* promoStmt = nullptr;
                if (sqlite3_prepare_v2(db, "INSERT INTO order_promotions(order_id,promotion_id,user_id) VALUES(?,?,?)", -1, &promoStmt, nullptr) != SQLITE_OK) {
                    ok = false; failMsg = "促销使用记录保存失败";
                } else {
                    for (int promoId : promoIds) {
                        sqlite3_reset(promoStmt);
                        sqlite3_bind_int(promoStmt, 1, order.id);
                        sqlite3_bind_int(promoStmt, 2, promoId);
                        sqlite3_bind_int(promoStmt, 3, userId);
                        if (sqlite3_step(promoStmt) != SQLITE_DONE) { ok = false; failMsg = "促销使用记录保存失败"; break; }
                    }
                }
                if (promoStmt) sqlite3_finalize(promoStmt);
                sqlite3_stmt* couponRecord = nullptr;
                if (ok && !couponIds.empty() && sqlite3_prepare_v2(db, "INSERT INTO order_coupons(order_id,coupon_id,user_id) VALUES(?,?,?)", -1, &couponRecord, nullptr) == SQLITE_OK) {
                    for (int selectedCouponId : couponIds) {
                        sqlite3_reset(couponRecord); sqlite3_bind_int(couponRecord, 1, order.id); sqlite3_bind_int(couponRecord, 2, selectedCouponId); sqlite3_bind_int(couponRecord, 3, userId);
                        if (sqlite3_step(couponRecord) != SQLITE_DONE) { ok = false; failMsg = "优惠券使用记录保存失败"; break; }
                    }
                }
                if (couponRecord) sqlite3_finalize(couponRecord);
                if (!ok) { resp = makeError(ErrorCodes::SYSTEM_BUSY, failMsg); }
                appendAudit(db, order.id, userId, "create_order", -1, order.status, "订单创建并完成结算");
                appendNotification(db, userId, "order", "订单已创建，等待支付");
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
            for (const auto& item : purchasedCartItems) {
                sqlite3_stmt* clearCart = nullptr;
                if (sqlite3_prepare_v2(db, "UPDATE cart_items SET quantity=quantity-? WHERE user_id=? AND product_id=? AND sku_id=?", -1, &clearCart, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int(clearCart, 1, std::get<2>(item)); sqlite3_bind_int(clearCart, 2, userId);
                    sqlite3_bind_int(clearCart, 3, std::get<0>(item)); sqlite3_bind_int(clearCart, 4, std::get<1>(item)); sqlite3_step(clearCart);
                }
                if (clearCart) sqlite3_finalize(clearCart);
                sqlite3_stmt* removeEmpty = nullptr;
                if (sqlite3_prepare_v2(db, "DELETE FROM cart_items WHERE user_id=? AND product_id=? AND sku_id=? AND quantity<=0", -1, &removeEmpty, nullptr) == SQLITE_OK) {
                    sqlite3_bind_int(removeEmpty, 1, userId); sqlite3_bind_int(removeEmpty, 2, std::get<0>(item)); sqlite3_bind_int(removeEmpty, 3, std::get<1>(item)); sqlite3_step(removeEmpty);
                }
                if (removeEmpty) sqlite3_finalize(removeEmpty);
            }
            OrderEventBus::instance().notifyOrderCreated(order);

            json out;
            out["order_id"]  = order.id;
            out["order_no"]  = order.orderNo;
            out["original"]  = order.totalAmount;
            out["discount"]  = order.discountAmount;
            out["final"]     = order.finalAmount;
            out["status"]    = order.status;
            out["payment_status"] = "pending";
            out["shipping_fee"] = order.shippingFee;
            out["points_used"] = order.pointsUsed;
            out["address"] = {{"receiver_name", order.addressReceiver},
                               {"phone", order.addressPhone},
                               {"detail", order.addressDetail}};
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
        // 管理员/客服可查看全部订单，普通用户只能查看自己的订单
        auto viewer = ctx_.users->findById(userId);
        const bool isOperator = viewer && (viewer->role == "admin" || viewer->role == "operator");
        auto orders = isOperator ? ctx_.orders->findAll()
                                 : ctx_.orders->findByUserId(userId);
        // 附加退货状态：order_id -> 最新退货单状态（0 无退货）
        std::map<int, int> returnStatus;
        for (const auto& r : ctx_.returns->findAll()) {
            if (returnStatus.find(r.orderId) == returnStatus.end()) {
                returnStatus[r.orderId] = r.status == RETURN_DONE ? 4 : r.status + 1;
            }
        }
        json out;
        out["orders"] = json::array();
        for (const auto& o : orders) {
            json item;
            item["order_id"]  = o.id;
            item["order_no"]  = o.orderNo;
            // 管理员视图附加下单用户名，便于识别订单归属
            auto owner = ctx_.users->findById(o.userId);
            item["username"] = owner ? owner->username : "";
            item["original"]  = o.totalAmount;
            item["discount"]  = o.discountAmount;
            item["final"]     = o.finalAmount;
            item["status"]    = o.status;
            item["return_status"] = returnStatus.count(o.id) ? returnStatus[o.id] : 0;
            item["create_time"] = o.createTime;
            item["shipping_fee"] = o.shippingFee;
            item["points_used"] = o.pointsUsed;
            item["address"] = {{"receiver_name", o.addressReceiver},
                                {"phone", o.addressPhone},
                                {"detail", o.addressDetail}};
            item["items"]     = json::array();
            for (const auto& it : o.items) {
                json ii;
                ii["product_id"]   = it.productId;
                ii["product_name"] = it.productName;
                ii["price"]        = it.price;
                ii["quantity"]     = it.quantity;
                ii["sku_id"]       = it.skuId;
                ii["sku_label"]    = it.skuLabel;
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
            const std::string& returnBaseTime = order->receiveTime.empty()
                ? order->createTime : order->receiveTime;
            sqlite3_bind_text(ageStmt, 1, returnBaseTime.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(ageStmt) == SQLITE_ROW) {
                double ageSeconds = sqlite3_column_double(ageStmt, 0);
                withinReturnWindow = ageSeconds >= 0.0 && ageSeconds <= 7.0 * 24.0 * 3600.0;
            }
        }
        if (ageStmt) sqlite3_finalize(ageStmt);
        if (order->receiveTime.empty()) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::ORDER_STATUS_FORBID, "确认收货后才能申请退货");
            return false;
        }
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
        appendAdminNotification(db, "return", "收到新的退货申请，请及时审核");

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
// 普通用户：仅返回自己的退货单；管理员/运营：返回全部（含他人）
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
        bool isAdmin = user && (user->role == "admin" || user->role == "operator");
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
            item["audit_comment"] = r.auditComment;
            item["refund_amount"] = r.refundAmount;
            // 审核 SLA：申请超过 48 小时仍未处理即标记超时，状态不被悄悄篡改。
            sqlite3_stmt* overdueStmt = nullptr;
            bool overdue = false;
            if (sqlite3_prepare_v2(ctx_.db, "SELECT (julianday('now')-julianday(?))*24 >= 48", -1, &overdueStmt, nullptr) == SQLITE_OK) {
                sqlite3_bind_text(overdueStmt, 1, r.applyTime.c_str(), -1, SQLITE_TRANSIENT);
                overdue = sqlite3_step(overdueStmt) == SQLITE_ROW && sqlite3_column_int(overdueStmt, 0) != 0;
            }
            if (overdueStmt) sqlite3_finalize(overdueStmt);
            item["is_overdue"] = (r.status == RETURN_PENDING && overdue);
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
        std::string auditComment = j.value("audit_comment", "");
        int refundAmount = j.value("refund_amount", 0);
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
        if (auditComment.size() > 500 || refundAmount < 0 || (approve && order && refundAmount > order->finalAmount)) {
            sqlite3_exec(db, "ROLLBACK;", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "审核意见或退款金额不合法");
            return false;
        }
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
                bool restored = false;
                if (it.skuId > 0) {
                    sqlite3_stmt* skuStmt = nullptr;
                    if (sqlite3_prepare_v2(db,
                        "UPDATE product_skus SET stock=stock+? WHERE id=? AND product_id=?",
                        -1, &skuStmt, nullptr) == SQLITE_OK) {
                        sqlite3_bind_int(skuStmt, 1, it.quantity);
                        sqlite3_bind_int(skuStmt, 2, it.skuId);
                        sqlite3_bind_int(skuStmt, 3, it.productId);
                        restored = sqlite3_step(skuStmt) == SQLITE_DONE && sqlite3_changes(db) == 1;
                    }
                    if (skuStmt) sqlite3_finalize(skuStmt);
                } else {
                    restored = ctx_.products->restoreStock(it.productId, it.quantity);
                }
                if (!restored) {
                    ok = false; failMsg = "库存回补失败";
                    break;
                }
            }
            if (ok) ok = ctx_.orders->updateStatus(order->id, ORDER_CANCELLED);  // 已取消（已退款）
            if (ok) ok = ctx_.returns->updateStatus(returnId, RETURN_APPROVED);
            if (ok && order->pointsUsed > 0) {
                sqlite3_stmt* s = nullptr;
                bool good = sqlite3_prepare_v2(db, "UPDATE users SET points=points+? WHERE id=?", -1, &s, nullptr) == SQLITE_OK;
                if (good) { sqlite3_bind_int(s, 1, order->pointsUsed); sqlite3_bind_int(s, 2, order->userId); good = sqlite3_step(s) == SQLITE_DONE && sqlite3_changes(db) == 1; }
                if (s) sqlite3_finalize(s);
                if (good) {
                    good = sqlite3_prepare_v2(db, "INSERT INTO point_transactions(user_id,order_id,amount,balance_after,reason) SELECT ?,?,?,points,'refund' FROM users WHERE id=?", -1, &s, nullptr) == SQLITE_OK;
                    if (good) { sqlite3_bind_int(s, 1, order->userId); sqlite3_bind_int(s, 2, order->id); sqlite3_bind_int(s, 3, order->pointsUsed); sqlite3_bind_int(s, 4, order->userId); good = sqlite3_step(s) == SQLITE_DONE; }
                    if (s) sqlite3_finalize(s);
                }
                if (!good) { ok = false; failMsg = "points refund failed"; }
            }
            if (ok && order->couponId > 0) {
                sqlite3_stmt* s = nullptr;
                bool good = sqlite3_prepare_v2(db, "UPDATE user_coupons SET status=0,used_at=NULL WHERE id=? AND user_id=? AND status=1", -1, &s, nullptr) == SQLITE_OK;
                if (good) { sqlite3_bind_int(s, 1, order->couponId); sqlite3_bind_int(s, 2, order->userId); good = sqlite3_step(s) == SQLITE_DONE && sqlite3_changes(db) == 1; }
                if (s) sqlite3_finalize(s);
                if (!good) { ok = false; failMsg = "coupon refund failed"; }
            }
            if (ok) {
                sqlite3_stmt* s = nullptr;
                bool good = sqlite3_prepare_v2(db, "UPDATE commission_records SET status=2 WHERE order_id=? AND status<>2", -1, &s, nullptr) == SQLITE_OK;
                if (good) { sqlite3_bind_int(s, 1, order->id); good = sqlite3_step(s) == SQLITE_DONE; }
                if (s) sqlite3_finalize(s);
                if (!good) { ok = false; failMsg = "commission reversal failed"; }
            }
            if (ok) {
                sqlite3_stmt* refundStmt = nullptr;
                bool refundOk = sqlite3_prepare_v2(db,
                    "INSERT INTO refund_records(order_id,return_id,user_id,amount,reason) VALUES(?,?,?,?,?)",
                    -1, &refundStmt, nullptr) == SQLITE_OK;
                if (refundOk) {
                    sqlite3_bind_int(refundStmt, 1, order->id);
                    sqlite3_bind_int(refundStmt, 2, returnId);
                    sqlite3_bind_int(refundStmt, 3, order->userId);
                    sqlite3_bind_int(refundStmt, 4, order->finalAmount);
                    sqlite3_bind_text(refundStmt, 5, rr->reason.c_str(), -1, SQLITE_TRANSIENT);
                    refundOk = sqlite3_step(refundStmt) == SQLITE_DONE;
                }
                if (refundStmt) sqlite3_finalize(refundStmt);
                if (!refundOk) {
                    ok = false;
                    failMsg = "退款流水保存失败";
                }
            }
            if (ok) appendAudit(db, order->id, adminId, "approve_return", ORDER_RETURNING, ORDER_CANCELLED, "退货审核通过并回补库存");
        } else {
            // 拒绝：订单恢复申请前状态
            ok = ctx_.orders->updateStatus(order->id, rr->origStatus);
            if (ok) ok = ctx_.returns->updateStatus(returnId, RETURN_REJECTED);
            if (ok) appendAudit(db, order->id, adminId, "reject_return", ORDER_RETURNING, rr->origStatus, "退货审核拒绝并恢复订单状态");
        }
        if (ok) appendNotification(db, rr->userId, "return", approve ? "退货审核已通过" : "退货申请已拒绝");

        sqlite3_stmt* auditMeta = nullptr;
        if (sqlite3_prepare_v2(db, "UPDATE returns SET audit_comment=?, refund_amount=? WHERE id=?", -1, &auditMeta, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(auditMeta, 1, auditComment.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_int(auditMeta, 2, approve ? (refundAmount > 0 ? refundAmount : order->finalAmount) : 0);
            sqlite3_bind_int(auditMeta, 3, returnId);
            if (sqlite3_step(auditMeta) != SQLITE_DONE) ok = false;
            sqlite3_finalize(auditMeta);
        } else ok = false;
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

bool ReturnCompleteHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int returnId = j.value("return_id", 0);
        int adminId = 0;
        std::string authErr;
        if (!Auth::requireAdmin(ctx_, j, adminId, authErr)) {
            resp = makeError(ErrorCodes::UNAUTHORIZED, authErr);
            return false;
        }
        if (sqlite3_exec(ctx_.db, "BEGIN IMMEDIATE", nullptr, nullptr, nullptr) != SQLITE_OK) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "退货完成处理繁忙");
            return false;
        }
        auto request = ctx_.returns->findById(returnId);
        if (!request || request->status != RETURN_APPROVED) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::RETURN_NOT_FOUND, "退货单不存在或尚未审核通过");
            return false;
        }
        if (!ctx_.returns->updateStatus(returnId, RETURN_DONE)) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "退货状态更新失败");
            return false;
        }
        appendAudit(ctx_.db, request->orderId, adminId, "complete_return",
                    ORDER_CANCELLED, ORDER_CANCELLED, "退货完成并确认退款");
        appendNotification(ctx_.db, request->userId, "return", "退货流程已完成");
        if (sqlite3_exec(ctx_.db, "COMMIT", nullptr, nullptr, nullptr) != SQLITE_OK) {
            sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "退货完成提交失败");
            return false;
        }
        resp = json({{"success", true}, {"return_id", returnId},
                     {"status", RETURN_DONE}, {"message", "退货流程已完成"}}).dump();
        return true;
    } catch (const std::exception& e) {
        sqlite3_exec(ctx_.db, "ROLLBACK", nullptr, nullptr, nullptr);
        resp = makeError(ErrorCodes::SYSTEM_BUSY, e.what());
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
        appendNotification(ctx_.db, order->userId, "order", target == ORDER_SHIPPED ? "订单已发货" : "订单状态已更新");
        resp = json({{"success", true}, {"order_id", orderId}, {"from_status", order->status}, {"status", target}}).dump(); return true;
    } catch (const std::exception& e) { resp = makeError(ErrorCodes::SYSTEM_BUSY, e.what()); return false; }
}

bool OrderAuditHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req); int userId = j.value("user_id", 0); std::string authErr;
        if (!Auth::requireUser(ctx_, j, userId, authErr)) { resp = makeError(ErrorCodes::UNAUTHORIZED, authErr); return false; }
        int orderId = j.value("order_id", 0); auto order = ctx_.orders->findById(orderId); auto viewer = ctx_.users->findById(userId);
        if (!order || (order->userId != userId && (!viewer || (viewer->role != "admin" && viewer->role != "operator")))) { resp = makeError(ErrorCodes::NO_PERMISSION, "无权查看该订单审计记录"); return false; }
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
        if (!Auth::requireAdmin(ctx_, j, loginUserId, authErr)) {
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
        auto serviceUser = ctx_.users->findById(userId);
        if (serviceUser && serviceUser->role != "customer") {
            resp = makeError(ErrorCodes::NO_PERMISSION, "管理员请使用客服工作台");
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
        // 每位用户维护一个未关闭工单，消息和工单时间线保持一致。
        int ticketId = 0;
        sqlite3_stmt* ticketStmt = nullptr;
        if (sqlite3_prepare_v2(ctx_.db, "SELECT id FROM service_tickets WHERE user_id=? AND status IN ('open','pending') ORDER BY id DESC LIMIT 1", -1, &ticketStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(ticketStmt, 1, userId);
            if (sqlite3_step(ticketStmt) == SQLITE_ROW) ticketId = sqlite3_column_int(ticketStmt, 0);
        }
        if (ticketStmt) { sqlite3_finalize(ticketStmt); ticketStmt = nullptr; }
        if (ticketId == 0 && sqlite3_prepare_v2(ctx_.db, "INSERT INTO service_tickets(user_id,subject) VALUES(?,?)", -1, &ticketStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(ticketStmt, 1, userId); sqlite3_bind_text(ticketStmt, 2, "在线咨询", -1, SQLITE_STATIC);
            if (sqlite3_step(ticketStmt) == SQLITE_DONE) ticketId = static_cast<int>(sqlite3_last_insert_rowid(ctx_.db));
        }
        if (ticketStmt) sqlite3_finalize(ticketStmt);
        if (ticketId <= 0) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "客服工单创建失败");
            return false;
        }
        sqlite3_stmt* messageStmt = nullptr;
        bool messageSaved = sqlite3_prepare_v2(ctx_.db,
            "INSERT INTO service_messages(user_id,direction,content) VALUES(?,?,?)",
            -1, &messageStmt, nullptr) == SQLITE_OK;
        if (messageSaved) {
            sqlite3_bind_int(messageStmt, 1, userId);
            sqlite3_bind_int(messageStmt, 2, 0);
            sqlite3_bind_text(messageStmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
            messageSaved = sqlite3_step(messageStmt) == SQLITE_DONE;
        }
        if (messageStmt) sqlite3_finalize(messageStmt);
        if (!messageSaved) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "客服消息保存失败");
            return false;
        }

        // 自动应答（模拟客服回复，避免长连接状态管理）
        std::string reply = "客服小助手：已收到您的消息，我们会尽快处理（工单已登记）。";
        messageStmt = nullptr;
        bool replySaved = sqlite3_prepare_v2(ctx_.db,
            "INSERT INTO service_messages(user_id,direction,content) VALUES(?,?,?)",
            -1, &messageStmt, nullptr) == SQLITE_OK;
        if (replySaved) {
            sqlite3_bind_int(messageStmt, 1, userId);
            sqlite3_bind_int(messageStmt, 2, 1);
            sqlite3_bind_text(messageStmt, 3, reply.c_str(), -1, SQLITE_TRANSIENT);
            replySaved = sqlite3_step(messageStmt) == SQLITE_DONE;
        }
        if (messageStmt) sqlite3_finalize(messageStmt);
        if (!replySaved) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "客服回复保存失败");
            return false;
        }
        sqlite3_stmt* touch = nullptr;
        if (ticketId > 0 && sqlite3_prepare_v2(ctx_.db, "UPDATE service_tickets SET last_message_at=CURRENT_TIMESTAMP,status='open' WHERE id=?", -1, &touch, nullptr) == SQLITE_OK) { sqlite3_bind_int(touch, 1, ticketId); sqlite3_step(touch); }
        if (touch) sqlite3_finalize(touch);
        appendNotification(ctx_.db, userId, "service", "客服已收到你的消息");
        json out;
        out["success"]  = true;
        out["received"] = content;
        out["reply"]    = reply;
        out["ticket_id"] = ticketId;
        resp = out.dump();
        return true;
    } catch (const std::exception& e) {
        resp = makeError(ErrorCodes::SYSTEM_BUSY, std::string("消息参数错误: ") + e.what());
        return false;
    }
}

bool ServiceReplyHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int adminId = 0;
        std::string authErr;
        if (!Auth::requireAdmin(ctx_, j, adminId, authErr)) {
            resp = makeError(ErrorCodes::UNAUTHORIZED, authErr);
            return false;
        }
        int userId = j.value("user_id", 0);
        std::string content = j.value("content", "");
        if (userId <= 0 || content.empty() || content.size() > 500) {
            resp = makeError(ErrorCodes::USER_NOT_FOUND, "invalid service reply");
            return false;
        }
        sqlite3_stmt* stmt = nullptr;
        bool saved = sqlite3_prepare_v2(ctx_.db,
            "INSERT INTO service_messages(user_id,direction,content) VALUES(?,?,?)",
            -1, &stmt, nullptr) == SQLITE_OK;
        if (saved) {
            sqlite3_bind_int(stmt, 1, userId);
            sqlite3_bind_int(stmt, 2, 1);
            sqlite3_bind_text(stmt, 3, content.c_str(), -1, SQLITE_TRANSIENT);
            saved = sqlite3_step(stmt) == SQLITE_DONE;
        }
        if (stmt) sqlite3_finalize(stmt);
        if (!saved) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "service reply save failed");
            return false;
        }
        sqlite3_stmt* ticketStmt = nullptr;
        if (sqlite3_prepare_v2(ctx_.db, "UPDATE service_tickets SET assigned_to=?,status='pending',last_message_at=CURRENT_TIMESTAMP WHERE user_id=? AND status IN ('open','pending')", -1, &ticketStmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(ticketStmt, 1, adminId); sqlite3_bind_int(ticketStmt, 2, userId); sqlite3_step(ticketStmt);
        }
        if (ticketStmt) sqlite3_finalize(ticketStmt);
        appendNotification(ctx_.db, userId, "service", "客服回复了你的消息");
        resp = json({{"success", true}, {"user_id", userId}, {"admin_id", adminId}}).dump();
        return true;
    } catch (const std::exception& e) {
        resp = makeError(ErrorCodes::SYSTEM_BUSY, e.what());
        return false;
    }
}

bool ServiceTicketsHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req); int uid = 0; std::string err;
        bool admin = Auth::requireAdmin(ctx_, j, uid, err);
        if (!admin && !Auth::requireLogin(ctx_, j, uid, err)) { resp = makeError(ErrorCodes::UNAUTHORIZED, err); return false; }
        int requested = j.value("user_id", uid);
        if (!admin) requested = uid;
        sqlite3_stmt* stmt = nullptr;
        const char* sql = admin ? "SELECT t.id,t.user_id,t.subject,t.status,t.assigned_to,t.last_message_at,t.created_at,t.closed_at,u.username,(SELECT COUNT(*) FROM service_messages m WHERE m.user_id=t.user_id AND m.direction=0) FROM service_tickets t JOIN users u ON u.id=t.user_id ORDER BY t.last_message_at DESC" : "SELECT t.id,t.user_id,t.subject,t.status,t.assigned_to,t.last_message_at,t.created_at,t.closed_at,u.username,(SELECT COUNT(*) FROM service_messages m WHERE m.user_id=t.user_id AND m.direction=1) FROM service_tickets t JOIN users u ON u.id=t.user_id WHERE t.user_id=? ORDER BY t.last_message_at DESC";
        if (sqlite3_prepare_v2(ctx_.db, sql, -1, &stmt, nullptr) != SQLITE_OK) { resp = makeError(ErrorCodes::SYSTEM_BUSY, "工单查询失败"); return false; }
        if (!admin) sqlite3_bind_int(stmt, 1, requested);
        json tickets = json::array();
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            int ticketId = sqlite3_column_int(stmt,0), ticketUser = sqlite3_column_int(stmt,1);
            json ticket = {{"ticket_id",ticketId},{"user_id",ticketUser},{"subject",safeText(stmt,2)},{"status",safeText(stmt,3)},{"assigned_to",sqlite3_column_int(stmt,4)},{"last_message_at",safeText(stmt,5)},{"created_at",safeText(stmt,6)},{"closed_at",safeText(stmt,7)},{"username",safeText(stmt,8)},{"message_count",sqlite3_column_int(stmt,9)}};
            ticket["messages"] = json::array();
            sqlite3_stmt* ms = nullptr;
            if (sqlite3_prepare_v2(ctx_.db, "SELECT direction,content,created_at FROM service_messages WHERE user_id=? ORDER BY id DESC LIMIT 50", -1, &ms, nullptr) == SQLITE_OK) {
                sqlite3_bind_int(ms, 1, ticketUser);
                while (sqlite3_step(ms) == SQLITE_ROW) ticket["messages"].push_back({{"direction",sqlite3_column_int(ms,0)},{"content",safeText(ms,1)},{"created_at",safeText(ms,2)}});
            }
            if (ms) sqlite3_finalize(ms);
            tickets.push_back(std::move(ticket));
        }
        sqlite3_finalize(stmt);
        resp = json({{"tickets",tickets}}).dump(); return true;
    } catch (const std::exception& e) { resp = makeError(ErrorCodes::SYSTEM_BUSY, e.what()); return false; }
}

bool ServiceTicketUpdateHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j=json::parse(req); int admin=0; std::string err;
        if (!Auth::requireAdmin(ctx_, j, admin, err)) { resp=makeError(ErrorCodes::UNAUTHORIZED, err); return false; }
        int id=j.value("ticket_id",0); std::string status=j.value("status","");
        if(id<=0 || (status!="open" && status!="pending" && status!="resolved" && status!="closed")){resp=makeError(ErrorCodes::SYSTEM_BUSY,"工单状态无效");return false;}
        sqlite3_stmt* s=nullptr; if(sqlite3_prepare_v2(ctx_.db,"UPDATE service_tickets SET status=?,assigned_to=?,closed_at=CASE WHEN ?='closed' THEN CURRENT_TIMESTAMP ELSE closed_at END WHERE id=?",-1,&s,nullptr)!=SQLITE_OK){resp=makeError(ErrorCodes::SYSTEM_BUSY,"工单更新失败");return false;}
        sqlite3_bind_text(s,1,status.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_int(s,2,admin); sqlite3_bind_text(s,3,status.c_str(),-1,SQLITE_TRANSIENT); sqlite3_bind_int(s,4,id); bool ok=sqlite3_step(s)==SQLITE_DONE; sqlite3_finalize(s);
        if(!ok){resp=makeError(ErrorCodes::SYSTEM_BUSY,"工单更新失败");return false;} resp=json({{"success",true},{"ticket_id",id},{"status",status}}).dump(); return true;
    }catch(const std::exception&e){resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what());return false;}
}

bool ReferralQueryHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j = json::parse(req);
        int userId = j.value("user_id", 0);
        std::string authErr;
        if (!Auth::requireUser(ctx_, j, userId, authErr)) {
            resp = makeError(ErrorCodes::UNAUTHORIZED, authErr);
            return false;
        }

        json out = json::object();
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(ctx_.db,
                "SELECT u.invite_code, i.id, i.username FROM users u LEFT JOIN users i ON i.id=u.inviter_id WHERE u.id=?",
                -1, &s, nullptr) != SQLITE_OK) {
            resp = makeError(ErrorCodes::SYSTEM_BUSY, "分销信息查询失败");
            return false;
        }
        sqlite3_bind_int(s, 1, userId);
        if (sqlite3_step(s) == SQLITE_ROW) {
            out["invite_code"] = safeText(s, 0);
            if (sqlite3_column_type(s, 1) == SQLITE_NULL) out["inviter"] = nullptr;
            else out["inviter"] = {{"user_id", sqlite3_column_int(s, 1)}, {"username", safeText(s, 2)}};
        }
        sqlite3_finalize(s);

        out["invitees"] = json::array();
        if (sqlite3_prepare_v2(ctx_.db, "SELECT id,username,register_date FROM users WHERE inviter_id=? ORDER BY id DESC", -1, &s, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(s, 1, userId);
            while (sqlite3_step(s) == SQLITE_ROW) out["invitees"].push_back({
                {"user_id", sqlite3_column_int(s, 0)}, {"username", safeText(s, 1)}, {"register_date", safeText(s, 2)}});
            sqlite3_finalize(s);
        }

        out["commissions"] = json::array();
        int total = 0, valid = 0, invalid = 0;
        if (sqlite3_prepare_v2(ctx_.db,
                "SELECT c.id,c.order_id,c.invitee_id,u.username,c.amount,c.status,c.created_at "
                "FROM commission_records c JOIN users u ON u.id=c.invitee_id "
                "WHERE c.inviter_id=? ORDER BY c.id DESC", -1, &s, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(s, 1, userId);
            while (sqlite3_step(s) == SQLITE_ROW) {
                int amount = sqlite3_column_int(s, 4), status = sqlite3_column_int(s, 5);
                total += amount;
                if (status == 2) invalid += amount; else valid += amount;
                out["commissions"].push_back({{"commission_id", sqlite3_column_int(s, 0)},
                    {"order_id", sqlite3_column_int(s, 1)}, {"invitee_id", sqlite3_column_int(s, 2)},
                    {"invitee_username", safeText(s, 3)}, {"amount", amount}, {"status", status},
                    {"created_at", safeText(s, 6)}});
            }
            sqlite3_finalize(s);
        }
        out["summary"] = {{"total", total}, {"valid", valid}, {"invalid", invalid}};
        resp = out.dump();
        return true;
    } catch (const std::exception& e) {
        resp = makeError(ErrorCodes::SYSTEM_BUSY, e.what());
        return false;
    }
}

bool PromoAdminHandler::execute(const std::string& req, std::string& resp) {
    try {
        auto j=json::parse(req); int admin=0; std::string err;
        if(!Auth::requireAdmin(ctx_,j,admin,err)){resp=makeError(ErrorCodes::UNAUTHORIZED,err);return false;}
        std::string action=j.value("action","list");
        auto appendPromoAudit = [&](int promotionId, const std::string& auditAction, const std::string& detail) {
            sqlite3_stmt* logStmt = nullptr;
            if (sqlite3_prepare_v2(ctx_.db, "INSERT INTO promotion_audit_logs(promotion_id,operator_id,action,detail) VALUES(?,?,?,?)", -1, &logStmt, nullptr) != SQLITE_OK) return false;
            sqlite3_bind_int(logStmt, 1, promotionId);
            sqlite3_bind_int(logStmt, 2, admin);
            sqlite3_bind_text(logStmt, 3, auditAction.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(logStmt, 4, detail.c_str(), -1, SQLITE_TRANSIENT);
            bool ok = sqlite3_step(logStmt) == SQLITE_DONE;
            sqlite3_finalize(logStmt);
            return ok;
        };
        if(action=="list") {
            sqlite3_stmt* s=nullptr; json a=json::array();
            const char* sql="SELECT id,name,type,param1,param2,param3,param4,param5,param6,start_time,end_time,status,exclusive_group,max_uses,per_user_limit FROM promotions ORDER BY id DESC";
            if(sqlite3_prepare_v2(ctx_.db,sql,-1,&s,nullptr)==SQLITE_OK) {
                while(sqlite3_step(s)==SQLITE_ROW) a.push_back({{"id",sqlite3_column_int(s,0)},{"name",safeText(s,1)},{"type",sqlite3_column_int(s,2)},{"param1",sqlite3_column_double(s,3)},{"param2",sqlite3_column_double(s,4)},{"param3",sqlite3_column_double(s,5)},{"param4",sqlite3_column_double(s,6)},{"param5",sqlite3_column_double(s,7)},{"param6",sqlite3_column_double(s,8)},{"start_time",safeText(s,9)},{"end_time",safeText(s,10)},{"status",sqlite3_column_int(s,11)},{"exclusive_group",safeText(s,12)},{"max_uses",sqlite3_column_int(s,13)},{"per_user_limit",sqlite3_column_int(s,14)}});
            }
            if(s) sqlite3_finalize(s); resp=json({{"promotions",a}}).dump(); return true;
        }
        if(action=="create") {
            std::string name=j.value("name",""); int type=j.value("type",1);
            if(name.empty() || name.size()>100 || type<1 || type>4 || j.value("max_uses",0)<0 || j.value("per_user_limit",0)<0) { resp=makeError(ErrorCodes::SYSTEM_BUSY,"促销规则参数不合法"); return false; }
            if(sqlite3_exec(ctx_.db,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)!=SQLITE_OK){resp=makeError(ErrorCodes::SYSTEM_BUSY,"促销创建繁忙");return false;}
            sqlite3_stmt* s=nullptr; const char* sql="INSERT INTO promotions(name,type,param1,param2,param3,param4,param5,param6,start_time,end_time,status,exclusive_group,max_uses,per_user_limit) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
            bool ok=sqlite3_prepare_v2(ctx_.db,sql,-1,&s,nullptr)==SQLITE_OK;
            if(ok){sqlite3_bind_text(s,1,name.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(s,2,type);for(int i=1;i<=6;++i){std::string k="param"+std::to_string(i);sqlite3_bind_double(s,2+i,j.value(k,0.0));}sqlite3_bind_text(s,9,j.value("start_time","").c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,10,j.value("end_time","").c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(s,11,j.value("status",1)?1:0);sqlite3_bind_text(s,12,j.value("exclusive_group","").c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(s,13,j.value("max_uses",0));sqlite3_bind_int(s,14,j.value("per_user_limit",0));ok=sqlite3_step(s)==SQLITE_DONE;}
            int id=static_cast<int>(sqlite3_last_insert_rowid(ctx_.db)); if(s)sqlite3_finalize(s);
            if(ok) ok=appendPromoAudit(id,"create","创建促销规则");
            if(!ok || sqlite3_exec(ctx_.db,"COMMIT",nullptr,nullptr,nullptr)!=SQLITE_OK){sqlite3_exec(ctx_.db,"ROLLBACK",nullptr,nullptr,nullptr);resp=makeError(ErrorCodes::SYSTEM_BUSY,"促销创建失败");return false;}
            resp=json({{"success",true},{"id",id}}).dump();return true;
        }
        int id=j.value("id",0); if(id<=0 || (action!="toggle" && action!="update")){resp=makeError(ErrorCodes::PRODUCT_NOT_FOUND,"促销操作无效");return false;}
        if(sqlite3_exec(ctx_.db,"BEGIN IMMEDIATE",nullptr,nullptr,nullptr)!=SQLITE_OK){resp=makeError(ErrorCodes::SYSTEM_BUSY,"促销更新繁忙");return false;}
        sqlite3_stmt* s=nullptr; bool ok=false;
        if(action=="toggle") {
            if(j.value("status",-1)!=0 && j.value("status",-1)!=1){sqlite3_exec(ctx_.db,"ROLLBACK",nullptr,nullptr,nullptr);resp=makeError(ErrorCodes::SYSTEM_BUSY,"促销状态无效");return false;}
            ok=sqlite3_prepare_v2(ctx_.db,"UPDATE promotions SET status=? WHERE id=?",-1,&s,nullptr)==SQLITE_OK; if(ok){sqlite3_bind_int(s,1,j.value("status",1));sqlite3_bind_int(s,2,id);ok=sqlite3_step(s)==SQLITE_DONE;}
        } else {
            std::string name=j.value("name",""); int type=j.value("type",1); if(name.empty()||name.size()>100||type<1||type>4||j.value("max_uses",0)<0||j.value("per_user_limit",0)<0){sqlite3_exec(ctx_.db,"ROLLBACK",nullptr,nullptr,nullptr);resp=makeError(ErrorCodes::SYSTEM_BUSY,"促销规则参数不合法");return false;}
            const char* sql="UPDATE promotions SET name=?,type=?,param1=?,param2=?,param3=?,param4=?,param5=?,param6=?,start_time=?,end_time=?,status=?,exclusive_group=?,max_uses=?,per_user_limit=? WHERE id=?";
            ok=sqlite3_prepare_v2(ctx_.db,sql,-1,&s,nullptr)==SQLITE_OK; if(ok){sqlite3_bind_text(s,1,name.c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(s,2,type);for(int i=1;i<=6;++i){std::string k="param"+std::to_string(i);sqlite3_bind_double(s,2+i,j.value(k,0.0));}sqlite3_bind_text(s,9,j.value("start_time","").c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_text(s,10,j.value("end_time","").c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(s,11,j.value("status",1)?1:0);sqlite3_bind_text(s,12,j.value("exclusive_group","").c_str(),-1,SQLITE_TRANSIENT);sqlite3_bind_int(s,13,j.value("max_uses",0));sqlite3_bind_int(s,14,j.value("per_user_limit",0));sqlite3_bind_int(s,15,id);ok=sqlite3_step(s)==SQLITE_DONE;}
        }
        if(s)sqlite3_finalize(s); if(ok)ok=sqlite3_changes(ctx_.db)>0 && appendPromoAudit(id,action,action=="toggle"?"启用/停用促销规则":"更新促销规则");
        if(!ok || sqlite3_exec(ctx_.db,"COMMIT",nullptr,nullptr,nullptr)!=SQLITE_OK){sqlite3_exec(ctx_.db,"ROLLBACK",nullptr,nullptr,nullptr);resp=makeError(ErrorCodes::SYSTEM_BUSY,"促销更新失败");return false;}
        resp=json({{"success",true},{"id",id}}).dump();return true;
    }catch(const std::exception&e){resp=makeError(ErrorCodes::SYSTEM_BUSY,e.what());return false;}
}
