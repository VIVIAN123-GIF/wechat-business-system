#include "ClientController.h"
#include "Renderers.h"
#include "../common/ProtocolHeader.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <limits>

using json = nlohmann::json;

ClientController::ClientController()
    : tcpClient_(std::make_unique<TcpClient>("127.0.0.1", 8888)),
      renderer_(std::make_unique<FancyRenderer>()) {}

ClientController::~ClientController() = default;

// ---------------- 输入工具（容错） ----------------
int ClientController::getUserInputInt(const std::string& prompt, int min, int max) {
    int value = -1;
    while (true) {
        std::cout << prompt;
        std::cin >> value;
        if (std::cin.fail()) {
            std::cin.clear();
            std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
            renderer_->drawMessage("请输入数字", true);
            continue;
        }
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        if (value < min || value > max) {
            renderer_->drawMessage("输入超出范围 (" + std::to_string(min) +
                                   "-" + std::to_string(max) + ")", true);
            continue;
        }
        return value;
    }
}

bool ClientController::sendAndRecv(int code, const std::string& body,
                                   int& respCode, std::string& respBody) {
    if (!tcpClient_->sendRequest(code, body)) {
        renderer_->drawMessage("发送失败，请检查服务端是否运行", true);
        return false;
    }
    if (!tcpClient_->receiveResponse(respCode, respBody)) {
        renderer_->drawMessage("接收响应超时或连接断开", true);
        return false;
    }
    // 错误响应统一解析提示
    if (respCode == ProtocolConstants::S_ERR_GENERAL ||
        respCode == ProtocolConstants::S_ERR_UNAUTH) {
        try {
            auto j = json::parse(respBody);
            std::string msg = j.value("err_msg", "未知错误");
            renderer_->drawMessage(msg, true);
        } catch (...) {
            renderer_->drawMessage("服务端返回错误", true);
        }
        return false;
    }
    return true;
}

// ---------------- 流程 ----------------
void ClientController::selectUIStyle() {
    renderer_->drawWelcome();
    std::cout << "\n选择 UI 风格：\n1. 极简风格\n2. 详细风格\n";
    int choice = getUserInputInt("请选择 (1-2): ", 1, 2);
    if (choice == 1) {
        renderer_ = std::make_unique<SimpleRenderer>();  // 策略模式动态切换
    } else {
        renderer_ = std::make_unique<FancyRenderer>();
    }
    renderer_->drawMessage("已切换到" + renderer_->getStyleName());
}

bool ClientController::connectServer() {
    renderer_->drawMessage("正在连接服务端 127.0.0.1:8888 ...");
    if (!tcpClient_->connect()) {
        renderer_->drawMessage("连接失败：请确认服务端已启动", true);
        return false;
    }
    renderer_->drawMessage("连接成功");
    // 心跳验证
    int code = 0;
    std::string body;
    if (sendAndRecv(ProtocolConstants::C_REQ_HEARTBEAT, "{}", code, body)) {
        renderer_->drawMessage("心跳正常，服务端在线");
    }
    return true;
}

bool ClientController::loginLoop() {
    while (true) {
        renderer_->drawLoginMenu();
        int choice = getUserInputInt("", 0, 2);
        if (choice == 0) return false;

        if (choice == 1) {  // 登录
            std::string username, password;
            std::cout << "用户名: ";
            std::getline(std::cin, username);
            std::cout << "密码: ";
            std::getline(std::cin, password);

            json req;
            req["username"] = username;
            req["password"] = password;
            int respCode = 0;
            std::string respBody;
            if (!sendAndRecv(ProtocolConstants::C_REQ_LOGIN, req.dump(), respCode, respBody)) {
                continue;
            }
            try {
                auto j = json::parse(respBody);
                userId_ = j["user_id"];
                username_ = username;
                level_ = j.value("level", 1);
                isLoggedIn_ = true;
                renderer_->drawMessage("欢迎，" + username_ +
                                       (level_ >= 2 ? "（VIP 会员，享额外95折）" : ""));
                return true;
            } catch (...) {
                renderer_->drawMessage("登录响应解析失败", true);
            }
        } else {  // 注册
            std::string username, password;
            std::cout << "新用户名: ";
            std::getline(std::cin, username);
            std::cout << "密码(至少4位): ";
            std::getline(std::cin, password);

            json req;
            req["username"] = username;
            req["password"] = password;
            int respCode = 0;
            std::string respBody;
            if (sendAndRecv(ProtocolConstants::C_REQ_REGISTER, req.dump(), respCode, respBody)) {
                renderer_->drawMessage("注册成功，请登录");
            }
        }
    }
}

