-- ============================================================
-- 微商系统 数据库结构 (SQLite)
-- 依据: 《数据库详细设计》
-- 表: users / products / orders / order_items / returns / promotions
-- ============================================================

PRAGMA foreign_keys = ON;

-- 用户表
CREATE TABLE IF NOT EXISTS users (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    username TEXT NOT NULL UNIQUE,
    password TEXT NOT NULL,               -- 密码 SHA-256 摘要：SHA256("微商系统$"||username||"#"||明文)
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
    price INTEGER NOT NULL CHECK(price >= 0),  -- 单位：分（整数存储，避免浮点误差）
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
    total_amount INTEGER NOT NULL,     -- 原价合计（分）
    discount_amount INTEGER DEFAULT 0, -- 优惠总金额（分）
    final_amount INTEGER NOT NULL,    -- 实付金额（分）
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
    price INTEGER NOT NULL,            -- 购买时的单价（快照，分）
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
    orig_status INTEGER DEFAULT 1,    -- 申请时订单原状态（审核拒绝时恢复）
    apply_time DATETIME DEFAULT CURRENT_TIMESTAMP,
    audit_time DATETIME,
    FOREIGN KEY (order_id) REFERENCES orders(id)
);

-- 促销活动表（便于动态扩展）
CREATE TABLE IF NOT EXISTS promotions (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    name TEXT NOT NULL,
    type INTEGER NOT NULL,            -- 1:打折 2:降价 3:满减券 4:阶梯折扣
    param1 REAL,                      -- 打折:折扣率 / 降价:降价金额 / 满减:满额门槛 / 阶梯:第1阶梯件数
    param2 REAL,                      -- (满减)优惠金额 / 阶梯:第1阶梯折扣率
    param3 REAL,                      -- 阶梯折扣:第2阶梯件数
    param4 REAL,                      -- 阶梯折扣:第2阶梯折扣率
    param5 REAL,                      -- 阶梯折扣:第3阶梯件数
    param6 REAL,                      -- 阶梯折扣:第3阶梯折扣率（超出最高档按此档延续）
    start_time DATETIME,
    end_time DATETIME,
    status INTEGER DEFAULT 1
);

-- 为提高查询效率创建索引
CREATE INDEX IF NOT EXISTS idx_orders_user_id ON orders(user_id);
CREATE INDEX IF NOT EXISTS idx_orders_status ON orders(status);
CREATE INDEX IF NOT EXISTS idx_order_items_order_id ON order_items(order_id);
CREATE INDEX IF NOT EXISTS idx_returns_order_id ON returns(order_id);

CREATE TABLE IF NOT EXISTS order_audit_logs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    order_id INTEGER NOT NULL,
    operator_id INTEGER,
    action TEXT NOT NULL,
    from_status INTEGER,
    to_status INTEGER,
    detail TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (order_id) REFERENCES orders(id),
    FOREIGN KEY (operator_id) REFERENCES users(id)
);

CREATE TABLE IF NOT EXISTS groups (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    group_no TEXT NOT NULL UNIQUE,
    product_id INTEGER NOT NULL,
    creator_id INTEGER NOT NULL,
    unit_price INTEGER NOT NULL CHECK(unit_price >= 0),
    target_count INTEGER NOT NULL CHECK(target_count >= 2),
    member_count INTEGER NOT NULL DEFAULT 1,
    status INTEGER NOT NULL DEFAULT 0,
    deadline DATETIME NOT NULL,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (product_id) REFERENCES products(id),
    FOREIGN KEY (creator_id) REFERENCES users(id)
);

CREATE TABLE IF NOT EXISTS group_members (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    group_id INTEGER NOT NULL,
    user_id INTEGER NOT NULL,
    joined_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    UNIQUE(group_id, user_id),
    FOREIGN KEY (group_id) REFERENCES groups(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id) ON DELETE CASCADE
);

CREATE TABLE IF NOT EXISTS group_orders (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    group_id INTEGER NOT NULL,
    user_id INTEGER NOT NULL,
    order_id INTEGER NOT NULL UNIQUE,
    UNIQUE(group_id, user_id),
    FOREIGN KEY (group_id) REFERENCES groups(id) ON DELETE CASCADE,
    FOREIGN KEY (user_id) REFERENCES users(id),
    FOREIGN KEY (order_id) REFERENCES orders(id)
);

CREATE INDEX IF NOT EXISTS idx_order_audit_order_id ON order_audit_logs(order_id);
CREATE INDEX IF NOT EXISTS idx_groups_status_deadline ON groups(status, deadline);
CREATE INDEX IF NOT EXISTS idx_group_orders_group_id ON group_orders(group_id);
