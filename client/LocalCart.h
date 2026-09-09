// ============================================================
// LocalCart.h - 本地购物车
// ============================================================
#pragma once
#include <map>
#include <vector>
#include <string>
#include "model/models.h"

class LocalCart {
public:
    void addItem(int productId, const std::string& name, double price, int qty = 1);
    void removeItem(int productId);
    void updateQuantity(int productId, int newQty);
    void clear();
    bool isEmpty() const;
    int getTotalCount() const;
    double getTotalPrice() const;
    const std::map<int, CartItem>& getItems() const { return items_; }

    // 序列化为结算请求 JSON（items 部分）
    std::string itemsToJson() const;

private:
    std::map<int, CartItem> items_;  // key: productId
};
