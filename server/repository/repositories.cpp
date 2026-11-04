#include "repositories.h"
#include "../Logger.h"

// ---------------- ProductRepository ----------------
std::vector<Product> ProductRepository::findAll() {
    std::vector<Product> result;
    const char* sql = "SELECT id, name, price, stock, description, category, status "
                      "FROM products WHERE status = 1";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Product p;
        p.id   = sqlite3_column_int(stmt, 0);
        p.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        p.price = sqlite3_column_int(stmt, 2);
        p.stock = sqlite3_column_int(stmt, 3);
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
            p.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
            p.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        p.status = sqlite3_column_int(stmt, 6);
        result.push_back(p);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::shared_ptr<Product> ProductRepository::findById(int id) {
    const char* sql = "SELECT id, name, price, stock, description, category, status "
                      "FROM products WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return nullptr;
    sqlite3_bind_int(stmt, 1, id);
    std::shared_ptr<Product> p;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        p = std::make_shared<Product>();
        p->id    = sqlite3_column_int(stmt, 0);
        p->name  = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        p->price = sqlite3_column_int(stmt, 2);
        p->stock = sqlite3_column_int(stmt, 3);
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
            p->description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
            p->category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        p->status = sqlite3_column_int(stmt, 6);
    }
    sqlite3_finalize(stmt);
    return p;
}

std::vector<Product> ProductRepository::findByKeyword(const std::string& keyword) {
    std::vector<Product> result;
    if (keyword.empty()) return result;
    std::string esc;
    for (char ch : keyword) {
        if (ch == '%' || ch == '_' || ch == '\\') esc += '\\';
        esc += ch;
    }
    std::string sql =
        "SELECT id, name, price, stock, description, category, status "
        "FROM products WHERE status = 1 AND ("
        " name LIKE ? ESCAPE '\\' OR category LIKE ? ESCAPE '\\' OR description LIKE ? ESCAPE '\\')";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) return result;
    std::string pattern = "%" + esc + "%";
    sqlite3_bind_text(stmt, 1, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pattern.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, pattern.c_str(), -1, SQLITE_TRANSIENT);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Product p;
        p.id = sqlite3_column_int(stmt, 0);
        p.name = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        p.price = sqlite3_column_int(stmt, 2);
        p.stock = sqlite3_column_int(stmt, 3);
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) p.description = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) p.category = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        p.status = sqlite3_column_int(stmt, 6);
        result.push_back(p);
    }
    sqlite3_finalize(stmt);
    return result;
}

bool ProductRepository::deductStock(int id, int quantity) {
    // 原子扣减：条件 stock >= quantity，返回是否成功（防超卖）
    const char* sql = "UPDATE products SET stock = stock - ?, update_time = datetime('now') "
                      "WHERE id = ? AND stock >= ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, quantity);
    sqlite3_bind_int(stmt, 2, id);
    sqlite3_bind_int(stmt, 3, quantity);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE) && (sqlite3_changes(db_) > 0);
    sqlite3_finalize(stmt);
    return ok;
}

