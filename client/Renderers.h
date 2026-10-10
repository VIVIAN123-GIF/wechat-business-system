// ============================================================
// Renderers.h - 两种 UI 风格（策略模式具体策略）
// SimpleRenderer: 极简（纯文字） / FancyRenderer: 详细（ASCII 边框）
// 每种风格可注入主题色（UITheme），实现"风格 × 配色"组合
// 注意：含 std::setw 的表格单元格不包 ANSI 色（避免转义序列计入列宽）
// ============================================================
#pragma once
#include <iostream>
#include <iomanip>
#include <sstream>
#include "UIRenderer.h"
#include "UITheme.h"

// 订单状态码 -> 中文（0待支付 1已支付 2已发货 3已完成 4已取消 5退货中）
inline std::string orderStatusName(int status) {
    switch (status) {
        case 0: return "待支付";
        case 1: return "已支付";
        case 2: return "已发货";
        case 3: return "已完成";
        case 4: return "已取消";
        case 5: return "退货中";
        default: return "未知";
    }
}

// 退货状态码 -> 中文（0无退货 1退货待审核 2退货已通过 3退货已拒绝）
inline std::string returnStatusName(int status) {
    switch (status) {
        case 0: return "";
        case 1: return "[退货待审核]";
        case 2: return "[退货已通过]";
        case 3: return "[退货已拒绝]";
        default: return "";
    }
}

// ---------- 极简风格 ----------
class SimpleRenderer : public UIRenderer {
public:
    explicit SimpleRenderer(const UITheme& theme = themeBlue()) : theme_(theme) {}
    void drawWelcome() override {
        std::cout << theme_.titleOf("\n===== 微商系统 v1.0 =====") << "\n";
    }
    void drawLoginMenu() override {
        std::cout << "\n1. 登录\n2. 注册\n0. 退出\n请选择: ";
    }
    void drawMainMenu() override {
        std::cout << "\n1. 查看商品\n2. 购物车\n3. 结算\n4. 我的订单\n"
                     "5. 申请退货\n6. 切换UI皮肤\n7. 销售统计\n"
                     "8. 搜索商品\n9. 联系客服\n"
                     "10. 退货进度\n11. 退货审核(管理员)\n0. 退出\n请选择: ";
    }
    void drawMessage(const std::string& msg, bool isError) override {
        std::cout << (isError ? theme_.errorOf("[错误] " + msg)
                              : theme_.infoOf(msg)) << "\n";
    }
    void drawDivider() override { std::cout << theme_.borderOf("--------------------------------") << "\n"; }
    void drawProductList(const std::vector<ProductInfo>& products) override {
        std::cout << theme_.accentOf("ID\t名称\t价格\t库存\t分类") << "\n";
        for (const auto& p : products) {
            std::cout << p.id << "\t" << p.name << "\t" << std::fixed
                      << std::setprecision(2) << p.price << "\t" << p.stock
                      << "\t" << p.category << "\n";
        }
    }
    void drawProductDetail(const ProductInfo& p) override {
        std::cout << "商品ID: " << p.id << " | 名称: " << p.name
                  << " | 价格: " << std::fixed << std::setprecision(2) << p.price
                  << " | 库存: " << p.stock << " | 分类: " << p.category << "\n";
    }
    void drawCart(const std::map<int, CartItem>& items, double total) override {
        if (items.empty()) { std::cout << theme_.infoOf("购物车为空") << "\n"; return; }
        std::cout << theme_.accentOf("商品\t数量\t小计") << "\n";
        for (const auto& kv : items) {
            const auto& it = kv.second;
            std::cout << it.productName << "\t" << it.quantity << "\t"
                      << std::fixed << std::setprecision(2) << it.getSubtotal() << "\n";
        }
        std::cout << theme_.accentOf("合计: ") << std::fixed
                  << std::setprecision(2) << total << "\n";
    }
    void drawPromoList(const std::vector<PromoInfo>& promos) override {
        std::cout << theme_.accentOf("可选促销:") << "\n";
        for (const auto& p : promos) {
            std::cout << p.id << ". " << p.name << "\n";
        }
    }
    void drawOrderResult(const std::string& orderNo, double original,
                         double discount, double final) override {
        std::ostringstream oss;
        oss << "订单号: " << orderNo << " | 原价: " << std::fixed
            << std::setprecision(2) << original << " | 优惠: " << discount
            << " | 实付: " << final;
        std::cout << theme_.accentOf(oss.str()) << "\n";
    }
    void drawOrderList(const std::vector<OrderInfo>& orders) override {
        if (orders.empty()) { std::cout << theme_.infoOf("暂无订单") << "\n"; return; }
        for (const auto& o : orders) {
            std::cout << "订单#" << o.orderId << " " << o.orderNo
                      << " 状态:" << orderStatusName(o.status)
                      << returnStatusName(o.returnStatus)
                      << " 实付:" << std::fixed
                      << std::setprecision(2) << o.final << " " << o.createTime << "\n";
            for (const auto& it : o.items) {
                std::cout << "  - " << it.productName << " x" << it.quantity
                          << " @" << std::fixed << std::setprecision(2) << it.price << "\n";
            }
        }
    }
    std::string getStyleName() const override { return "极简风格"; }
    std::string getThemeName() const override { return theme_.name; }
private:
    const UITheme& theme_;
};

