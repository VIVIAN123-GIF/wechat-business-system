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
        if (!hasColumn(db, "promotions", "param3")) {
            if (sqlite3_exec(db,
                "ALTER TABLE promotions ADD COLUMN param3 REAL;"
                "ALTER TABLE promotions ADD COLUMN param4 REAL;"
                "ALTER TABLE promotions ADD COLUMN param5 REAL;"
                "ALTER TABLE promotions ADD COLUMN param6 REAL;",
                nullptr, nullptr, &errMsg) != SQLITE_OK) {
                LOG_ERROR(std::string("促销表迁移失败: ") + (errMsg ? errMsg : "unknown"));
                if (errMsg) sqlite3_free(errMsg);
                return 1;
            }
            LOG_INFO("已迁移 promotions 表：新增阶梯折扣字段 param3~param6");
        }

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
                    << sqlite3_column_double(stmt, 1)
                    << " | 优惠 ¥" << sqlite3_column_double(stmt, 2);
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

    // 4. 主线程等待退出指令
    std::cout << "\n输入 q 并回车以停止服务..." << std::endl;
    std::string cmd;
    while (std::getline(std::cin, cmd)) {
        if (cmd == "q" || cmd == "Q") break;
    }
    server.stop();
    return 0;
}
