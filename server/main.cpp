// ============================================================
// main.cpp - 微商系统服务端入口
// ============================================================
#ifdef _WIN32
#include <winsock2.h>  // 必须先于 windows.h
#include <windows.h>
#endif
#include <iostream>
#include <string>
#include "TcpServer.h"
#include "Logger.h"
#include "database/SQLiteHelper.h"
#include "handlers/handlers.h"
#include "../common/ProtocolHeader.h"

// ---------------- 数据库初始化（内嵌 DDL，程序自包含） ----------------
namespace {
const char* kSchemaSQL =
    "CREATE TABLE IF NOT EXISTS users ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " username TEXT NOT NULL UNIQUE,"
    " password TEXT NOT NULL,"
    " real_name TEXT, phone TEXT,"
    " level INTEGER DEFAULT 1,"
    " register_date DATETIME DEFAULT CURRENT_TIMESTAMP,"
    " last_login DATETIME);"

    "CREATE TABLE IF NOT EXISTS products ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " name TEXT NOT NULL, price REAL NOT NULL CHECK(price >= 0),"
    " stock INTEGER NOT NULL DEFAULT 0 CHECK(stock >= 0),"
    " description TEXT, category TEXT, image_url TEXT,"
    " status INTEGER DEFAULT 1,"
    " create_time DATETIME DEFAULT CURRENT_TIMESTAMP,"
    " update_time DATETIME);"

    "CREATE TABLE IF NOT EXISTS orders ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " order_no TEXT NOT NULL UNIQUE,"
    " user_id INTEGER NOT NULL,"
    " total_amount REAL NOT NULL, discount_amount REAL DEFAULT 0,"
    " final_amount REAL NOT NULL,"
    " status INTEGER DEFAULT 0,"
    " pay_time DATETIME, create_time DATETIME DEFAULT CURRENT_TIMESTAMP,"
    " FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE);"

    "CREATE TABLE IF NOT EXISTS order_items ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " order_id INTEGER NOT NULL, product_id INTEGER NOT NULL,"
    " product_name TEXT NOT NULL, price REAL NOT NULL,"
    " quantity INTEGER NOT NULL CHECK(quantity > 0),"
    " FOREIGN KEY (order_id) REFERENCES orders(id) ON DELETE CASCADE);"

    "CREATE TABLE IF NOT EXISTS returns ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " order_id INTEGER NOT NULL, user_id INTEGER NOT NULL,"
    " reason TEXT, status INTEGER DEFAULT 0,"
    " apply_time DATETIME DEFAULT CURRENT_TIMESTAMP, audit_time DATETIME,"
    " FOREIGN KEY (order_id) REFERENCES orders(id));"

    "CREATE TABLE IF NOT EXISTS promotions ("
    " id INTEGER PRIMARY KEY AUTOINCREMENT,"
    " name TEXT NOT NULL, type INTEGER NOT NULL,"
    " param1 REAL, param2 REAL,"
    " start_time DATETIME, end_time DATETIME, status INTEGER DEFAULT 1);"

    "CREATE INDEX IF NOT EXISTS idx_orders_user_id ON orders(user_id);"
    "CREATE INDEX IF NOT EXISTS idx_orders_status ON orders(status);"
    "CREATE INDEX IF NOT EXISTS idx_order_items_order_id ON order_items(order_id);"
    "CREATE INDEX IF NOT EXISTS idx_returns_order_id ON returns(order_id);";

const char* kSeedSQL =
    "INSERT INTO users (username, password, real_name, phone, level) VALUES"
    "('admin','123456','管理员','13800000001',2),"
    "('user1','123456','测试用户一','13800000002',1),"
    "('user2','123456','测试用户二','13800000003',1);"

    "INSERT INTO products (name, price, stock, description, category) VALUES"
    "('高端护肤品套装',299.00,15,'深层补水保湿，明星单品组合','美妆'),"
    "('有机茶叶礼盒',188.00,32,'高山有机绿茶，送礼佳品','食品'),"
    "('手工皮具钱包',399.00,8,'头层牛皮，手工缝制','箱包'),"
    "('降噪无线耳机',899.00,5,'主动降噪，续航30小时','数码'),"
    "('智能手环',249.00,20,'心率血氧监测，50米防水','数码'),"
    "('限量款背包',1299.00,1,'仅此一件，售完即止','箱包');"

    "INSERT INTO promotions (name, type, param1, param2, status) VALUES"
    "('全场9折',1,0.90,NULL,1),"
    "('满200减30',3,200,30,1),"
    "('满500减100',3,500,100,1),"
    "('数码直降50',2,50,NULL,1);";
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
        if (sqlite3_exec(db, kSchemaSQL, nullptr, nullptr, &errMsg) != SQLITE_OK) {
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
            if (sqlite3_exec(db, kSeedSQL, nullptr, nullptr, &errMsg) == SQLITE_OK) {
                seeded = true;
            } else {
                LOG_ERROR(std::string("种子数据插入失败: ") + (errMsg ? errMsg : "unknown"));
                if (errMsg) sqlite3_free(errMsg);
            }
        }
        if (stmt) sqlite3_finalize(stmt);
        if (seeded) LOG_INFO("已初始化种子数据（用户/商品/促销）");
        LOG_INFO("数据库初始化完成: ms.db");
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
