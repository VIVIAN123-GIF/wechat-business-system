-- ============================================================
-- 微商系统 数据库结构 (SQLite)
-- 依据: 第三周报告《数据库详细设计》
-- 表: users / products / orders / order_items / returns / promotions
-- ============================================================

PRAGMA foreign_keys = ON;

-- 用户表
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    password TEXT NOT NULL,
    real_name TEXT,
    phone TEXT,
    level INTEGER DEFAULT 1,          -- 1:普通 2:VIP
    register_date DATETIME DEFAULT CURRENT_TIMESTAMP,
    last_login DATETIME
);

-- 商品表
CREATE TABLE IF NOT EXISTS products (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    price REAL NOT NULL CHECK(price >= 0),
    stock INTEGER NOT NULL DEFAULT 0 CHECK(stock >= 0),
    description TEXT,
    category TEXT,
    image_url TEXT,
    status INTEGER DEFAULT 1,         -- 0:下架 1:上架
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    update_time DATETIME
);

-- 订单主表
CREATE TABLE IF NOT EXISTS orders (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    order_no TEXT NOT NULL UNIQUE,    -- 业务订单号，如 "MS20260909001"
    user_id INTEGER NOT NULL,
    total_amount REAL NOT NULL,       -- 原价合计
    discount_amount REAL DEFAULT 0,   -- 优惠总金额
    final_amount REAL NOT NULL,       -- 实付金额
    status INTEGER DEFAULT 0,         -- 0待支付 1已支付 2已发货 3已完成 4已取消 5退货中
    pay_time DATETIME,
    create_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

-- 订单明细表（快照存储）
CREATE TABLE IF NOT EXISTS order_items (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    order_id INTEGER NOT NULL,
    product_id INTEGER NOT NULL,
    product_name TEXT NOT NULL,       -- 购买时的商品名称（快照）
    price REAL NOT NULL,              -- 购买时的单价（快照）
    quantity INTEGER NOT NULL CHECK(quantity > 0),
    FOREIGN KEY (order_id) REFERENCES orders(id) ON DELETE CASCADE
);

-- 退货申请表
CREATE TABLE IF NOT EXISTS returns (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    order_id INTEGER NOT NULL,
    user_id INTEGER NOT NULL,
    reason TEXT,
    status INTEGER DEFAULT 0,         -- 0待审核 1审核通过 2审核拒绝 3已完成
    apply_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    audit_time DATETIME,
    FOREIGN KEY (order_id) REFERENCES orders(id)
);

-- 促销活动表（便于动态扩展）
CREATE TABLE IF NOT EXISTS promotions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    type INTEGER NOT NULL,            -- 1:打折 2:降价 3:满减券
    param1 REAL,                      -- 折扣率 / 降价金额 / 满额门槛
    param2 REAL,                      -- (满减)优惠金额
    start_time DATETIME,
    end_time DATETIME,
    status INTEGER DEFAULT 1
);

-- 为提高查询效率创建索引
CREATE INDEX IF NOT EXISTS idx_orders_user_id ON orders(user_id);
CREATE INDEX IF NOT EXISTS idx_orders_status ON orders(status);
CREATE INDEX IF NOT EXISTS idx_order_items_order_id ON order_items(order_id);
CREATE INDEX IF NOT EXISTS idx_returns_order_id ON returns(order_id);