bool ProductRepository::restoreStock(int id, int quantity) {
    const char* sql = "UPDATE products SET stock = stock + ?, update_time = datetime('now') WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, quantity);
    sqlite3_bind_int(stmt, 2, id);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

// ---------------- OrderRepository ----------------
bool OrderRepository::insert(Order& order) {
    const char* sql = "INSERT INTO orders (order_no, user_id, total_amount, discount_amount, "
                      "final_amount, status, create_time) VALUES (?, ?, ?, ?, ?, ?, datetime('now'))";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, order.orderNo.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, order.userId);
    sqlite3_bind_int(stmt, 3, order.totalAmount);
    sqlite3_bind_int(stmt, 4, order.discountAmount);
    sqlite3_bind_int(stmt, 5, order.finalAmount);
    sqlite3_bind_int(stmt, 6, order.status);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    if (!ok) return false;

    order.id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    auto cleanupPartialOrder = [this, &order]() {
        sqlite3_stmt* cleanup = nullptr;
        if (sqlite3_prepare_v2(db_, "DELETE FROM order_items WHERE order_id=?", -1, &cleanup, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(cleanup, 1, order.id);
            sqlite3_step(cleanup);
        }
        if (cleanup) sqlite3_finalize(cleanup);
        cleanup = nullptr;
        if (sqlite3_prepare_v2(db_, "DELETE FROM orders WHERE id=?", -1, &cleanup, nullptr) == SQLITE_OK) {
            sqlite3_bind_int(cleanup, 1, order.id);
            sqlite3_step(cleanup);
        }
        if (cleanup) sqlite3_finalize(cleanup);
    };

    // 明细
    const char* sqlItem = "INSERT INTO order_items (order_id, product_id, product_name, price, quantity) "
                          "VALUES (?, ?, ?, ?, ?)";
    for (const auto& item : order.items) {
        sqlite3_stmt* stmt2 = nullptr;
        if (sqlite3_prepare_v2(db_, sqlItem, -1, &stmt2, nullptr) != SQLITE_OK) {
            cleanupPartialOrder();
            return false;
        }
        sqlite3_bind_int(stmt2, 1, order.id);
        sqlite3_bind_int(stmt2, 2, item.productId);
        sqlite3_bind_text(stmt2, 3, item.productName.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int(stmt2, 4, item.price);
        sqlite3_bind_int(stmt2, 5, item.quantity);
        ok = (sqlite3_step(stmt2) == SQLITE_DONE);
        sqlite3_finalize(stmt2);
        if (!ok) {
            cleanupPartialOrder();
            return false;
        }
    }
    return true;
}

Order OrderRepository::loadOrderWithItems(sqlite3_stmt* stmt) {
    Order o;
    o.id            = sqlite3_column_int(stmt, 0);
    o.orderNo       = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    o.userId        = sqlite3_column_int(stmt, 2);
    o.totalAmount   = sqlite3_column_int(stmt, 3);
    o.discountAmount= sqlite3_column_int(stmt, 4);
    o.finalAmount   = sqlite3_column_int(stmt, 5);
    o.status        = sqlite3_column_int(stmt, 6);
    if (sqlite3_column_type(stmt, 7) != SQLITE_NULL)
        o.createTime = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    return o;
}

std::vector<Order> OrderRepository::findByUserId(int userId) {
    std::vector<Order> orders;
    const char* sql = "SELECT id, order_no, user_id, total_amount, discount_amount, "
                      "final_amount, status, create_time FROM orders WHERE user_id = ? "
                      "ORDER BY id DESC";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return orders;
    sqlite3_bind_int(stmt, 1, userId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        orders.push_back(loadOrderWithItems(stmt));
    }
    sqlite3_finalize(stmt);

    // 加载明细（每个订单一次查询）
    const char* sqlItems = "SELECT id, order_id, product_id, product_name, price, quantity "
                           "FROM order_items WHERE order_id = ?";
    for (auto& order : orders) {
        sqlite3_stmt* stmt2 = nullptr;
        if (sqlite3_prepare_v2(db_, sqlItems, -1, &stmt2, nullptr) != SQLITE_OK) continue;
        sqlite3_bind_int(stmt2, 1, order.id);
        while (sqlite3_step(stmt2) == SQLITE_ROW) {
            OrderItem it;
            it.id          = sqlite3_column_int(stmt2, 0);
            it.orderId     = sqlite3_column_int(stmt2, 1);
            it.productId   = sqlite3_column_int(stmt2, 2);
            it.productName = reinterpret_cast<const char*>(sqlite3_column_text(stmt2, 3));
            it.price       = sqlite3_column_int(stmt2, 4);
            it.quantity    = sqlite3_column_int(stmt2, 5);
            order.items.push_back(it);
        }
        sqlite3_finalize(stmt2);
    }
    return orders;
}

std::shared_ptr<Order> OrderRepository::findById(int orderId) {
    const char* sql = "SELECT id, order_no, user_id, total_amount, discount_amount, "
                      "final_amount, status, create_time FROM orders WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return nullptr;
    sqlite3_bind_int(stmt, 1, orderId);
    std::shared_ptr<Order> o;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        o = std::make_shared<Order>(loadOrderWithItems(stmt));
    }
    sqlite3_finalize(stmt);
    if (!o) return nullptr;

    const char* sqlItems = "SELECT id, order_id, product_id, product_name, price, quantity "
                           "FROM order_items WHERE order_id = ?";
    sqlite3_stmt* stmt2 = nullptr;
    if (sqlite3_prepare_v2(db_, sqlItems, -1, &stmt2, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt2, 1, orderId);
        while (sqlite3_step(stmt2) == SQLITE_ROW) {
            OrderItem it;
            it.id          = sqlite3_column_int(stmt2, 0);
            it.orderId     = sqlite3_column_int(stmt2, 1);
            it.productId   = sqlite3_column_int(stmt2, 2);
            it.productName = reinterpret_cast<const char*>(sqlite3_column_text(stmt2, 3));
            it.price       = sqlite3_column_int(stmt2, 4);
            it.quantity    = sqlite3_column_int(stmt2, 5);
            o->items.push_back(it);
        }
        sqlite3_finalize(stmt2);
    }
    return o;
}

bool OrderRepository::updateStatus(int orderId, int newStatus) {
    const char* sql = "UPDATE orders SET status = ? WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, newStatus);
    sqlite3_bind_int(stmt, 2, orderId);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

bool OrderRepository::updateStatusIf(int orderId, int newStatus, int s1, int s2, int s3) {
    const char* sql = "UPDATE orders SET status = ? WHERE id = ? AND status IN (?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, newStatus);
    sqlite3_bind_int(stmt, 2, orderId);
    sqlite3_bind_int(stmt, 3, s1);
    sqlite3_bind_int(stmt, 4, s2);
    sqlite3_bind_int(stmt, 5, s3);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE) && (sqlite3_changes(db_) > 0);
    sqlite3_finalize(stmt);
    return ok;
}

// ---------------- UserRepository ----------------
std::shared_ptr<User> UserRepository::findByUsername(const std::string& username) {
    const char* sql = "SELECT id, username, password, real_name, phone, level FROM users WHERE username = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return nullptr;
    sqlite3_bind_text(stmt, 1, username.c_str(), -1, SQLITE_TRANSIENT);
    std::shared_ptr<User> u;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        u = std::make_shared<User>();
        u->id       = sqlite3_column_int(stmt, 0);
        u->username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        u->password = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
            u->realName = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL)
            u->phone = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
        u->level = sqlite3_column_int(stmt, 5);
    }
    sqlite3_finalize(stmt);
    return u;
}

