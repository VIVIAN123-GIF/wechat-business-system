// ============================================================
// ClientController.h - 客户端控制器（MVC 之 Controller）
// ============================================================
#pragma once
#include <memory>
#include <string>
#include "TcpClient.h"
#include "LocalCart.h"
#include "UIRenderer.h"
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

    // 工具
    int getUserInputInt(const std::string& prompt, int min, int max);
    bool sendAndRecv(int code, const std::string& body,
                     int& respCode, std::string& respBody);
    bool ensureLoggedIn();

    std::unique_ptr<TcpClient> tcpClient_;
    std::unique_ptr<UIRenderer> renderer_;
    LocalCart cart_;
    int userId_ = 0;
    std::string username_;
    int level_ = 1;
    bool isLoggedIn_ = false;
};
