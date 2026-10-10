// ============================================================
// ProtocolHeader.h - 协议头定义（客户端/服务端共享）
// 格式: Magic(4) + Version(4) + Code(4) + Length(4) = 16 字节
// ============================================================
#pragma once
#include <cstdint>
#include <cstring>

#pragma pack(push, 1)
struct ProtocolHeader {
    uint32_t magic;      // 魔术字 0x4D495F53 ("MI_S")
    uint32_t version;    // 版本号 0x00000001
    uint32_t code;       // 协议码
    uint32_t length;     // 数据体长度（不含头）
};
#pragma pack(pop)

namespace ProtocolConstants {
    constexpr uint32_t MAGIC   = 0x4D495F53;   // "MI_S"
    constexpr uint32_t VERSION = 0x00000001;
    constexpr size_t   HEADER_SIZE = sizeof(ProtocolHeader);

    // 商品
    constexpr uint32_t C_REQ_PRODUCT_LIST  = 1001;
    constexpr uint32_t S_RES_PRODUCT_LIST  = 2001;
    constexpr uint32_t C_REQ_PRODUCT_DETAIL = 1002;
    constexpr uint32_t S_RES_PRODUCT_DETAIL = 2002;
    // 商品搜索（按名称/分类模糊查询）
    constexpr uint32_t C_REQ_SEARCH        = 1005;
    constexpr uint32_t S_RES_SEARCH        = 2005;
    // 账号
    constexpr uint32_t C_REQ_LOGIN        = 1006;
    constexpr uint32_t S_RES_LOGIN        = 2006;
    constexpr uint32_t C_REQ_REGISTER     = 1007;
    constexpr uint32_t S_RES_REGISTER     = 2007;
    // 购物车（服务端仅库存校验）
    constexpr uint32_t C_REQ_ADD_CART     = 1010;
    constexpr uint32_t S_RES_ADD_CART     = 2010;
    // 促销
    constexpr uint32_t C_REQ_PROMO_LIST   = 1020;
    constexpr uint32_t S_RES_PROMO_LIST   = 2020;
    // 结算
    constexpr uint32_t C_REQ_CHECKOUT     = 1030;
    constexpr uint32_t S_RES_CHECKOUT     = 2030;
    // 订单
    constexpr uint32_t C_REQ_ORDER_QUERY  = 1040;
    constexpr uint32_t S_RES_ORDER_QUERY  = 2040;
    // 售后
    constexpr uint32_t C_REQ_RETURN       = 1050;
    constexpr uint32_t S_RES_RETURN       = 2050;
    // 销售统计（服务端 UI 层：数据统计分析界面）
    constexpr uint32_t C_REQ_STATS        = 1060;
    constexpr uint32_t S_RES_STATS        = 2060;
    // 客服消息（社交：联系客服）
    constexpr uint32_t C_REQ_SERVICE_MSG  = 1065;
    constexpr uint32_t S_RES_SERVICE_MSG  = 2065;
    // 售后扩展：退货单查询 / 退货审核（管理员）
    constexpr uint32_t C_REQ_RETURN_LIST  = 1070;
    constexpr uint32_t S_RES_RETURN_LIST  = 2070;
    constexpr uint32_t C_REQ_RETURN_AUDIT = 1080;
    constexpr uint32_t S_RES_RETURN_AUDIT = 2080;
    // 系统
    constexpr uint32_t C_REQ_HEARTBEAT    = 1999;
    constexpr uint32_t S_RES_HEARTBEAT    = 2999;
    // 错误
    constexpr uint32_t S_ERR_GENERAL      = 4001;
    constexpr uint32_t S_ERR_UNAUTH       = 4002;
}

namespace ErrorCodes {
    constexpr int STOCK_NOT_ENOUGH     = 1001;
    constexpr int PRODUCT_NOT_FOUND    = 1002;
    constexpr int USER_NOT_FOUND       = 1003;
    constexpr int CART_EMPTY           = 1004;
    constexpr int ORDER_NOT_FOUND      = 1005;
    constexpr int ORDER_STATUS_FORBID  = 1006;
    constexpr int PROMO_EXPIRED        = 1007;
    constexpr int DUPLICATE_SUBMIT     = 1008;
    constexpr int SYSTEM_BUSY          = 1009;
    constexpr int NETWORK_TIMEOUT      = 1010;
    constexpr int UNAUTHORIZED         = 1011;  // 未登录/会话无效（协议码 4002）
    constexpr int RETURN_NOT_FOUND     = 1012;  // 退货单不存在或已审核
    constexpr int NO_PERMISSION        = 1013;  // 无权限（非管理员/非本人）
}
