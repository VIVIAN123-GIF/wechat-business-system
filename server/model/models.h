// ============================================================
// models.h - 服务端实体层（Model）
// ============================================================
#pragma once
#include <string>
#include <vector>

// 商品
struct Product {
    int id = 0;
    std::string name;
    double price = 0.0;
    int stock = 0;
    std::string description;
    std::string category;
    int status = 1;
};

// 用户
struct User {
    int id = 0;
    std::string username;
    std::string password;
    std::string realName;
    std::string phone;
    int level = 1;          // 1普通 2VIP
};

// 订单明细（快照）
struct OrderItem {
    int id = 0;
    int orderId = 0;
    int productId = 0;
    std::string productName;  // 快照
    double price = 0.0;       // 快照
    int quantity = 0;
};

// 订单
struct Order {
    int id = 0;
    std::string orderNo;
    int userId = 0;
    double totalAmount = 0.0;
    double discountAmount = 0.0;
    double finalAmount = 0.0;
    int status = 1;           // 0待支付 1已支付 2已发货 3已完成 4已取消 5退货中
    std::string createTime;
    std::vector<OrderItem> items;
};

// 退货申请
struct ReturnRequest {
    int id = 0;
    int orderId = 0;
    int userId = 0;
    std::string reason;
    int status = 0;           // 0待审核 1审核通过 2审核拒绝 3已完成
    std::string applyTime;
    std::string auditTime;
};

// 促销活动
struct Promotion {
    int id = 0;
    std::string name;
    int type = 1;             // 1打折 2降价 3满减券
    double param1 = 0.0;      // 折扣率 / 直降金额 / 满额门槛
    double param2 = 0.0;      // 满减金额
    int status = 1;
};