std::shared_ptr<User> UserRepository::findById(int id) {
    const char* sql = "SELECT id, username, password, real_name, phone, level FROM users WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return nullptr;
    sqlite3_bind_int(stmt, 1, id);
    std::shared_ptr<User> u;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        u = std::make_shared<User>();
        u->id       = sqlite3_column_int(stmt, 0);
        u->username = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        u->password = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
        u->level    = sqlite3_column_int(stmt, 5);
    }
    sqlite3_finalize(stmt);
    return u;
}

bool UserRepository::insert(User& user) {
    const char* sql = "INSERT INTO users (username, password, real_name, phone, level) VALUES (?, ?, ?, ?, ?)";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, user.username.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, user.password.c_str(), -1, SQLITE_TRANSIENT);
    if (user.realName.empty()) sqlite3_bind_null(stmt, 3);
    else sqlite3_bind_text(stmt, 3, user.realName.c_str(), -1, SQLITE_TRANSIENT);
    if (user.phone.empty()) sqlite3_bind_null(stmt, 4);
    else sqlite3_bind_text(stmt, 4, user.phone.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 5, user.level);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    if (ok) user.id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    return ok;
}

bool UserRepository::updateLastLogin(int userId) {
    const char* sql = "UPDATE users SET last_login = datetime('now') WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, userId);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

// ---------------- ReturnRepository ----------------
bool ReturnRepository::insert(ReturnRequest& req) {
    const char* sql = "INSERT INTO returns (order_id, user_id, reason, status, orig_status, apply_time) "
                      "VALUES (?, ?, ?, ?, ?, datetime('now'))";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, req.orderId);
    sqlite3_bind_int(stmt, 2, req.userId);
    if (req.reason.empty()) sqlite3_bind_null(stmt, 3);
    else sqlite3_bind_text(stmt, 3, req.reason.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, req.status);
    sqlite3_bind_int(stmt, 5, req.origStatus);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    if (ok) req.id = static_cast<int>(sqlite3_last_insert_rowid(db_));
    return ok;
}

