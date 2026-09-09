// ============================================================
// Renderers.h - 两种 UI 风格（策略模式具体策略）
// SimpleRenderer: 极简（纯文字） / FancyRenderer: 详细（ASCII 边框）
// ============================================================
#pragma once
#include <iostream>
#include <iomanip>
#include <sstream>
#include "UIRenderer.h"

// ---------- 极简风格 ----------
class SimpleRenderer : public UIRenderer {
public:
    void drawWelcome() override {
        std::cout << "\n===== 微商系统 v1.0 =====\n";
    }
    void drawLoginMenu() override {
        std::cout << "\n1. 登录\n2. 注册\n0. 退出\n请选择: ";
    }
    void drawMainMenu() override {
        std::cout << "\n1. 查看商品\n2. 购物车\n3. 结算\n4. 我的订单\n"
                     "5. 申请退货\n6. 切换UI风格\n0. 退出\n请选择: ";
    }
    void drawMessage(const std::string& msg, bool isError) override {
        std::cout << (isError ? "[错误] " : "") << msg << "\n";
    }
    void drawDivider() override { std::cout << "--------------------------------\n"; }
    void drawProductList(const std::vector<ProductInfo>& products) override {
        std::cout << "ID\t名称\t价格\t库存\t分类\n";
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
        if (items.empty()) { std::cout << "购物车为空\n"; return; }
        std::cout << "商品\t数量\t小计\n";
        for (const auto& kv : items) {
            const auto& it = kv.second;
            std::cout << it.productName << "\t" << it.quantity << "\t"
                      << std::fixed << std::setprecision(2) << it.getSubtotal() << "\n";
        }
        std::cout << "合计: " << std::fixed << std::setprecision(2) << total << "\n";
    }
    void drawPromoList(const std::vector<PromoInfo>& promos) override {
        std::cout << "可选促销:\n";
        for (const auto& p : promos) {
            std::cout << p.id << ". " << p.name << "\n";
        }
    }
    void drawOrderResult(const std::string& orderNo, double original,
                         double discount, double final) override {
        std::cout << "订单号: " << orderNo << " | 原价: " << std::fixed
                  << std::setprecision(2) << original << " | 优惠: " << discount
                  << " | 实付: " << final << "\n";
    }
    void drawOrderList(const std::vector<OrderInfo>& orders) override {
        if (orders.empty()) { std::cout << "暂无订单\n"; return; }
        for (const auto& o : orders) {
            std::cout << "订单#" << o.orderId << " " << o.orderNo
                      << " 状态:" << o.status << " 实付:" << std::fixed
                      << std::setprecision(2) << o.final << " " << o.createTime << "\n";
            for (const auto& it : o.items) {
                std::cout << "  - " << it.productName << " x" << it.quantity
                          << " @" << std::fixed << std::setprecision(2) << it.price << "\n";
            }
        }
    }
    std::string getStyleName() const override { return "极简风格"; }
};

