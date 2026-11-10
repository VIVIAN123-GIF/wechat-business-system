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

// ---------- 中文显示宽度与对齐辅助 ----------
// UTF-8 中文字符显示占 2 列，std::setw 按字节计数会导致中文对不齐
inline int displayWidth(const std::string& s) {
    int w = 0;
    for (size_t i = 0; i < s.size(); ) {
        unsigned char c = static_cast<unsigned char>(s[i]);
        if (c < 0x80)      { w += 1; i += 1; }
        else if (c < 0xC0) { w += 1; i += 1; }
        else if (c < 0xE0) { w += 1; i += 2; }
        else if (c < 0xF0) { w += 2; i += 3; }
        else                { w += 2; i += 4; }
    }
    return w;
}
inline std::string padR(const std::string& s, int width) {
    int dw = displayWidth(s);
    return (dw >= width) ? s : s + std::string(width - dw, ' ');
}
inline std::string padL(const std::string& s, int width) {
    int dw = displayWidth(s);
    return (dw >= width) ? s : std::string(width - dw, ' ') + s;
}
inline std::string fmtPrice(double v) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2) << v;
    return oss.str();
}

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
    void drawMainMenu(int level = 1) override {
        std::cout << "\n── 购物功能 ──────────────────────\n"
                     "1. 查看商品   2. 购物车    3. 结算\n"
                     "4. 我的订单   5. 申请退货  10.退货进度\n"
                     "── 其他功能 ──────────────────────\n"
                     "6. 切换皮肤   7. 销售统计  8. 搜索商品\n"
                     "9. 联系客服\n";
        if (level >= 2) {
            std::cout << "── 管理员 ────────────────────────\n"
                         "11.退货审核\n";
        }
        std::cout << "0. 退出系统\n请选择: ";
    }
    void drawMessage(const std::string& msg, bool isError) override {
        std::cout << (isError ? theme_.errorOf("[错误] " + msg)
                              : theme_.infoOf(msg)) << "\n";
    }
    void drawDivider() override { std::cout << theme_.borderOf("--------------------------------") << "\n"; }
    void drawProductList(const std::vector<ProductInfo>& products) override {
        const char* sep = "+------+--------------------+-----------+------+----------+";
        std::cout << theme_.borderOf(sep) << "\n"
                  << theme_.accentOf(
                      "|  ID  | 名称               | 价格      | 库存 | 分类     |") << "\n"
                  << theme_.borderOf(sep) << "\n";
        for (const auto& p : products) {
            std::cout << "| " << padL(std::to_string(p.id), 4) << " | "
                      << padR(p.name, 18) << " | "
                      << padL(fmtPrice(p.price), 9) << " | "
                      << padL(std::to_string(p.stock), 4) << " | "
                      << padR(p.category, 8) << " |\n";
        }
        std::cout << theme_.borderOf(sep) << "\n";
    }
    void drawProductDetail(const ProductInfo& p) override {
        std::cout << "商品ID: " << p.id << " | 名称: " << p.name
                  << " | 价格: " << std::fixed << std::setprecision(2) << p.price
                  << " | 库存: " << p.stock << " | 分类: " << p.category << "\n";
    }
    void drawCart(const std::map<std::pair<int, int>, CartItem>& items, double total) override {
        if (items.empty()) { std::cout << theme_.infoOf("购物车为空") << "\n"; return; }
        const char* sep = "+------+--------------------+------+----------+";
        std::cout << theme_.borderOf(sep) << "\n"
                  << theme_.accentOf(
                      "|  ID  | 商品               | 数量 | 小计     |") << "\n"
                  << theme_.borderOf(sep) << "\n";
        for (const auto& kv : items) {
            const auto& it = kv.second;
            std::cout << "| " << padL(std::to_string(it.productId), 4) << " | "
                      << padR(it.productName, 18) << " | "
                      << padL(std::to_string(it.quantity), 4) << " | "
                      << padL(fmtPrice(it.getSubtotal()), 8) << " |\n";
        }
        std::cout << theme_.borderOf(sep) << "\n"
                  << "| 合计" << std::string(28, ' ') << " | "
                  << padL(fmtPrice(total), 8) << " |\n"
                  << theme_.borderOf("+------+--------------------+------+----------+") << "\n";
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
    void drawMainMenu(int level = 1) override {
        std::string menu =
            "+------------------------------------------+\n"
            "| 【购物】                                 |\n"
            "|  1.查看商品   2.购物车    3.结算         |\n"
            "|  4.我的订单   5.申请退货  10.退货进度    |\n"
            "+------------------------------------------+\n"
            "| 【其他】                                 |\n"
            "|  6.切换皮肤   7.销售统计  8.搜索商品     |\n"
            "|  9.联系客服                              |\n";
        if (level >= 2) {
            menu +=
                "+------------------------------------------+\n"
                "| 【管理员】                               |\n"
                "|  11.退货审核                             |\n";
        }
        menu +=
            "+------------------------------------------+\n"
            "|  0.退出系统                              |\n"
            "+------------------------------------------+";
        std::cout << theme_.borderOf(menu) << "\n请选择: ";
    }
    void drawMessage(const std::string& msg, bool isError) override {
        std::cout << (isError ? theme_.errorOf("[错误] " + msg)
                              : theme_.infoOf("[提示] " + msg)) << "\n";
    }
    void drawDivider() override {
        std::cout << theme_.borderOf("+------------------------------------------+") << "\n";
    }
    void drawProductList(const std::vector<ProductInfo>& products) override {
        const char* sep = "+------+--------------------+-----------+------+----------+";
        std::cout << theme_.accentOf(
            "+------+--------------------+-----------+------+----------+\n"
            "|  ID  | 名称               | 价格      | 库存 | 分类     |\n"
            "+------+--------------------+-----------+------+----------+") << "\n";
        for (const auto& p : products) {
            std::cout << "| " << padL(std::to_string(p.id), 4) << " | "
                      << padR(p.name, 18) << " | "
                      << padL(fmtPrice(p.price), 9) << " | "
                      << padL(std::to_string(p.stock), 4) << " | "
                      << padR(p.category, 8) << " |\n";
        }
        std::cout << theme_.borderOf(sep) << "\n";
    }
    void drawProductDetail(const ProductInfo& p) override {
        const char* sep = "+------+--------------------+-----------+------+----------+";
        std::cout << theme_.accentOf(
            "+------+--------------------+-----------+------+----------+\n"
            "|  ID  | 名称               | 价格      | 库存 | 分类     |\n"
            "+------+--------------------+-----------+------+----------+") << "\n"
                  << "| " << padL(std::to_string(p.id), 4) << " | "
                  << padR(p.name, 18) << " | "
                  << padL(fmtPrice(p.price), 9) << " | "
                  << padL(std::to_string(p.stock), 4) << " | "
                  << padR(p.category, 8) << " |\n"
                  << theme_.borderOf(sep) << "\n";
    }
    void drawCart(const std::map<std::pair<int, int>, CartItem>& items, double total) override {
        if (items.empty()) { std::cout << theme_.infoOf("[购物车] 为空") << "\n"; return; }
        const char* sep = "+------+--------------------+------+----------+";
        std::cout << theme_.accentOf(
            "+------+--------------------+------+----------+\n"
            "|  ID  | 商品               | 数量 | 小计     |\n"
            "+------+--------------------+------+----------+") << "\n";
        for (const auto& kv : items) {
            const auto& it = kv.second;
            std::cout << "| " << padL(std::to_string(it.productId), 4) << " | "
                      << padR(it.productName, 18) << " | "
                      << padL(std::to_string(it.quantity), 4) << " | "
                      << padL(fmtPrice(it.getSubtotal()), 8) << " |\n";
        }
        std::cout << theme_.borderOf(sep) << "\n"
                  << "| 合计" << std::string(28, ' ') << " | "
                  << padL(fmtPrice(total), 8) << " |\n"
                  << theme_.borderOf(sep) << "\n";
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
            std::cout << "| " << padL(std::to_string(p.id), 4) << " | "
                      << padR(p.name, 18) << " | "
                      << padL(desc.str(), 10) << " |\n";
        }
        std::cout << theme_.borderOf("+------+--------------------+------------+") << "\n";
    }
    void drawOrderResult(const std::string& orderNo, double original,
                         double discount, double final) override {
        std::cout << theme_.borderOf("+------------------------------------------+") << "\n"
                  << "| 订单号: " << padR(orderNo, 33) << "|\n"
                  << "| 原价: " << padL(fmtPrice(original), 8) << std::string(27, ' ') << "|\n"
                  << "| 优惠: " << padL(fmtPrice(discount), 8) << std::string(27, ' ') << "|\n"
                  << "| 实付: " << padL(fmtPrice(final), 8) << std::string(27, ' ') << "|\n"
                  << theme_.borderOf("+------------------------------------------+") << "\n";
    }
    void drawOrderList(const std::vector<OrderInfo>& orders) override {
        if (orders.empty()) { std::cout << theme_.infoOf("[订单] 暂无订单") << "\n"; return; }
        for (const auto& o : orders) {
            std::cout << theme_.borderOf("+------+------------------------------+------------------+") << "\n"
                      << "| " << padL(std::to_string(o.orderId), 4)
                      << " | " << padR(o.orderNo, 28)
                      << " | " << padR(orderStatusName(o.status), 16) << " |\n"
                      << "| 实付: " << padL(fmtPrice(o.final), 8)
                      << "  优惠: " << padL(fmtPrice(o.discount), 8)
                      << "  " << padR(o.createTime, 22) << " |\n";
            if (o.returnStatus > 0) {
                std::cout << "| " << padR(returnStatusName(o.returnStatus), 54) << " |\n";
            }
            std::cout << theme_.borderOf("+------+------------------------------+------------------+") << "\n";
            for (const auto& it : o.items) {
                std::cout << "  - " << it.productName << " x" << it.quantity
                          << " @" << fmtPrice(it.price) << "\n";
            }
        }
    }
    std::string getStyleName() const override { return "详细风格"; }
    std::string getThemeName() const override { return theme_.name; }
private:
    const UITheme& theme_;
};