std::vector<ReturnRequest> ReturnRepository::findByUserId(int userId) {
    std::vector<ReturnRequest> result;
    const char* sql = "SELECT id, order_id, user_id, reason, status, apply_time, audit_time "
                      "FROM returns WHERE user_id = ? ORDER BY id DESC";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    sqlite3_bind_int(stmt, 1, userId);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ReturnRequest r;
        r.id       = sqlite3_column_int(stmt, 0);
        r.orderId  = sqlite3_column_int(stmt, 1);
        r.userId   = sqlite3_column_int(stmt, 2);
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL)
            r.reason = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        r.status   = sqlite3_column_int(stmt, 4);
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL)
            r.applyTime = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 5));
        if (sqlite3_column_type(stmt, 6) != SQLITE_NULL)
            r.auditTime = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        result.push_back(r);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::vector<ReturnRequest> ReturnRepository::findAll() {
    std::vector<ReturnRequest> result;
    const char* sql = "SELECT id, order_id, user_id, reason, status, orig_status, apply_time, audit_time "
                      "FROM returns ORDER BY id DESC";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        ReturnRequest r;
        r.id = sqlite3_column_int(stmt, 0);
        r.orderId = sqlite3_column_int(stmt, 1);
        r.userId = sqlite3_column_int(stmt, 2);
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) r.reason = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        r.status = sqlite3_column_int(stmt, 4);
        r.origStatus = sqlite3_column_int(stmt, 5);
        if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) r.applyTime = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        if (sqlite3_column_type(stmt, 7) != SQLITE_NULL) r.auditTime = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
        result.push_back(r);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::shared_ptr<ReturnRequest> ReturnRepository::findById(int returnId) {
    const char* sql = "SELECT id, order_id, user_id, reason, status, orig_status, apply_time, audit_time "
                      "FROM returns WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return nullptr;
    sqlite3_bind_int(stmt, 1, returnId);
    std::shared_ptr<ReturnRequest> r;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        r = std::make_shared<ReturnRequest>();
        r->id = sqlite3_column_int(stmt, 0);
        r->orderId = sqlite3_column_int(stmt, 1);
        r->userId = sqlite3_column_int(stmt, 2);
        if (sqlite3_column_type(stmt, 3) != SQLITE_NULL) r->reason = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
        r->status = sqlite3_column_int(stmt, 4);
        r->origStatus = sqlite3_column_int(stmt, 5);
        if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) r->applyTime = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 6));
        if (sqlite3_column_type(stmt, 7) != SQLITE_NULL) r->auditTime = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 7));
    }
    sqlite3_finalize(stmt);
    return r;
}

bool ReturnRepository::updateStatus(int returnId, int newStatus) {
    const char* sql = "UPDATE returns SET status = ?, audit_time = datetime('now') WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, newStatus);
    sqlite3_bind_int(stmt, 2, returnId);
    bool ok = (sqlite3_step(stmt) == SQLITE_DONE);
    sqlite3_finalize(stmt);
    return ok;
}

// ---------------- PromotionRepository ----------------
std::vector<Promotion> PromotionRepository::findAllAvailable() {
    std::vector<Promotion> result;
    const char* sql = "SELECT id, name, type, param1, param2, param3, param4, param5, param6, status FROM promotions "
                      "WHERE status = 1 AND (end_time IS NULL OR end_time > datetime('now')) "
                      "ORDER BY id";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return result;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Promotion p;
        p.id     = sqlite3_column_int(stmt, 0);
        p.name   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        p.type   = sqlite3_column_int(stmt, 2);
        p.param1 = sqlite3_column_double(stmt, 3);
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) p.param2 = sqlite3_column_double(stmt, 4);
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) p.param3 = sqlite3_column_double(stmt, 5);
        if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) p.param4 = sqlite3_column_double(stmt, 6);
        if (sqlite3_column_type(stmt, 7) != SQLITE_NULL) p.param5 = sqlite3_column_double(stmt, 7);
        if (sqlite3_column_type(stmt, 8) != SQLITE_NULL) p.param6 = sqlite3_column_double(stmt, 8);
        p.status = sqlite3_column_int(stmt, 9);
        result.push_back(p);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::shared_ptr<Promotion> PromotionRepository::findById(int id) {
    const char* sql = "SELECT id, name, type, param1, param2, param3, param4, param5, param6, status FROM promotions WHERE id = ?";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return nullptr;
    sqlite3_bind_int(stmt, 1, id);
    std::shared_ptr<Promotion> p;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        p = std::make_shared<Promotion>();
        p->id     = sqlite3_column_int(stmt, 0);
        p->name   = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
        p->type   = sqlite3_column_int(stmt, 2);
        p->param1 = sqlite3_column_double(stmt, 3);
        if (sqlite3_column_type(stmt, 4) != SQLITE_NULL) p->param2 = sqlite3_column_double(stmt, 4);
        if (sqlite3_column_type(stmt, 5) != SQLITE_NULL) p->param3 = sqlite3_column_double(stmt, 5);
        if (sqlite3_column_type(stmt, 6) != SQLITE_NULL) p->param4 = sqlite3_column_double(stmt, 6);
        if (sqlite3_column_type(stmt, 7) != SQLITE_NULL) p->param5 = sqlite3_column_double(stmt, 7);
        if (sqlite3_column_type(stmt, 8) != SQLITE_NULL) p->param6 = sqlite3_column_double(stmt, 8);
        p->status = sqlite3_column_int(stmt, 9);
    }
    sqlite3_finalize(stmt);
    return p;
}
