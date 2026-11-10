// ============================================================
// ClientController.h - 客户端控制器（MVC 之 Controller）
// ============================================================
#pragma once
#include <memory>
#include <string>
#include "TcpClient.h"
#include "LocalCart.h"
#include "UIRenderer.h"
#include "UIFactory.h"
#include "model/models.h"

class ClientController {
public:
    ClientController();
    ~ClientController();

    void run();  // 主入口

private:
    // 流程
    void selectUIStyle();
    bool connectServer();
    bool loginLoop();
    void mainLoop();

    // 功能
    void handleShowProducts();
    void handleCartManagement();
    void handleCheckout();
    void handleOrders();
    void handleReturn();
    void handleSwitchStyle();
    void handleStats();
    void handleSearch();
    void handleServiceMsg();
    void handleMyReturns();
    void handleAuditReturns();

    // 工具
    std::unique_ptr<UIRenderer> createSkin(int skinIndex);  // 抽象工厂创建皮肤组合
    int getUserInputInt(const std::string& prompt, int min, int max);
    bool sendAndRecv(int code, const std::string& body,
                     int& respCode, std::string& respBody);
    bool syncCartFromServer();

    std::unique_ptr<TcpClient> tcpClient_;
    std::unique_ptr<UIRenderer> renderer_;
    int skinIndex_ = 2;  // 当前皮肤组合：1 极简×蓝 / 2 详细×绿 / 3 详细×粉
    LocalCart cart_;
    int userId_ = 0;
    std::string username_;
    std::string token_;   // 登录会话 token（鉴权）
    int level_ = 1;
    bool isLoggedIn_ = false;
};
