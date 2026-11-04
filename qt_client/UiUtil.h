// ============================================================
// UiUtil.h - UI 辅助小工具（金额格式化、错误信息提取）
// ============================================================
#pragma once
#include <QString>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// 分 -> "299.00"
inline QString yuan(int cents) {
    return QString::number(cents / 100.0, 'f', 2);
}
inline QString yuanD(double cents) {
    return QString::number(cents / 100.0, 'f', 2);
}
// 从错误响应体取提示文字
inline QString errMsg(const json& j) {
    return QString::fromStdString(j.value("err_msg", "未知错误"));
}
// 订单状态码 -> 中文
inline QString orderStatusText(int s) {
    switch (s) {
        case 1: return QStringLiteral("已支付");
        case 2: return QStringLiteral("已发货");
        case 3: return QStringLiteral("已完成");
        case 4: return QStringLiteral("已取消");
        case 5: return QStringLiteral("退货中");
        default: return QStringLiteral("未知");
    }
}
// 退货状态码 -> 中文
inline QString returnStatusText(int s) {
    switch (s) {
        case 0: return QStringLiteral("待审核");
        case 1: return QStringLiteral("已通过");
        case 2: return QStringLiteral("已拒绝");
        default: return QStringLiteral("未知");
    }
}