void ClientController::mainLoop() {
    while (true) {
        renderer_->drawDivider();
        renderer_->drawMainMenu();
        int choice = getUserInputInt("", 0, 6);
        switch (choice) {
            case 1: handleShowProducts(); break;
            case 2: handleCartManagement(); break;
            case 3: handleCheckout(); break;
            case 4: handleOrders(); break;
            case 5: handleReturn(); break;
            case 6: handleSwitchStyle(); break;
            case 0:
                renderer_->drawMessage("感谢使用，再见！");
                return;
            default: break;
        }
    }
}

void ClientController::run() {
    selectUIStyle();
    if (!connectServer()) return;
    if (!loginLoop()) return;
    mainLoop();
}

// ---------------- 功能 ----------------
void ClientController::handleShowProducts() {
    int respCode = 0;
    std::string respBody;
    if (!sendAndRecv(ProtocolConstants::C_REQ_PRODUCT_LIST, "{}", respCode, respBody)) return;

    std::vector<ProductInfo> products;
    try {
        auto j = json::parse(respBody);
        for (auto& item : j["products"]) {
            ProductInfo p;
            p.id    = item["id"];
            p.name  = item["name"];
            p.price = item["price"];
            p.stock = item["stock"];
            p.category = item.value("category", "");
            products.push_back(p);
        }
    } catch (...) {
        renderer_->drawMessage("商品数据解析失败", true);
        return;
    }
    renderer_->drawProductList(products);

    // 支持加入购物车
    std::cout << "\n加入购物车？输入商品ID（0 返回）: ";
    int pid = getUserInputInt("", 0, 100000);
    if (pid == 0) return;
    int qty = getUserInputInt("数量: ", 1, 9999);

    // 校验库存（服务端）
    json req;
    req["user_id"] = userId_;
    req["product_id"] = pid;
    req["quantity"] = qty;
    if (sendAndRecv(ProtocolConstants::C_REQ_ADD_CART, req.dump(), respCode, respBody)) {
        for (const auto& p : products) {
            if (p.id == pid) {
                cart_.addItem(pid, p.name, p.price, qty);
                renderer_->drawMessage("已加入购物车: " + p.name + " x" + std::to_string(qty));
                return;
            }
        }
        renderer_->drawMessage("商品ID不存在", true);
    }
}

void ClientController::handleCartManagement() {
    while (true) {
        renderer_->drawCart(cart_.getItems(), cart_.getTotalPrice());
        std::cout << "\n购物车管理：\n1. 删除商品\n2. 修改数量\n3. 清空\n0. 返回\n";
        int choice = getUserInputInt("请选择: ", 0, 3);
        if (choice == 0) return;
        if (choice == 1) {
            int pid = getUserInputInt("输入商品ID: ", 1, 100000);
            cart_.removeItem(pid);
            renderer_->drawMessage("已删除");
        } else if (choice == 2) {
            int pid = getUserInputInt("输入商品ID: ", 1, 100000);
            int qty = getUserInputInt("新数量(0删除): ", 0, 9999);
            cart_.updateQuantity(pid, qty);
            renderer_->drawMessage("已更新");
        } else if (choice == 3) {
            cart_.clear();
            renderer_->drawMessage("购物车已清空");
        }
    }
}

