// ============================================================
// main.cpp - 微商系统服务端入口
// ============================================================
#ifdef _WIN32
#include <winsock2.h>  // 必须先于 windows.h
#include <windows.h>
#endif
#include <iostream>
#include <cstring>
#include <string>
#include <sstream>
#include <iomanip>
#include <atomic>
#include <chrono>
#include <thread>
#include "TcpServer.h"
#include "Logger.h"
#include "database/SQLiteHelper.h"
#include "handlers/handlers.h"
#include "../common/ProtocolHeader.h"
#include "../common/SHA256.h"

// ---------------- 数据库初始化（从 external SQL 文件读取，单份维护） ----------------
#include <fstream>

// 读取整个文本文件为字符串（UTF-8），失败返回空串
std::string readFile(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) return "";
    std::string content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    return content;
}

// 检查表是否已存在某列（用于老库迁移）
bool hasColumn(sqlite3* db, const char* table, const char* column) {
    std::string sql = "PRAGMA table_info(" + std::string(table) + ");";
    sqlite3_stmt* stmt = nullptr;
    bool found = false;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) == SQLITE_OK) {
        while (sqlite3_step(stmt) == SQLITE_ROW) {
            const char* name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
            if (name && strcmp(name, column) == 0) { found = true; break; }
        }
    }
    if (stmt) sqlite3_finalize(stmt);
    return found;
}

