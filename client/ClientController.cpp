// ============================================================
// ClientController.cpp - 控制台客户端主控制器（MVC 的 C）
// ============================================================
#include "ClientController.h"
#include "Renderers.h"
#include "../common/ProtocolHeader.h"
#include "../common/SHA256.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <iomanip>
#include <limits>

using json = nlohmann::json;

ClientController::ClientController()
    : tcpClient_(std::make_unique<TcpClient>("127.0.0.1", 8888)),
      renderer_(createSkin(2)) {}

ClientController::~ClientController() = default;

// ---------------- 皮肤工厂（抽象工厂） ----------------
std::unique_ptr<UIRenderer> ClientController::createSkin(int skinIndex) {
    switch (skinIndex) {
        case 1: return BlueThemeFactory().createSimple();
        case 3: return PinkThemeFactory().createFancy();
        case 2:
        default: return GreenThemeFactory().createFancy();
    }
}

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
    // 自动注入会话 token（鉴权）
    std::string outBody = body;
    if (!token_.empty()) {
        try {
            auto j = json::parse(body.empty() ? "{}" : body);
            if (!j.contains("token")) j["token"] = token_;
            outBody = j.dump();
        } catch (...) { outBody = body; }
    }
    if (!tcpClient_->sendRequest(code, outBody)) {
        renderer_->drawMessage("发送失败，请检查服务端是否运行", true);
        return false;
    }
    if (!tcpClient_->receiveResponse(respCode, respBody)) {
        renderer_->drawMessage("接收响应超时或连接断开", true);
        return false;
    }
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
    std::cout << "\n选择 UI 风格：\n1. 极简×蓝\n2. 详细×绿\n3. 详细×粉\n";
    int choice = getUserInputInt("请选择 (1-3): ", 1, 3);
    skinIndex_ = choice;
    renderer_ = createSkin(choice);
    renderer_->drawMessage("已切换到" + renderer_->getThemeName() + "-" +
                           renderer_->getStyleName());
}

bool ClientController::connectServer() {
    renderer_->drawMessage("正在连接服务端 127.0.0.1:8888 ...");
    if (!tcpClient_->connect()) {
        renderer_->drawMessage("连接失败：请确认服务端已启动", true);
        return false;
    }
    renderer_->drawMessage("连接成功");
    int code = 0; std::string body;
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

        if (choice == 1) {  // 登录（密码本地哈希后传输）
            std::string username, password;
            std::cout << "用户名: ";
            std::getline(std::cin, username);
            std::cout << "密码: ";
            std::getline(std::cin, password);

            json req;
            req["username"] = username;
            req["password"] = hashPassword(username, password);
            int respCode = 0; std::string respBody;
            if (!sendAndRecv(ProtocolConstants::C_REQ_LOGIN, req.dump(), respCode, respBody)) {
                continue;
            }
            try {
                auto j = json::parse(respBody);
                userId_ = j["user_id"];
                username_ = username;
                level_ = j.value("level", 1);
                token_ = j.value("token", "");
                isLoggedIn_ = true;
                syncCartFromServer();
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
            req["password"] = hashPassword(username, password);
            int respCode = 0; std::string respBody;
            if (sendAndRecv(ProtocolConstants::C_REQ_REGISTER, req.dump(), respCode, respBody)) {
                renderer_->drawMessage("注册成功，请登录");
            }
        }
    }
}

bool ClientController::syncCartFromServer() {
    json req = {{"user_id", userId_}, {"token", token_}};
    int code = 0; std::string body;
    if (!sendAndRecv(ProtocolConstants::C_REQ_CART_LIST, req.dump(), code, body) || code != ProtocolConstants::S_RES_CART_LIST) return false;
    try {
        auto result = json::parse(body); LocalCart fresh;
        for (const auto& item : result.value("items", json::array())) {
            fresh.addItem(item.value("product_id", 0), item.value("name", ""),
                          item.value("price", 0) / 100.0, item.value("quantity", 0),
                          item.value("sku_id", 0), item.value("sku_label", ""));
        }
        cart_ = std::move(fresh); return true;
    } catch (...) { return false; }
}

