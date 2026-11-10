// ============================================================
// LocalCart.cpp - 控制台客户端本地购物车
// ============================================================
#include "LocalCart.h"
#include <nlohmann/json.hpp>

// 加购：同一商品累加数量，不同商品新增一行
void LocalCart::addItem(int productId, const std::string& name, double price, int qty, int skuId, const std::string& skuLabel) {
    auto key = std::make_pair(productId, skuId);
    auto it = items_.find(key);
    if (it != items_.end()) {
        it->second.quantity += qty;
    } else {
        CartItem item;
        item.productId = productId;
        item.skuId = skuId;
        item.skuLabel = skuLabel;
        item.productName = name;
        item.price = price;
        item.quantity = qty;
        items_[key] = item;
    }
}

void LocalCart::removeItem(int productId, int skuId) { items_.erase(std::make_pair(productId, skuId)); }

// 改数量：<=0 就删掉
void LocalCart::updateQuantity(int productId, int newQty, int skuId) {
    auto it = items_.find(std::make_pair(productId, skuId));
    if (it != items_.end()) {
        if (newQty <= 0) items_.erase(it);
        else it->second.quantity = newQty;
    }
}

void LocalCart::clear() { items_.clear(); }

bool LocalCart::isEmpty() const { return items_.empty(); }

int LocalCart::getTotalCount() const {
    int n = 0;
    for (const auto& kv : items_) n += kv.second.quantity;
    return n;
}

double LocalCart::getTotalPrice() const {
    double total = 0;
    for (const auto& kv : items_) total += kv.second.getSubtotal();
    return total;
}

// 转成结算请求里的 items 数组
std::string LocalCart::itemsToJson() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& kv : items_) {
        nlohmann::json item;
        item["product_id"] = kv.second.productId;
        item["sku_id"] = kv.second.skuId;
        item["quantity"] = kv.second.quantity;
        arr.push_back(item);
    }
    return arr.dump();
}
