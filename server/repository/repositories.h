// ============================================================
// repositories.h - 持久层（Repository）
// ============================================================
#pragma once
#include <sqlite3.h>
#include <string>
#include <vector>
#include <memory>
#include "../model/models.h"

// 商品仓储
class ProductRepository {
public:
    explicit ProductRepository(sqlite3* db) : db_(db) {}

    std::vector<Product> findAll();          // 仅上架商品
    std::shared_ptr<Product> findById(int id);
    // 原子扣减库存：UPDATE ... SET stock=stock-qty WHERE id=? AND stock>=qty
    bool deductStock(int id, int quantity);
    bool updateStock(int id, int newStock);
    bool restoreStock(int id, int quantity); // 退货时回补

private:
    sqlite3* db_;
};

// 订单仓储
class OrderRepository {
public:
    explicit OrderRepository(sqlite3* db) : db_(db) {}

    // 插入订单头 + 明细（调用方需处于事务中）
    bool insert(Order& order);
    std::vector<Order> findByUserId(int userId);  // 含明细
    std::shared_ptr<Order> findById(int orderId);
    bool updateStatus(int orderId, int newStatus);

private:
    sqlite3* db_;
    Order loadOrderWithItems(sqlite3_stmt* stmt);
};

// 用户仓储
class UserRepository {
public:
    explicit UserRepository(sqlite3* db) : db_(db) {}

    std::shared_ptr<User> findByUsername(const std::string& username);
    std::shared_ptr<User> findById(int id);
    bool insert(User& user);                  // 返回自增 id
    bool updateLastLogin(int userId);

private:
    sqlite3* db_;
};

// 退货仓储
class ReturnRepository {
public:
    explicit ReturnRepository(sqlite3* db) : db_(db) {}

    bool insert(const ReturnRequest& req);
    std::vector<ReturnRequest> findByUserId(int userId);

private:
    sqlite3* db_;
};

// 促销仓储
class PromotionRepository {
public:
    explicit PromotionRepository(sqlite3* db) : db_(db) {}

    std::vector<Promotion> findAllAvailable();
    std::shared_ptr<Promotion> findById(int id);

private:
    sqlite3* db_;
};
