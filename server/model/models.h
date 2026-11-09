// ============================================================
// models.h - 服务端实体层（Model）
// ============================================================
#pragma once
#include <string>
#include <vector>

// ---------- 状态枚举（消除魔法数字） ----------
// 订单状态
enum OrderStatus {
    ORDER_PENDING   = 0,  // 待支付
    ORDER_PAID      = 1,  // 已支付
    ORDER_SHIPPED   = 2,  // 已发货
    ORDER_COMPLETED = 3,  // 已完成
    ORDER_CANCELLED = 4,  // 已取消（已退款）
    ORDER_RETURNING = 5   // 退货中
};

// 退货单状态
enum ReturnStatus {
    RETURN_PENDING  = 0,  // 待审核
    RETURN_APPROVED = 1,  // 审核通过
    RETURN_REJECTED = 2,  // 审核拒绝
    RETURN_DONE     = 3   // 已完成
};

// 商品
struct Product {
    int id = 0;
    std::string name;
    int price = 0;              // 单位：分（避免浮点误差）
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
    int points = 0;
};

// 订单明细（快照）
struct OrderItem {
    int id = 0;
    int orderId = 0;
    int productId = 0;
    std::string productName;  // 快照
    int price = 0;            // 快照，单位：分
    int quantity = 0;
    int skuId = 0;
    std::string skuLabel;
};

// 订单
struct Order {
    int id = 0;
    std::string orderNo;
    int userId = 0;
    int totalAmount = 0;       // 单位：分
    int discountAmount = 0;    // 单位：分
    int finalAmount = 0;      // 单位：分
    int couponId = 0;
    int status = ORDER_PAID;   // 0待支付 1已支付 2已发货 3已完成 4已取消 5退货中
    std::string createTime;
    std::string addressReceiver;
    std::string addressPhone;
    std::string addressDetail;
    std::string receiveTime;
    int shippingFee = 0;
    int pointsUsed = 0;
    std::vector<OrderItem> items;
};

struct Address {
    int id = 0;
    int userId = 0;
    std::string receiverName;
    std::string phone;
    std::string detail;
    bool isDefault = false;
};

// 退货申请
struct ReturnRequest {
    int id = 0;
    int orderId = 0;
    int userId = 0;
    std::string reason;
    int status = 0;           // 0待审核 1审核通过 2审核拒绝 3已完成
    int origStatus = 0;       // 申请时订单原状态（审核拒绝时恢复）
    std::string applyTime;
    std::string auditTime;
};

// 促销活动
struct Promotion {
    int id = 0;
    std::string name;
    int type = 1;             // 1打折 2降价 3满减券 4阶梯折扣
    double param1 = 0.0;      // 通用：折扣率 / 直降金额 / 满额门槛；阶梯折扣：第1阶梯件数
    double param2 = 0.0;      // 满减金额 / 第1阶梯折扣率
    double param3 = 0.0;      // 阶梯折扣：第2阶梯件数
    double param4 = 0.0;      // 阶梯折扣：第2阶梯折扣率
    double param5 = 0.0;      // 阶梯折扣：第3阶梯件数
    double param6 = 0.0;      // 阶梯折扣：第3阶梯折扣率
    int status = 1;
};