void ClientController::mainLoop() {
    while (true) {
        renderer_->drawDivider();
        renderer_->drawMainMenu(level_);
        int choice = getUserInputInt("", 0, 11);
        switch (choice) {
            case 1: handleShowProducts(); break;
            case 2: handleCartManagement(); break;
            case 3: handleCheckout(); break;
            case 4: handleOrders(); break;
            case 5: handleReturn(); break;
            case 6: handleSwitchStyle(); break;
            case 7: handleStats(); break;
            case 8: handleSearch(); break;
            case 9: handleServiceMsg(); break;
            case 10: handleMyReturns(); break;
            case 11: handleAuditReturns(); break;
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
    while (true) {
        // 1. 拉取并显示商品列表
        int respCode = 0; std::string respBody;
        if (!sendAndRecv(ProtocolConstants::C_REQ_PRODUCT_LIST, "{}", respCode, respBody)) return;

        std::vector<ProductInfo> products;
        try {
            auto j = json::parse(respBody);
            for (auto& item : j["products"]) {
                ProductInfo p;
                p.id    = item["id"];
                p.name  = item["name"];
                p.price = item.value("price", 0) / 100.0;
                p.stock = item["stock"];
                p.category = item.value("category", "");
                products.push_back(p);
            }
        } catch (...) {
            renderer_->drawMessage("商品数据解析失败", true);
            return;
        }
        renderer_->drawProductList(products);

        // 2. 选择商品查看详情
        int pid = getUserInputInt("\n输入商品ID查看详情（0 返回主菜单）: ", 0, 100000);
        if (pid == 0) return;  // 返回主菜单

        // 客户端校验：商品ID是否存在
        const ProductInfo* sel = nullptr;
        for (const auto& p : products) {
            if (p.id == pid) { sel = &p; break; }
        }
        if (!sel) {
            renderer_->drawMessage("商品ID不存在，请重新选择", true);
            continue;  // 回商品列表
        }

        // 3. 显示详情（列表数据已含全部字段，无需额外请求）
        renderer_->drawProductDetail(*sel);

        // 4. 加入购物车（默认当前商品，无需重新输入ID）
        int qty = getUserInputInt("加入购物车数量（0=不加入，返回列表）: ", 0, 9999);
        if (qty == 0) continue;  // 不加入，回商品列表

        // 客户端预校验：库存充足
        if (qty > sel->stock) {
            renderer_->drawMessage("库存不足，当前库存: " + std::to_string(sel->stock), true);
            continue;
        }

        json req;
        req["user_id"] = userId_;
        req["product_id"] = pid;
        req["quantity"] = qty;
        if (sendAndRecv(ProtocolConstants::C_REQ_ADD_CART, req.dump(), respCode, respBody)) {
            cart_.addItem(pid, sel->name, sel->price, qty);
            renderer_->drawMessage("已加入购物车: " + sel->name + " x" + std::to_string(qty));
        }
        // 回商品列表继续浏览
    }
}

void ClientController::handleCartManagement() {
    int respCode = 0; std::string respBody;
    while (true) {
        renderer_->drawCart(cart_.getItems(), cart_.getTotalPrice());
        std::cout << "\n购物车管理：\n1. 删除商品\n2. 修改数量\n3. 清空\n0. 返回主菜单\n";
        int choice = getUserInputInt("请选择: ", 0, 3);
        if (choice == 0) return;
        if (choice == 1) {
            int pid = getUserInputInt("输入商品ID: ", 1, 100000);
            json req={{"user_id",userId_},{"token",token_},{"product_id",pid},{"sku_id",0}};
            if(sendAndRecv(ProtocolConstants::C_REQ_CART_REMOVE,req.dump(),respCode,respBody)) cart_.removeItem(pid);
        } else if (choice == 2) {
            int pid = getUserInputInt("输入商品ID: ", 1, 100000);
            int qty = getUserInputInt("新数量(0删除): ", 0, 9999);
            json req={{"user_id",userId_},{"token",token_},{"product_id",pid},{"sku_id",0},{"quantity",qty}};
            if(sendAndRecv(ProtocolConstants::C_REQ_CART_SET,req.dump(),respCode,respBody)) cart_.updateQuantity(pid, qty);
        } else if (choice == 3) {
            json req={{"user_id",userId_},{"token",token_}};
            if(sendAndRecv(ProtocolConstants::C_REQ_CART_CLEAR,req.dump(),respCode,respBody)) cart_.clear();
        }
    }
}

void ClientController::handleCheckout() {
    if (cart_.isEmpty()) {
        renderer_->drawMessage("购物车为空，请先添加商品", true);
        return;
    }
    renderer_->drawCart(cart_.getItems(), cart_.getTotalPrice());

    int respCode = 0; std::string respBody;
    std::vector<PromoInfo> promos;
    json promoReq; promoReq["user_id"] = userId_;
    if (sendAndRecv(ProtocolConstants::C_REQ_PROMO_LIST, promoReq.dump(), respCode, respBody)) {
        try {
            auto j = json::parse(respBody);
            for (auto& p : j["promos"]) {
                PromoInfo pi;
                pi.id = p["id"]; pi.name = p["name"];
                pi.type = p["type"]; pi.param1 = p.value("param1", 0.0);
                pi.param2 = p.value("param2", 0.0);
                promos.push_back(pi);
            }
        } catch (...) {}
    }
    renderer_->drawPromoList(promos);

    // 选择促销（可多选，逗号分隔ID）
    std::vector<int> promoIds;
    std::cout << "\n选择促销ID（多个用逗号分隔，0=不用）: ";
    std::string line;
    std::getline(std::cin, line);
    if (line != "0" && !line.empty()) {
        size_t pos = 0;
        while (pos < line.size()) {
            size_t comma = line.find(',', pos);
            if (comma == std::string::npos) comma = line.size();
            int id = std::atoi(line.substr(pos, comma - pos).c_str());
            if (id > 0) promoIds.push_back(id);
            pos = comma + 1;
        }
    }

    int addressId = 0;
    std::vector<json> addresses;
    json addressReq;
    addressReq["user_id"] = userId_;
    if (!sendAndRecv(ProtocolConstants::C_REQ_ADDRESS_LIST, addressReq.dump(), respCode, respBody)) return;
    try {
        auto addressResponse = json::parse(respBody);
        if (addressResponse.contains("addresses") && addressResponse["addresses"].is_array()) {
            for (const auto& address : addressResponse["addresses"]) addresses.push_back(address);
        }
    } catch (...) {
        renderer_->drawMessage("收货地址解析失败", true);
        return;
    }

    if (addresses.empty()) {
        renderer_->drawMessage("暂无收货地址，请先创建地址");
        std::string receiver, phone, detail;
        std::cout << "收货人（输入0取消）: "; std::getline(std::cin, receiver);
        if (receiver == "0" || receiver.empty()) return;
        std::cout << "联系电话: "; std::getline(std::cin, phone);
        std::cout << "详细地址: "; std::getline(std::cin, detail);
        json createReq = {{"user_id", userId_}, {"receiver_name", receiver},
                          {"phone", phone}, {"detail", detail}, {"is_default", true}};
        if (!sendAndRecv(ProtocolConstants::C_REQ_ADDRESS_CREATE, createReq.dump(), respCode, respBody)) return;
        try {
            addressId = json::parse(respBody).value("address_id", 0);
        } catch (...) {
            renderer_->drawMessage("收货地址保存响应解析失败", true);
            return;
        }
    } else {
        std::cout << "\n可用收货地址：\n";
        for (size_t i = 0; i < addresses.size(); ++i) {
            const auto& address = addresses[i];
            std::cout << (i + 1) << ". " << address.value("receiver_name", "")
                      << " " << address.value("phone", "") << " "
                      << address.value("detail", "")
                      << (address.value("is_default", false) ? " [默认]" : "") << "\n";
        }
        int choice = getUserInputInt("请选择地址（0取消）: ", 0, static_cast<int>(addresses.size()));
        if (choice == 0) return;
        addressId = addresses[static_cast<size_t>(choice - 1)].value("id", 0);
    }
    if (addressId <= 0) {
        renderer_->drawMessage("收货地址无效，无法结算", true);
        return;
    }

    json req;
    req["user_id"] = userId_;
    req["promo_ids"] = promoIds;
    req["address_id"] = addressId;
    if (sendAndRecv(ProtocolConstants::C_REQ_CHECKOUT, req.dump(), respCode, respBody)) {
        try {
            auto j = json::parse(respBody);
            renderer_->drawOrderResult(j["order_no"].get<std::string>(),
                j["original"].get<int>() / 100.0,
                j["discount"].get<int>() / 100.0,
                j["final"].get<int>() / 100.0);
            if (j.contains("promo_desc")) {
                renderer_->drawMessage("促销明细: " + j["promo_desc"].get<std::string>());
            }
            cart_.clear();
            renderer_->drawMessage("订单生成成功，购物车已清空");
        } catch (...) {
            renderer_->drawMessage("结算响应解析失败", true);
        }
    }
}

void ClientController::handleOrders() {
    json req; req["user_id"] = userId_;
    int respCode = 0; std::string respBody;
    if (!sendAndRecv(ProtocolConstants::C_REQ_ORDER_QUERY, req.dump(), respCode, respBody)) return;

    std::vector<OrderInfo> orders;
    try {
        auto j = json::parse(respBody);
        for (auto& o : j["orders"]) {
            OrderInfo info;
            info.orderId   = o["order_id"];
            info.orderNo   = o["order_no"];
            info.original  = o.value("original", 0) / 100.0;
            info.discount  = o.value("discount", 0) / 100.0;
            info.final     = o.value("final", 0) / 100.0;
            info.status    = o["status"];
            info.returnStatus = o.value("return_status", 0);
            info.createTime = o.value("create_time", "");
            for (auto& it : o["items"]) {
                CartItem ci;
                ci.productId   = it["product_id"];
                ci.productName = it["product_name"];
                ci.price       = it.value("price", 0) / 100.0;
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
    int orderId = getUserInputInt("输入要退货的订单ID (0返回主菜单): ", 0, 1000000);
    if (orderId == 0) return;
    std::cout << "退货原因: ";
    std::string reason;
    std::getline(std::cin, reason);
    if (reason.empty()) reason = "不想要了";

    json req;
    req["user_id"] = userId_;
    req["order_id"] = orderId;
    req["reason"] = reason;
    int respCode = 0; std::string respBody;
    if (sendAndRecv(ProtocolConstants::C_REQ_RETURN, req.dump(), respCode, respBody)) {
        renderer_->drawMessage("退货申请已提交，等待审核");
    }
}

void ClientController::handleMyReturns() {
    json req; req["user_id"] = userId_;
    int respCode = 0; std::string respBody;
    if (!sendAndRecv(ProtocolConstants::C_REQ_RETURN_LIST, req.dump(), respCode, respBody)) return;
    try {
        auto j = json::parse(respBody);
        std::cout << "\n====== 我的退货进度 ======\n";
        std::cout << padR("退货ID", 8) << padR("订单ID", 8) << padR("状态", 10)
                  << padR("申请时间", 20) << "原因\n";
        const char* st[] = {"待审核", "已通过", "已拒绝", "已完成", "已完成"};
        for (auto& r : j["returns"]) {
            std::cout << padL(std::to_string(r["return_id"].get<int>()), 8)
                      << padL(std::to_string(r["order_id"].get<int>()), 8)
                      << padR(st[r.value("status", 0)], 10)
                      << padR(r.value("apply_time", ""), 20)
                      << r.value("reason", "") << "\n";
        }
    } catch (...) {
        renderer_->drawMessage("退货进度解析失败", true);
    }
}

void ClientController::handleAuditReturns() {
    json req; req["user_id"] = userId_;
    int respCode = 0; std::string respBody;
    if (!sendAndRecv(ProtocolConstants::C_REQ_RETURN_LIST, req.dump(), respCode, respBody)) return;
    try {
        auto j = json::parse(respBody);
        std::cout << "\n====== 退货审核（管理员）======\n";
        std::cout << padR("退货ID", 8) << padR("订单ID", 8) << padR("用户", 8)
                  << padR("状态", 10) << "原因\n";
        const char* st[] = {"待审核", "已通过", "已拒绝", "已完成", "已完成"};
        for (auto& r : j["returns"]) {
            std::cout << padL(std::to_string(r["return_id"].get<int>()), 8)
                      << padL(std::to_string(r["order_id"].get<int>()), 8)
                      << padL(std::to_string(r["user_id"].get<int>()), 8)
                      << padR(st[r.value("status", 0)], 10)
                      << r.value("reason", "") << "\n";
        }
    } catch (...) { return; }

    int returnId = getUserInputInt("\n审核退货ID（0 返回主菜单）: ", 0, 1000000);
    if (returnId == 0) return;
    int approve = getUserInputInt("1=通过（回补库存） 2=拒绝: ", 1, 2);
    json req2;
    req2["user_id"] = userId_;
    req2["return_id"] = returnId;
    req2["approve"] = (approve == 1);
    if (sendAndRecv(ProtocolConstants::C_REQ_RETURN_AUDIT, req2.dump(), respCode, respBody)) {
        renderer_->drawMessage(approve == 1 ? "已通过，库存已回补" : "已拒绝，订单恢复原状态");
    }
}

void ClientController::handleStats() {
    json req;
    req["user_id"] = userId_; req["days"] = 0;
    req["product_id"] = 0;  // 0=全部商品（管理员统计全量）
    int respCode = 0; std::string respBody;
    if (!sendAndRecv(ProtocolConstants::C_REQ_STATS, req.dump(), respCode, respBody)) return;
    try {
        auto j = json::parse(respBody);
        renderer_->drawDivider();
        renderer_->drawMessage("====== 销售数据统计分析 ======");
        auto s = j["summary"];
        std::cout << "订单数: " << s["order_count"].get<int>()
                  << " | 商品总价: " << std::fixed << std::setprecision(2) << s.value("total_amount",0)/100.0
                  << " | 优惠: " << s.value("discount",0)/100.0
                  << " | 实收: " << s.value("final_amount",0)/100.0
                  << " | 客单价: " << s.value("avg_order",0)/100.0
                  << " | 退货申请: " << s["return_count"].get<int>() << "\n";

        std::cout << "\n[按日销售趋势]\n" << padR("日期", 16) << padR("订单数", 8) << "销售额\n";
        for (auto& d : j["by_day"]) {
            std::cout << padR(d.value("date", ""), 16)
                      << padL(std::to_string(d["orders"].get<int>()), 8)
                      << fmtPrice(d.value("amount", 0) / 100.0) << "\n";
        }
        std::cout << "\n[热销商品排行]\n" << padR("商品ID", 8) << padR("名称", 14)
                  << padR("销量", 6) << "销售额\n";
        for (auto& p : j["by_product"]) {
            std::cout << padL(std::to_string(p["product_id"].get<int>()), 8)
                      << padR(p.value("name", ""), 14)
                      << padL(std::to_string(p["quantity"].get<int>()), 6)
                      << fmtPrice(p.value("amount", 0) / 100.0) << "\n";
        }
        if (j.contains("by_user")) {
            std::cout << "\n[用户贡献排行]\n" << padR("用户ID", 8) << padR("用户名", 14)
                      << padR("订单数", 8) << "消费额\n";
            for (auto& u : j["by_user"]) {
                std::cout << padL(std::to_string(u["user_id"].get<int>()), 8)
                          << padR(u.value("username", ""), 14)
                          << padL(std::to_string(u["orders"].get<int>()), 8)
                          << fmtPrice(u.value("amount", 0) / 100.0) << "\n";
            }
        }
    } catch (...) {
        renderer_->drawMessage("统计数据解析失败", true);
    }
}

void ClientController::handleSearch() {
    std::cout << "搜索关键词: ";
    std::string keyword;
    std::getline(std::cin, keyword);
    if (keyword.empty()) return;
    if (keyword.size() > 100) keyword.resize(100);

    json req;
    req["user_id"] = userId_;
    req["keyword"] = keyword;
    int respCode = 0; std::string respBody;
    if (!sendAndRecv(ProtocolConstants::C_REQ_SEARCH, req.dump(), respCode, respBody)) return;
    try {
        auto j = json::parse(respBody);
        std::vector<ProductInfo> products;
        for (auto& item : j["products"]) {
            ProductInfo p;
            p.id = item["id"]; p.name = item["name"];
            p.price = item.value("price", 0) / 100.0;
            p.stock = item["stock"];
            p.category = item.value("category", "");
            products.push_back(p);
        }
        renderer_->drawProductList(products);
    } catch (...) {
        renderer_->drawMessage("搜索响应解析失败", true);
    }
}

void ClientController::handleServiceMsg() {
    std::cout << "联系客服（500字以内）: ";
    std::string msg;
    std::getline(std::cin, msg);
    if (msg.empty()) return;
    if (msg.size() > 500) msg.resize(500);

    json req;
    req["user_id"] = userId_;
    req["content"] = msg;
    int respCode = 0; std::string respBody;
    if (sendAndRecv(ProtocolConstants::C_REQ_SERVICE_MSG, req.dump(), respCode, respBody)) {
        try {
            auto j = json::parse(respBody);
            renderer_->drawMessage("客服已收到: " + j.value("received", ""));
            renderer_->drawMessage("自动应答: " + j.value("reply", "我们会尽快联系您"));
        } catch (...) {}
    }
}

void ClientController::handleSwitchStyle() {
    int choice = getUserInputInt("\n选择皮肤：\n1.极简×蓝 2.详细×绿 3.详细×粉\n请选择(1-3): ", 1, 3);
    skinIndex_ = choice;
    renderer_ = createSkin(choice);
    renderer_->drawMessage("已切换到" + renderer_->getThemeName() + "-" +
                           renderer_->getStyleName());
}