void ClientController::handleCheckout() {
    if (cart_.isEmpty()) {
        renderer_->drawMessage("购物车为空，请先添加商品", true);
        return;
    }
    renderer_->drawCart(cart_.getItems(), cart_.getTotalPrice());

    // 拉取可用促销
    int respCode = 0;
    std::string respBody;
    std::vector<PromoInfo> promos;
    if (sendAndRecv(ProtocolConstants::C_REQ_PROMO_LIST, "{}", respCode, respBody)) {
        try {
            auto j = json::parse(respBody);
            for (auto& item : j["promotions"]) {
                PromoInfo p;
                p.id    = item["id"];
                p.name  = item["name"];
                p.type  = item["type"];
                p.param1 = item.value("param1", 0.0);
                p.param2 = item.value("param2", 0.0);
                promos.push_back(p);
            }
        } catch (...) {
            renderer_->drawMessage("促销数据解析失败", true);
            return;
        }
    } else {
        return;
    }

    renderer_->drawPromoList(promos);
    std::cout << "选择要叠加的促销ID（多个用空格分隔，直接回车不使用促销）: ";
    std::string line;
    std::getline(std::cin, line);
    std::vector<int> promoIds;
    {
        std::istringstream iss(line);
        int id;
        while (iss >> id) promoIds.push_back(id);
    }

    json req;
    req["user_id"] = userId_;
    req["items"] = json::parse(cart_.itemsToJson());
    req["promo_ids"] = promoIds;

    renderer_->drawMessage("正在提交订单，请稍候...");
    if (!sendAndRecv(ProtocolConstants::C_REQ_CHECKOUT, req.dump(), respCode, respBody)) {
        return;
    }
    try {
        auto j = json::parse(respBody);
        renderer_->drawOrderResult(j["order_no"], j["original"], j["discount"], j["final"]);
        if (j.contains("promo_desc")) {
            renderer_->drawMessage("促销明细: " + j["promo_desc"].get<std::string>());
        }
        cart_.clear();
        renderer_->drawMessage("订单生成成功，购物车已清空");
    } catch (...) {
        renderer_->drawMessage("结算响应解析失败", true);
    }
}

void ClientController::handleOrders() {
    json req;
    req["user_id"] = userId_;
    int respCode = 0;
    std::string respBody;
    if (!sendAndRecv(ProtocolConstants::C_REQ_ORDER_QUERY, req.dump(), respCode, respBody)) return;

    std::vector<OrderInfo> orders;
    try {
        auto j = json::parse(respBody);
        for (auto& o : j["orders"]) {
            OrderInfo info;
            info.orderId   = o["order_id"];
            info.orderNo   = o["order_no"];
            info.original  = o["original"];
            info.discount  = o["discount"];
            info.final     = o["final"];
            info.status    = o["status"];
            info.createTime = o.value("create_time", "");
            for (auto& it : o["items"]) {
                CartItem ci;
                ci.productId   = it["product_id"];
                ci.productName = it["product_name"];
                ci.price       = it["price"];
                ci.quantity    = it["quantity"];
                info.items.push_back(ci);
            }
            orders.push_back(info);
        }
    } catch (...) {
        renderer_->drawMessage("订单数据解析失败", true);
        return;
    }
    renderer_->drawOrderList(orders);
}

void ClientController::handleReturn() {
    renderer_->drawMessage("请先查询订单确认订单ID");
    int orderId = getUserInputInt("输入要退货的订单ID (0返回): ", 0, 1000000);
    if (orderId == 0) return;
    std::cout << "退货原因: ";
    std::string reason;
    std::getline(std::cin, reason);
    if (reason.empty()) reason = "不想要了";

    json req;
    req["user_id"] = userId_;
    req["order_id"] = orderId;
    req["reason"] = reason;
    int respCode = 0;
    std::string respBody;
    if (sendAndRecv(ProtocolConstants::C_REQ_RETURN, req.dump(), respCode, respBody)) {
        renderer_->drawMessage("退货申请已提交，等待审核");
    }
}

void ClientController::handleSwitchStyle() {
    if (renderer_->getStyleName() == "极简风格") {
        renderer_ = std::make_unique<FancyRenderer>();
    } else {
        renderer_ = std::make_unique<SimpleRenderer>();
    }
    renderer_->drawMessage("已切换至" + renderer_->getStyleName());
}
