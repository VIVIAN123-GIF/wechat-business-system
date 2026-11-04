// ============================================================
// UIRenderer.h - UI 渲染策略抽象接口（策略模式）
// ============================================================
#pragma once
#include <string>
#include <vector>
#include <map>
#include "model/models.h"

class UIRenderer {
public:
    virtual ~UIRenderer() = default;

    // 通用界面
    virtual void drawWelcome() = 0;
    virtual void drawLoginMenu() = 0;
    virtual void drawMainMenu(int level = 1) = 0;
    virtual void drawMessage(const std::string& msg, bool isError = false) = 0;
    virtual void drawDivider() = 0;

    // 商品
    virtual void drawProductList(const std::vector<ProductInfo>& products) = 0;
    virtual void drawProductDetail(const ProductInfo& p) = 0;

    // 购物车
    virtual void drawCart(const std::map<int, CartItem>& items, double total) = 0;

    // 促销
    virtual void drawPromoList(const std::vector<PromoInfo>& promos) = 0;

    // 结算与订单
    virtual void drawOrderResult(const std::string& orderNo, double original,
                                 double discount, double final) = 0;
    virtual void drawOrderList(const std::vector<OrderInfo>& orders) = 0;

    // 风格名 / 主题色名
    virtual std::string getStyleName() const = 0;
    virtual std::string getThemeName() const = 0;
};
