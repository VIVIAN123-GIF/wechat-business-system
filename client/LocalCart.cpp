#include "LocalCart.h"
#include <nlohmann/json.hpp>

void LocalCart::addItem(int productId, const std::string& name, double price, int qty) {
    auto it = items_.find(productId);
    if (it != items_.end()) {
        it->second.quantity += qty;
    } else {
        CartItem item;
        item.productId = productId;
        item.productName = name;
        item.price = price;
        item.quantity = qty;
        items_[productId] = item;
    }
}

void LocalCart::removeItem(int productId) { items_.erase(productId); }

void LocalCart::updateQuantity(int productId, int newQty) {
    auto it = items_.find(productId);
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

std::string LocalCart::itemsToJson() const {
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& kv : items_) {
        nlohmann::json item;
        item["product_id"] = kv.second.productId;
        item["quantity"] = kv.second.quantity;
        arr.push_back(item);
    }
    return arr.dump();
}