// 观察者模式监听者：订单创建时打印日志
class OrderCreatedLogger : public OrderEventListener {
public:
    void onOrderCreated(const Order& order) override {
        LOG_INFO("【观察者通知】新订单生成: 单号=" + order.orderNo +
                 " 实付=" + std::to_string(order.finalAmount) +
                 " 优惠=" + std::to_string(order.discountAmount));
    }
};

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);  // 控制台 UTF-8 输出
#endif

    LOG_INFO("========== 微商系统服务端 ==========");

    // 1. 初始化数据库（单连接完成建表 + 种子，避免多连接写锁冲突）
    try {
        DatabaseManager::instance().init("ms.db");
        sqlite3* db = DatabaseManager::instance().getConnection();
        if (!db) throw std::runtime_error("无法获取数据库连接");

        char* errMsg = nullptr;
        // 从 external SQL 文件读取 DDL 与种子（单份维护，不再内嵌双份）
        std::string schemaSQL = readFile("database/schema.sql");
        if (schemaSQL.empty()) {
            LOG_ERROR("无法读取 database/schema.sql，请在项目根目录运行服务端");
            return 1;
        }
        if (sqlite3_exec(db, schemaSQL.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
            LOG_ERROR(std::string("数据库建表失败: ") + (errMsg ? errMsg : "unknown"));
            if (errMsg) sqlite3_free(errMsg);
            return 1;
        }
        if (!hasColumn(db, "orders", "address_receiver")) {
            if (sqlite3_exec(db, "ALTER TABLE orders ADD COLUMN address_receiver TEXT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR(std::string("订单收货人字段迁移失败: ") + (errMsg ? errMsg : "unknown"));
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }
        if (!hasColumn(db, "orders", "address_phone")) {
            if (sqlite3_exec(db, "ALTER TABLE orders ADD COLUMN address_phone TEXT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR(std::string("订单收货电话字段迁移失败: ") + (errMsg ? errMsg : "unknown"));
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }
        if (!hasColumn(db, "orders", "address_detail")) {
            if (sqlite3_exec(db, "ALTER TABLE orders ADD COLUMN address_detail TEXT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR(std::string("订单收货地址字段迁移失败: ") + (errMsg ? errMsg : "unknown"));
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }
        if (!hasColumn(db, "orders", "coupon_id")) {
            if (sqlite3_exec(db, "ALTER TABLE orders ADD COLUMN coupon_id INTEGER;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR(std::string("订单优惠券字段迁移失败: ") + (errMsg ? errMsg : "unknown"));
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }
        if (!hasColumn(db, "orders", "receive_time")) {
            if (sqlite3_exec(db, "ALTER TABLE orders ADD COLUMN receive_time DATETIME;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR(std::string("订单签收字段迁移失败: ") + (errMsg ? errMsg : "unknown"));
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }
        if (!hasColumn(db, "users", "points")) {
            if (sqlite3_exec(db, "ALTER TABLE users ADD COLUMN points INTEGER NOT NULL DEFAULT 0;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR(std::string("用户积分字段迁移失败: ") + (errMsg ? errMsg : "unknown"));
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }
        if (!hasColumn(db, "users", "role")) {
            if (sqlite3_exec(db, "ALTER TABLE users ADD COLUMN role TEXT NOT NULL DEFAULT 'customer'; UPDATE users SET role='admin' WHERE username='admin' AND level>=2;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR("用户角色字段迁移失败");
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }
        if (!hasColumn(db, "users", "inviter_id")) {
            if (sqlite3_exec(db, "ALTER TABLE users ADD COLUMN inviter_id INTEGER;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR("用户邀请关系字段迁移失败");
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }
        if (!hasColumn(db, "users", "invite_code")) {
            if (sqlite3_exec(db, "ALTER TABLE users ADD COLUMN invite_code TEXT;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR("用户邀请码字段迁移失败");
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }
        if (!hasColumn(db, "orders", "shipping_fee")) {
            if (sqlite3_exec(db, "ALTER TABLE orders ADD COLUMN shipping_fee INTEGER NOT NULL DEFAULT 0;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR("订单运费字段迁移失败");
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }
        if (!hasColumn(db, "orders", "points_used")) {
            if (sqlite3_exec(db, "ALTER TABLE orders ADD COLUMN points_used INTEGER NOT NULL DEFAULT 0;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR("订单积分字段迁移失败");
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }
        if (!hasColumn(db, "reviews", "image_url")) {
            if (sqlite3_exec(db, "ALTER TABLE reviews ADD COLUMN image_url TEXT NOT NULL DEFAULT '';", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR(std::string("评价图片字段迁移失败: ") + (errMsg ? errMsg : "unknown"));
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }
        if (!hasColumn(db, "order_items", "sku_id")) {
            if (sqlite3_exec(db, "ALTER TABLE order_items ADD COLUMN sku_id INTEGER;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR(std::string("order_items SKU migration failed: ") + (errMsg ? errMsg : "unknown"));
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }
        if (!hasColumn(db, "order_items", "sku_label")) {
            if (sqlite3_exec(db, "ALTER TABLE order_items ADD COLUMN sku_label TEXT NOT NULL DEFAULT '';", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR(std::string("order_items SKU label migration failed: ") + (errMsg ? errMsg : "unknown"));
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }
        if (!hasColumn(db, "returns", "audit_comment")) {
            if (sqlite3_exec(db, "ALTER TABLE returns ADD COLUMN audit_comment TEXT NOT NULL DEFAULT '';", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR("退货审核意见字段迁移失败");
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }
        if (!hasColumn(db, "returns", "refund_amount")) {
            if (sqlite3_exec(db, "ALTER TABLE returns ADD COLUMN refund_amount INTEGER NOT NULL DEFAULT 0;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR("退货退款金额字段迁移失败");
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }
        if (!hasColumn(db, "promotions", "exclusive_group")) {
            if (sqlite3_exec(db, "ALTER TABLE promotions ADD COLUMN exclusive_group TEXT NOT NULL DEFAULT '';", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR("促销互斥组字段迁移失败");
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }
        if (!hasColumn(db, "promotions", "max_uses")) {
            if (sqlite3_exec(db, "ALTER TABLE promotions ADD COLUMN max_uses INTEGER NOT NULL DEFAULT 0;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR("促销总次数字段迁移失败");
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }
        if (!hasColumn(db, "promotions", "per_user_limit")) {
            if (sqlite3_exec(db, "ALTER TABLE promotions ADD COLUMN per_user_limit INTEGER NOT NULL DEFAULT 0;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR("促销个人次数字段迁移失败");
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }
        // 首次运行则插入种子数据
        sqlite3_stmt* stmt = nullptr;
        const char* cnt = "SELECT COUNT(*) FROM users";
        bool seeded = false;
        if (sqlite3_prepare_v2(db, cnt, -1, &stmt, nullptr) == SQLITE_OK &&
            sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) == 0) {
            std::string seedSQL = readFile("database/init_data.sql");
            if (seedSQL.empty()) {
                LOG_ERROR("无法读取 database/init_data.sql");
                return 1;
            }
            if (sqlite3_exec(db, seedSQL.c_str(), nullptr, nullptr, &errMsg) == SQLITE_OK) {
                seeded = true;
            } else {
                LOG_ERROR(std::string("种子数据插入失败: ") + (errMsg ? errMsg : "unknown"));
                if (errMsg) sqlite3_free(errMsg);
            }
        }
        if (stmt) sqlite3_finalize(stmt);
        if (seeded) LOG_INFO("已初始化种子数据（用户/商品/促销）");

        // ===== 老库迁移：promotions 表补充阶梯折扣参数字段（param3~param6） =====
        bool tierColumnsMigrated = false;
        for (const char* column : {"param3", "param4", "param5", "param6"}) {
            if (hasColumn(db, "promotions", column)) continue;
            std::string sql = "ALTER TABLE promotions ADD COLUMN " + std::string(column) + " REAL;";
            if (sqlite3_exec(db, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR(std::string("促销表字段迁移失败（") + column + "): " + (errMsg ? errMsg : "unknown"));
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
            tierColumnsMigrated = true;
        }
        if (tierColumnsMigrated) LOG_INFO("已迁移 promotions 表：补充缺失的阶梯折扣字段");

        // 促销使用记录表：老库也需要补齐，供使用次数限制校验。
        sqlite3_exec(db,
            "CREATE TABLE IF NOT EXISTS order_promotions ("
            "order_id INTEGER NOT NULL, promotion_id INTEGER NOT NULL, user_id INTEGER NOT NULL, "
            "created_at DATETIME DEFAULT CURRENT_TIMESTAMP, PRIMARY KEY(order_id,promotion_id), "
            "FOREIGN KEY(order_id) REFERENCES orders(id) ON DELETE CASCADE, "
            "FOREIGN KEY(promotion_id) REFERENCES promotions(id) ON DELETE CASCADE, "
            "FOREIGN KEY(user_id) REFERENCES users(id) ON DELETE CASCADE);",
            nullptr, nullptr, nullptr);

        // ===== 阶梯折扣促销种子（老库不重插用户数据，这里单独补促销） =====
        stmt = nullptr;
        const char* cntTier = "SELECT COUNT(*) FROM promotions WHERE type = 4";
        bool hasTier = false;
        if (sqlite3_prepare_v2(db, cntTier, -1, &stmt, nullptr) == SQLITE_OK &&
            sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_int(stmt, 0) > 0) {
            hasTier = true;
        }
        if (stmt) sqlite3_finalize(stmt);
        if (!hasTier) {
            const char* kTierSeed =
                "INSERT INTO promotions (name, type, param1, param2, param3, param4, param5, param6, status) VALUES"
                "('第2件9折',4,1,1.0,2,0.9,NULL,NULL,1),"
                "('第3件免单',4,2,1.0,3,0.0,NULL,NULL,1);";
            if (sqlite3_exec(db, kTierSeed, nullptr, nullptr, &errMsg) == SQLITE_OK) {
                LOG_INFO("已补充阶梯折扣促销种子（第2件9折 / 第3件免单）");
            } else {
                LOG_ERROR(std::string("阶梯促销种子插入失败: ") + (errMsg ? errMsg : "unknown"));
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }

        // ===== 老库迁移：returns 表补充 orig_status 字段（审核拒绝时恢复订单状态） =====
        if (!hasColumn(db, "returns", "orig_status")) {
            if (sqlite3_exec(db,
                "ALTER TABLE returns ADD COLUMN orig_status INTEGER DEFAULT 1;",
                nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR(std::string("退货表迁移失败: ") + (errMsg ? errMsg : "unknown"));
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
            LOG_INFO("已迁移 returns 表：新增 orig_status 字段");
        }

        if (!hasColumn(db, "groups", "unit_price")) {
            if (sqlite3_exec(db,
                "ALTER TABLE groups ADD COLUMN unit_price INTEGER NOT NULL DEFAULT 0;"
                "UPDATE groups SET unit_price = COALESCE((SELECT price FROM products WHERE products.id = groups.product_id), 0) WHERE unit_price = 0;",
                nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR(std::string("拼团表迁移失败: ") + (errMsg ? errMsg : "unknown"));
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
        }

        // ===== 老库迁移：存量明文密码就地哈希（杜绝明文落库） =====
        {
            sqlite3_stmt* sel = nullptr;
            int migrated = 0;
            const char* q = "SELECT id, username, password FROM users";
            if (sqlite3_prepare_v2(db, q, -1, &sel, nullptr) == SQLITE_OK) {
                while (sqlite3_step(sel) == SQLITE_ROW) {
                    int id = sqlite3_column_int(sel, 0);
                    std::string name = reinterpret_cast<const char*>(sqlite3_column_text(sel, 1));
                    std::string pwd = reinterpret_cast<const char*>(sqlite3_column_text(sel, 2));
                    // 已是 64 位 hex 摘要则跳过，否则按新规则哈希
                    bool isDigest = (pwd.size() == 64) &&
                        (pwd.find_first_not_of("0123456789abcdefABCDEF") == std::string::npos);
                    if (!isDigest) {
                        std::string hashed = hashPassword(name, pwd);
                        sqlite3_stmt* upd = nullptr;
                        const char* u = "UPDATE users SET password = ? WHERE id = ?";
                        if (sqlite3_prepare_v2(db, u, -1, &upd, nullptr) == SQLITE_OK) {
                            sqlite3_bind_text(upd, 1, hashed.c_str(), -1, SQLITE_TRANSIENT);
                            sqlite3_bind_int(upd, 2, id);
                            if (sqlite3_step(upd) == SQLITE_DONE) ++migrated;
                        }
                        if (upd) sqlite3_finalize(upd);
                    }
                }
                sqlite3_finalize(sel);
            }
            if (migrated > 0) LOG_INFO("已迁移 " + std::to_string(migrated) + " 个明文密码为 SHA-256 摘要");
        }

        LOG_INFO("数据库初始化完成: ms.db");

        // ===== 服务端 UI 层：启动时输出运营数据概览（交易结果/数据统计） =====
        {
            sqlite3_stmt* stmt = nullptr;
            const char* overview = "SELECT COUNT(*), COALESCE(SUM(final_amount),0),"
                                   " COALESCE(SUM(discount_amount),0)"
                                   " FROM orders WHERE status != 4";
            if (sqlite3_prepare_v2(db, overview, -1, &stmt, nullptr) == SQLITE_OK &&
                sqlite3_step(stmt) == SQLITE_ROW) {
                std::ostringstream oss;
                oss << "运营数据概览: 订单 " << sqlite3_column_int(stmt, 0)
                    << " 笔 | 实收 ¥" << std::fixed << std::setprecision(2)
                    << sqlite3_column_int64(stmt, 1) / 100.0
                    << " | 优惠 ¥" << sqlite3_column_int64(stmt, 2) / 100.0;
                LOG_INFO(oss.str());
            }
            if (stmt) sqlite3_finalize(stmt);
        }
    } catch (const std::exception& e) {
        LOG_ERROR(std::string("数据库初始化失败: ") + e.what());
        return 1;
    }

    // 2. 注册观察者
    static OrderCreatedLogger orderLogger;
    OrderEventBus::instance().subscribe(&orderLogger);

    // 3. 启动服务端
    TcpServer server(8888);
    if (!server.start()) {
        LOG_ERROR("服务端启动失败");
        return 1;
    }

    std::atomic<bool> cleanupRunning{true};
    // 答辩演示期间关闭后台定时清理（拼团超时/待支付订单超时），
    // 避免长时间运行触发已知的 cleanupContext 边界崩溃；
    // 上线前把下面这行改成 true 即可恢复完整功能。
    const bool enableBackgroundCleanup = false;
    std::thread cleanupThread([&] {
        if (!enableBackgroundCleanup) return;
        while (cleanupRunning.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(30));
            if (!cleanupRunning.load()) break;
            HandlerContext cleanupContext;
            cleanupContext.db = DatabaseManager::instance().getConnection();
            cleanupContext.products = std::make_shared<ProductRepository>(cleanupContext.db);
            cleanupContext.orders = std::make_shared<OrderRepository>(cleanupContext.db);
            cleanupContext.users = std::make_shared<UserRepository>(cleanupContext.db);
            cleanupContext.returns = std::make_shared<ReturnRepository>(cleanupContext.db);
            cleanupContext.promos = std::make_shared<PromotionRepository>(cleanupContext.db);
            cleanupContext.promoFactory = std::make_shared<PromotionFactory>(cleanupContext.promos);
            cleanupContext.stockMutex = &TcpServer::sharedStockMutex();
            cleanupContext.sessions = &SessionManager::instance();
            try {
                expireGroupsNow(cleanupContext);
                expirePendingOrdersNow(cleanupContext);
            } catch (const std::exception& e) {
                LOG_ERROR(std::string("后台清理任务失败: ") + e.what());
            } catch (...) {
                LOG_ERROR("后台清理任务失败: unknown exception");
            }
        }
    });

    // 4. 主线程等待退出指令
    std::cout << "\n输入 q 并回车以停止服务..." << std::endl;
    std::string cmd;
    while (std::getline(std::cin, cmd)) {
        if (cmd == "q" || cmd == "Q") break;
    }
    cleanupRunning.store(false);
    if (cleanupThread.joinable()) cleanupThread.join();
    server.stop();
    return 0;
}