// ---------- 详细风格 ----------
class FancyRenderer : public UIRenderer {
public:
    explicit FancyRenderer(const UITheme& theme = themeGreen()) : theme_(theme) {}
    void drawWelcome() override {
        std::cout << theme_.borderOf(
            "+------------------------------------------+\n"
            "|        微商系统 v1.0                     |\n"
            "|   社交零售 · 智慧促销                    |\n"
            "+------------------------------------------+") << "\n";
    }
    void drawLoginMenu() override {
        std::cout << theme_.borderOf(
            "+------------------------------------------+\n"
            "|  1. 登录      2. 注册      0. 退出       |\n"
            "+------------------------------------------+") << "\n请选择: ";
    }
    void drawMainMenu() override {
        std::cout << theme_.borderOf(
            "+------------------------------------------+\n"
            "|  1. 查看商品    2. 购物车   3. 结算      |\n"
            "|  4. 我的订单    5. 申请退货              |\n"
            "|  6. 切换UI皮肤  7. 销售统计              |\n"
            "|  8. 搜索商品    9. 联系客服              |\n"
            "|  10.退货进度    11.退货审核  0. 退出     |\n"
            "+------------------------------------------+") << "\n请选择: ";
    }
    void drawMessage(const std::string& msg, bool isError) override {
        std::cout << (isError ? theme_.errorOf("[错误] " + msg)
                              : theme_.infoOf("[提示] " + msg)) << "\n";
    }
    void drawDivider() override {
        std::cout << theme_.borderOf("+------------------------------------------+") << "\n";
    }
    void drawProductList(const std::vector<ProductInfo>& products) override {
        std::cout << theme_.accentOf(
            "+------+--------------------+---------+------+----------+\n"
            "|  ID  | 名称               | 价格    | 库存 | 分类     |\n"
            "+------+--------------------+---------+------+----------+") << "\n";
        for (const auto& p : products) {
            std::cout << "| " << std::right << std::setw(4) << p.id << " | "
                      << std::left << std::setw(18) << p.name << " | "
                      << std::right << std::fixed << std::setprecision(2)
                      << std::setw(7) << p.price << " | "
                      << std::setw(4) << p.stock << " | "
                      << std::left << std::setw(8) << p.category << " |\n";
        }
        std::cout << theme_.borderOf("+------+--------------------+---------+------+----------+") << "\n";
        std::cout << std::right;
    }
    void drawProductDetail(const ProductInfo& p) override {
        std::cout << theme_.accentOf(
            "+------+--------------------+---------+------+----------+\n"
            "|  ID  | 名称               | 价格    | 库存 | 分类     |\n"
            "+------+--------------------+---------+------+----------+") << "\n"
                  << "| " << std::right << std::setw(4) << p.id << " | "
                  << std::left << std::setw(18) << p.name << " | "
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(7) << p.price << " | "
                  << std::setw(4) << p.stock << " | "
                  << std::left << std::setw(8) << p.category << " |\n"
                  << theme_.borderOf("+------+--------------------+---------+------+----------+") << "\n";
        std::cout << std::right;
    }
    void drawCart(const std::map<int, CartItem>& items, double total) override {
        if (items.empty()) { std::cout << theme_.infoOf("[购物车] 为空") << "\n"; return; }
        std::cout << theme_.accentOf(
            "+--------------------+--------+----------+\n"
            "| 商品               | 数量   | 小计     |\n"
            "+--------------------+--------+----------+") << "\n";
        for (const auto& kv : items) {
            const auto& it = kv.second;
            std::cout << "| " << std::left << std::setw(18) << it.productName << " | "
                      << std::right << std::setw(6) << it.quantity << " | "
                      << std::fixed << std::setprecision(2) << std::setw(8)
                      << it.getSubtotal() << " |\n";
        }
        std::cout << theme_.borderOf("+--------------------+--------+----------+") << "\n"
                  << "| 合计                               | "
                  << std::fixed << std::setprecision(2) << std::setw(8) << total
                  << " |\n" << theme_.borderOf("+--------------------+--------+----------+") << "\n";
        std::cout << std::right;
    }
    void drawPromoList(const std::vector<PromoInfo>& promos) override {
        std::cout << theme_.accentOf(
            "+------+--------------------+------------+\n"
            "|  ID  | 促销名称           | 说明       |\n"
            "+------+--------------------+------------+") << "\n";
        for (const auto& p : promos) {
            std::ostringstream desc;
            if (p.type == 1) desc << "打" << static_cast<int>(p.param1 * 10) << "折";
            else if (p.type == 2) desc << "直降" << static_cast<int>(p.param1) << "元";
            else if (p.type == 3) desc << "满" << static_cast<int>(p.param1)
                                      << "减" << static_cast<int>(p.param2);
            else if (p.type == 4) desc << "阶梯折扣";
            std::cout << "| " << std::setw(4) << p.id << " | "
                      << std::left << std::setw(18) << p.name << " | "
                      << std::right << std::setw(10) << desc.str() << " |\n";
        }
        std::cout << theme_.borderOf("+------+--------------------+------------+") << "\n";
        std::cout << std::right;
    }
    void drawOrderResult(const std::string& orderNo, double original,
                         double discount, double final) override {
        std::cout << theme_.borderOf("+------------------------------------------+") << "\n"
                  << "| 订单号: " << std::left << std::setw(26) << orderNo << "|\n"
                  << "| 原价: " << std::fixed << std::setprecision(2)
                  << std::setw(8) << original << std::setw(17) << " |\n"
                  << "| 优惠: " << std::setw(8) << discount << std::setw(17) << " |\n"
                  << "| 实付: " << std::setw(8) << final << std::setw(17) << " |\n"
                  << theme_.borderOf("+------------------------------------------+") << "\n";
        std::cout << std::right;
    }
    void drawOrderList(const std::vector<OrderInfo>& orders) override {
        if (orders.empty()) { std::cout << theme_.infoOf("[订单] 暂无订单") << "\n"; return; }
        for (const auto& o : orders) {
            std::cout << theme_.borderOf("+------+------------------------------+------------------+") << "\n"
                      << "| 订单#" << std::left << std::setw(6) << o.orderId
                      << " | " << std::setw(28) << o.orderNo
                      << " | " << std::setw(14) << orderStatusName(o.status) << " |\n"
                      << "| 实付: " << std::fixed << std::setprecision(2) << o.final
                      << " | 优惠: " << o.discount
                      << " | " << std::setw(24) << o.createTime << " |\n";
            if (o.returnStatus > 0) {
                std::cout << "| " << std::left << std::setw(58)
                          << returnStatusName(o.returnStatus) << " |\n";
            }
            std::cout << theme_.borderOf("+------+------------------------------+------------------+") << "\n";
            for (const auto& it : o.items) {
                std::cout << "  - " << it.productName << " x" << it.quantity
                          << " @" << std::fixed << std::setprecision(2) << it.price << "\n";
            }
        }
        std::cout << std::right;
    }
    std::string getStyleName() const override { return "详细风格"; }
    std::string getThemeName() const override { return theme_.name; }
private:
    const UITheme& theme_;
};