// ---------- 详细风格 ----------
class FancyRenderer : public UIRenderer {
public:
    void drawWelcome() override {
        std::cout << "\n+------------------------------------------+\n"
                     "|        微商系统 v1.0                     |\n"
                     "|   社交零售 · 智慧促销                    |\n"
                     "+------------------------------------------+\n";
    }
    void drawLoginMenu() override {
        std::cout << "+------------------------------------------+\n"
                     "|  1. 登录      2. 注册      0. 退出       |\n"
                     "+------------------------------------------+\n请选择: ";
    }
    void drawMainMenu() override {
        std::cout << "+------------------------------------------+\n"
                     "|  1. 查看商品    2. 购物车   3. 结算      |\n"
                     "|  4. 我的订单    5. 申请退货              |\n"
                     "|  6. 切换UI风格  0. 退出                  |\n"
                     "+------------------------------------------+\n请选择: ";
    }
    void drawMessage(const std::string& msg, bool isError) override {
        std::cout << (isError ? "[错误] " : "[提示] ") << msg << "\n";
    }
    void drawDivider() override { std::cout << "+------------------------------------------+\n"; }
    void drawProductList(const std::vector<ProductInfo>& products) override {
        std::cout << "+------+--------------------+---------+------+----------+\n"
                     "|  ID  | 名称               | 价格    | 库存 | 分类     |\n"
                     "+------+--------------------+---------+------+----------+\n";
        for (const auto& p : products) {
            std::cout << "| " << std::right << std::setw(4) << p.id << " | "
                      << std::left << std::setw(18) << p.name << " | "
                      << std::right << std::fixed << std::setprecision(2)
                      << std::setw(7) << p.price << " | "
                      << std::setw(4) << p.stock << " | "
                      << std::left << std::setw(8) << p.category << " |\n";
        }
        std::cout << "+------+--------------------+---------+------+----------+\n";
        std::cout << std::right;
    }
    void drawProductDetail(const ProductInfo& p) override {
        std::cout << "+------+--------------------+---------+------+----------+\n"
                  << "|  ID  | 名称               | 价格    | 库存 | 分类     |\n"
                  << "+------+--------------------+---------+------+----------+\n"
                  << "| " << std::right << std::setw(4) << p.id << " | "
                  << std::left << std::setw(18) << p.name << " | "
                  << std::right << std::fixed << std::setprecision(2)
                  << std::setw(7) << p.price << " | "
                  << std::setw(4) << p.stock << " | "
                  << std::left << std::setw(8) << p.category << " |\n"
                  << "+------+--------------------+---------+------+----------+\n";
        std::cout << std::right;
    }
    void drawCart(const std::map<int, CartItem>& items, double total) override {
        if (items.empty()) { std::cout << "[购物车] 为空\n"; return; }
        std::cout << "+--------------------+--------+----------+\n"
                     "| 商品               | 数量   | 小计     |\n"
                     "+--------------------+--------+----------+\n";
        for (const auto& kv : items) {
            const auto& it = kv.second;
            std::cout << "| " << std::left << std::setw(18) << it.productName << " | "
                      << std::right << std::setw(6) << it.quantity << " | "
                      << std::fixed << std::setprecision(2) << std::setw(8)
                      << it.getSubtotal() << " |\n";
        }
        std::cout << "+--------------------+--------+----------+\n"
                  << "| 合计                               | "
                  << std::fixed << std::setprecision(2) << std::setw(8) << total
                  << " |\n+--------------------+--------+----------+\n";
        std::cout << std::right;
    }
    void drawPromoList(const std::vector<PromoInfo>& promos) override {
        std::cout << "+------+--------------------+------------+\n"
                     "|  ID  | 促销名称           | 说明       |\n"
                     "+------+--------------------+------------+\n";
        for (const auto& p : promos) {
            std::ostringstream desc;
            if (p.type == 1) desc << "打" << static_cast<int>(p.param1 * 100) << "折";
            else if (p.type == 2) desc << "直降" << static_cast<int>(p.param1) << "元";
            else if (p.type == 3) desc << "满" << static_cast<int>(p.param1)
                                      << "减" << static_cast<int>(p.param2);
            std::cout << "| " << std::setw(4) << p.id << " | "
                      << std::left << std::setw(18) << p.name << " | "
                      << std::right << std::setw(10) << desc.str() << " |\n";
        }
        std::cout << "+------+--------------------+------------+\n";
        std::cout << std::right;
    }
    void drawOrderResult(const std::string& orderNo, double original,
                         double discount, double final) override {
        std::cout << "+------------------------------------------+\n"
                  << "| 订单号: " << std::left << std::setw(26) << orderNo << "|\n"
                  << "| 原价: " << std::fixed << std::setprecision(2)
                  << std::setw(8) << original << std::setw(17) << " |\n"
                  << "| 优惠: " << std::setw(8) << discount << std::setw(17) << " |\n"
                  << "| 实付: " << std::setw(8) << final << std::setw(17) << " |\n"
                  << "+------------------------------------------+\n";
        std::cout << std::right;
    }
    void drawOrderList(const std::vector<OrderInfo>& orders) override {
        if (orders.empty()) { std::cout << "[订单] 暂无订单\n"; return; }
        for (const auto& o : orders) {
            std::cout << "+------+------------------------------+----------+\n"
                      << "| 订单#" << std::left << std::setw(6) << o.orderId
                      << " | " << std::setw(28) << o.orderNo
                      << " | 状态:" << o.status << " |\n"
                      << "| 实付: " << std::fixed << std::setprecision(2) << o.final
                      << " | 优惠: " << o.discount
                      << " | " << std::setw(24) << o.createTime << " |\n"
                      << "+------+------------------------------+----------+\n";
            for (const auto& it : o.items) {
                std::cout << "  - " << it.productName << " x" << it.quantity
                          << " @" << std::fixed << std::setprecision(2) << it.price << "\n";
            }
        }
        std::cout << std::right;
    }
    std::string getStyleName() const override { return "详细风格"; }
};
