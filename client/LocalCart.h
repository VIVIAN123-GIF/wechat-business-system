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
    void addItem(int productId, const std::string& name, double price, int qty = 1, int skuId = 0, const std::string& skuLabel = {});
    void removeItem(int productId, int skuId = 0);
    void updateQuantity(int productId, int newQty, int skuId = 0);
    void clear();
    bool isEmpty() const;
    int getTotalCount() const;
    double getTotalPrice() const;
    const std::map<std::pair<int, int>, CartItem>& getItems() const { return items_; }

    // 序列化为结算请求 JSON（items 部分）
    std::string itemsToJson() const;

private:
    std::map<std::pair<int, int>, CartItem> items_;
};
