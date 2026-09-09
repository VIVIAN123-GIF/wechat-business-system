// ============================================================
// models.h - 客户端模型
// ============================================================
#pragma once
#include <string>
#include <vector>

struct ProductInfo {
    int id = 0;
    std::string name;
    double price = 0.0;
    int stock = 0;
    std::string category;
};

struct CartItem {
    int productId = 0;
    std::string productName;
    double price = 0.0;
    int quantity = 0;
    double getSubtotal() const { return price * quantity; }
};

struct OrderInfo {
    int orderId = 0;
    std::string orderNo;
    double original = 0.0;
    double discount = 0.0;
    double final = 0.0;
    int status = 0;
    std::string createTime;
    std::vector<CartItem> items;  // 复用购物车项结构承载明细
};

struct PromoInfo {
    int id = 0;
    std::string name;
    int type = 0;
    double param1 = 0.0;
    double param2 = 0.0;
};
