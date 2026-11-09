// ============================================================
// test_client.cpp - 集成测试（模拟完整业务链路 + 并发防超卖）
// 运行前需先启动 build/server.exe
// ============================================================
#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#endif
#include <cstring>
#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <atomic>
#include <sstream>
#include "../common/ProtocolHeader.h"
#include "../common/ProtocolEncoder.h"
#include "../common/NetUtil.h"
#include "../common/SHA256.h"
#include "../server/model/models.h"
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// 已登录会话 token（鉴权）
std::string g_token;
std::string g_adminToken;
int g_addressId = 0;

// 统一在请求中注入会话 token
json withAuth(json req) {
    if (!g_token.empty()) req["token"] = g_token;
    if (g_addressId > 0 && !req.contains("address_id")) req["address_id"] = g_addressId;
    return req;
}

struct Conn {
    SOCKET fd = INVALID_SOCKET;
    bool connect(const char* ip = "127.0.0.1", int port = 8888) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(port));
        addr.sin_addr.s_addr = inet_addr(ip);
        for (int attempt = 0; attempt < 5; ++attempt) {
            fd = socket(AF_INET, SOCK_STREAM, 0);
            if (fd == INVALID_SOCKET) return false;
            DWORD timeoutMs = 10000;
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, reinterpret_cast<const char*>(&timeoutMs), sizeof(timeoutMs));
            if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) return true;
            closesocket(fd);
            fd = INVALID_SOCKET;
            Sleep(100);
        }
        return false;
    }
    bool sendRecv(int code, const std::string& body, int& respCode, std::string& respBody) {
        auto buf = ProtocolEncoder::encode(static_cast<uint32_t>(code), body);
        int sent = 0;
        while (sent < static_cast<int>(buf.size())) {
            int n = send(fd, buf.data() + sent, static_cast<int>(buf.size()) - sent, 0);
            if (n == SOCKET_ERROR) return false;
            sent += n;
        }
        uint32_t recvCode = 0;
        if (!NetUtil::recvFullPacket(fd, recvCode, respBody)) return false;
        respCode = static_cast<int>(recvCode);
        return true;
    }
    bool sendRecvSuccess(int code, const std::string& body, int expectedCode,
                         std::string& respBody) {
        int respCode = 0;
        if (!sendRecv(code, body, respCode, respBody)) return false;
        return respCode == expectedCode;
    }
    void close() {
        if (fd != INVALID_SOCKET) {
            shutdown(fd, SD_BOTH);
            closesocket(fd);
            fd = INVALID_SOCKET;
            Sleep(150);
        }
    }
};

int g_pass = 0, g_fail = 0;
void check(const std::string& name, bool ok, const std::string& detail = "") {
    if (ok) { std::cout << "[PASS] " << name << std::endl; ++g_pass; }
    else { std::cout << "[FAIL] " << name << (detail.empty() ? "" : " | " + detail) << std::endl; ++g_fail; }
}

bool payCreatedOrder(Conn& c, int userId, int& code, std::string& body) {
    try {
        std::string createBody = body;
        int orderId = json::parse(body).value("order_id", 0);
        if (orderId <= 0) return false;
        json pay = withAuth({{"user_id", userId}, {"order_id", orderId}});
        std::string payBody;
        bool paid = c.sendRecv(1100, pay.dump(), code, payBody) && code == 2100;
        if (paid) body = createBody; else body = payBody;
        return paid;
    } catch (...) { return false; }
}

int main() {
    std::cout << "========== 微商系统集成测试 ==========" << std::endl;

    // 1. 心跳
    {
        Conn c;
        check("建立连接", c.connect());
        int code = 0; std::string body;
        check("心跳 1999 -> 2999", c.sendRecv(1999, "{\"timestamp\":1}", code, body) && code == 2999);
        c.close();
    }

    // 2. 登录
    int userId = 0;
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        json loginReq;
        loginReq["username"] = "user1";
        loginReq["password"] = hashPassword("user1", "User1#2026!");  // 密码摘要登录
        bool ok = c.sendRecv(1006, loginReq.dump(), code, body);
        check("登录 user1 -> 2006", ok && code == 2006);
        if (ok && code >= 2000 && code < 3000) {
            auto j = json::parse(body);
            check("登录成功标志", j["success"].get<bool>());
            userId = j["user_id"];
            g_token = j.value("token", "");
            check("登录返回会话 token", !g_token.empty());
        }
        // 错误密码（摘要比较）
        json badReq;
        badReq["username"] = "user1";
        badReq["password"] = hashPassword("user1", "wrong");
        ok = c.sendRecv(1006, badReq.dump(), code, body);
        check("错误密码被拒 (4001)", ok && code == 4001);
        // 无 token 访问他人接口应被拒（会话鉴权）
        json noAuthReq = {{"user_id", 999}};
        ok = c.sendRecv(1040, noAuthReq.dump(), code, body);
        check("无 token 查询订单被拒 (4002)", ok && code == 4002);
        c.close();
    }

    // 2.5 admin 登录（用于退货审核）
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        json loginReq;
        loginReq["username"] = "admin";
        loginReq["password"] = hashPassword("admin", "Admin#2026!");
        bool ok = c.sendRecv(1006, loginReq.dump(), code, body);
        check("登录 admin -> 2006", ok && code == 2006);
        if (ok && code >= 2000 && code < 3000) {
            auto j = json::parse(body);
            g_adminToken = j.value("token", "");
        }
        c.close();
    }

    // 3. 商品列表
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        json req = withAuth({{"user_id", userId}, {"receiver_name", "test receiver"}, {"phone", "13800000000"}, {"detail", "test address"}, {"is_default", true}});
        if (c.sendRecv(1121, req.dump(), code, body) && code == 2121) g_addressId = json::parse(body).value("address_id", 0);
        check("创建测试收货地址", g_addressId > 0);
        c.close();
    }

    int productCount = 0;
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        bool ok = c.sendRecv(1001, "{}", code, body);
        check("商品列表 1001 -> 2001", ok && code == 2001);
        if (ok && code >= 2000 && code < 3000) {
            auto j = json::parse(body);
            productCount = static_cast<int>(j["products"].size());
            check("商品数量为 6", productCount == 6, "实际=" + std::to_string(productCount));
        }
        c.close();
    }

    // 3.1 商品详情 1002 -> 2002
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        bool ok = c.sendRecv(1002, "{\"product_id\":1}", code, body);
        check("商品详情 1002 -> 2002", ok && code == 2002);
        if (ok && code >= 2000 && code < 3000) {
            auto j = json::parse(body);
            check("详情含名称", j.value("name", "") != "");
        }
        c.close();
    }

    // 4. 加购校验
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        json req = withAuth({{"user_id", userId}, {"product_id", 1}, {"quantity", 2}});
        bool ok = c.sendRecv(1010, req.dump(), code, body);
        check("加购校验 1010 -> 2010", ok && code == 2010);
        // 超量校验
        json req2 = withAuth({{"user_id", userId}, {"product_id", 1}, {"quantity", 9999}});
        ok = c.sendRecv(1010, req2.dump(), code, body);
        check("超量加购被拒 (1001)", ok && code == 4001 && body.find("库存不足") != std::string::npos);
        c.close();
    }

    // 5. 促销列表
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        bool ok = c.sendRecv(1020, "{}", code, body);
        check("促销列表 1020 -> 2020", ok && code == 2020);
        if (ok && code >= 2000 && code < 3000) {
            auto j = json::parse(body);
            check("促销数量为 6", j["promotions"].size() == 6,
                  "实际=" + std::to_string(j["promotions"].size()));
        }
        c.close();
    }

    // 5.5 商品搜索（按名称/分类模糊查询）
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        // 搜分类"数码" -> 2 条（降噪无线耳机 / 智能手环）
        bool ok = c.sendRecv(1005, "{\"keyword\":\"数码\"}", code, body);
        check("搜索 1005 -> 2005", ok && code == 2005);
        if (ok && code >= 2000 && code < 3000) {
            auto j = json::parse(body);
            check("搜索'数码'命中2件", j["count"] == 2,
                  "count=" + std::to_string(j["count"].get<int>()));
        }
        // 搜名称"耳机" -> 1 条（降噪无线耳机 id=4）
        ok = c.sendRecv(1005, "{\"keyword\":\"耳机\"}", code, body);
        if (ok && code >= 2000 && code < 3000) {
            auto j = json::parse(body);
            check("搜索'耳机'命中1件", j["count"] == 1 && j["products"][0]["id"] == 4);
        }
        // 搜不存在的词 -> 0 条
        ok = c.sendRecv(1005, "{\"keyword\":\"不存在的商品xyz\"}", code, body);
        if (ok && code >= 2000 && code < 3000) {
            auto j = json::parse(body);
            check("搜索无结果返回0件", j["count"] == 0);
        }
        c.close();
    }

    // 6. 结算（含促销链：满200减30）
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        json req = withAuth(json::object());
        req["user_id"] = userId;
        req["items"] = json::array({ {{"product_id", 1}, {"quantity", 2}} });
        req["promo_ids"] = json::array({2});  // 满200减30
        bool ok = c.sendRecv(1030, req.dump(), code, body);
        bool paid = ok && code == 2030 && payCreatedOrder(c, userId, code, body);
        if (paid) { code = 2030; }
        check("结算 1030 -> 2030", ok && code == 2030);
        if (ok && code >= 2000 && code < 3000) {
            auto j = json::parse(body);
            // 299*2 = 598，满200减30 -> 568（user1 非 VIP 无折上折）
            double final = j["final"];
            check("促销计算正确 (598-30=568)", std::abs(final - 56800.0) < 1.0,
                  "final=" + std::to_string(final));
            check("订单号格式 MS+时间", j["order_no"].get<std::string>().rfind("MS", 0) == 0);
            check("促销描述包含满减", j["promo_desc"].get<std::string>().find("满200减30") != std::string::npos);
        }
        c.close();
    }

    // 6.1 无效/过期促销被拒（不再静默跳过）
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        json req = withAuth(json::object());
        req["user_id"] = userId;
        req["items"] = json::array({ {{"product_id", 1}, {"quantity", 1}} });
        req["promo_ids"] = json::array({999});  // 不存在的促销
        bool ok = c.sendRecv(1030, req.dump(), code, body);
        check("无效促销被拒 (1007)", ok && code == 4001 && body.find("已过期") != std::string::npos);
        // 重复促销自动去重不报错
        json req2 = withAuth(json::object());
        req2["user_id"] = userId;
        req2["items"] = json::array({ {{"product_id", 3}, {"quantity", 1}} });
        req2["promo_ids"] = json::array({1, 1});  // 重复选两次全场9折
        ok = c.sendRecv(1030, req2.dump(), code, body);
        bool paidDuplicate = ok && code == 2030 && payCreatedOrder(c, userId, code, body);
        if (paidDuplicate) { code = 2030; }
        check("重复促销自动去重不报错", ok && code == 2030);
        c.close();
    }

    // 6.5 阶梯折扣（type=4：第2件9折 / 第3件免单）
    // 促销 id：5=第2件9折(1件原价,2件起9折) 6=第3件免单(前2件原价,第3件起免单)
    {
        Conn c; c.connect();
        int code = 0; std::string body;

        // 6.5.1 第2件9折：299 元商品买 2 件 => 299 + 299*0.9 = 568.1
        json reqA = withAuth(json::object());
        reqA["user_id"] = userId;
        reqA["items"] = json::array({ {{"product_id", 1}, {"quantity", 2}} });
        reqA["promo_ids"] = json::array({5});
        bool okA = c.sendRecv(1030, reqA.dump(), code, body);
        bool paidA = okA && code == 2030 && payCreatedOrder(c, userId, code, body);
        if (paidA) { code = 2030; }
        check("阶梯折扣 第2件9折 1030 -> 2030", okA && code == 2030);
        if (okA) {
            auto j = json::parse(body);
            double final = j["final"];
            check("第2件9折计算正确 (299+269.1=568.1)", std::abs(final - 56810.0) < 1.0,
                  "final=" + std::to_string(final));
            check("阶梯折扣描述含'第2件9折'",
                  j["promo_desc"].get<std::string>().find("第2件9折") != std::string::npos,
                  "desc=" + j["promo_desc"].get<std::string>());
        }

        // 6.5.2 第3件免单：299 元商品买 3 件 => 299*2 + 0 = 598
        json reqB = withAuth(json::object());
        reqB["user_id"] = userId;
        reqB["items"] = json::array({ {{"product_id", 1}, {"quantity", 3}} });
        reqB["promo_ids"] = json::array({6});
        bool okB = c.sendRecv(1030, reqB.dump(), code, body);
        bool paidB = okB && code == 2030 && payCreatedOrder(c, userId, code, body);
        if (paidB) { code = 2030; }
        check("阶梯折扣 第3件免单 1030 -> 2030", okB && code == 2030);
        if (okB) {
            auto j = json::parse(body);
            double final = j["final"];
            check("第3件免单计算正确 (598)", std::abs(final - 59800.0) < 1.0,
                  "final=" + std::to_string(final));
        }

        // 6.5.3 混合链叠加：第2件9折 + 满200减30（先阶梯 568.1，再满减 30 => 538.1）
        json reqC = withAuth(json::object());
        reqC["user_id"] = userId;
        reqC["items"] = json::array({ {{"product_id", 1}, {"quantity", 2}} });
        reqC["promo_ids"] = json::array({5, 2});
        bool okC = c.sendRecv(1030, reqC.dump(), code, body);
        bool paidC = okC && code == 2030 && payCreatedOrder(c, userId, code, body);
        if (paidC) { code = 2030; }
        check("混合链 第2件9折+满200减30 1030 -> 2030", okC && code == 2030);
        if (okC) {
            auto j = json::parse(body);
            double final = j["final"];
            check("混合链计算正确 (568.1-30=538.1)", std::abs(final - 53810.0) < 1.0,
                  "final=" + std::to_string(final));
            check("混合链描述含'满200减30'",
                  j["promo_desc"].get<std::string>().find("满200减30") != std::string::npos);
        }

        // 6.5.4 超出最高档延续：买 4 件（第2件9折规则）=> 1.0 + 0.9*3 = 3.7 倍单价
        json reqD = withAuth(json::object());
        reqD["user_id"] = userId;
        reqD["items"] = json::array({ {{"product_id", 2}, {"quantity", 4}} });
        reqD["promo_ids"] = json::array({5});
        bool okD = c.sendRecv(1030, reqD.dump(), code, body);
        bool paidD = okD && code == 2030 && payCreatedOrder(c, userId, code, body);
        if (paidD) { code = 2030; }
        check("阶梯折扣 超出最高档 1030 -> 2030", okD && code == 2030);
        if (okD) {
            auto j = json::parse(body);
            double final = j["final"];
            // 商品2 单价188：188*(1.0 + 0.9*3) = 188*3.7 = 695.6
            check("超出档延续计算正确 (188*3.7=695.6)", std::abs(final - 69560.0) < 1.0,
                  "final=" + std::to_string(final));
        }
        c.close();
    }

    // 7. 订单查询
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        json req = withAuth({{"user_id", userId}});
        bool ok = c.sendRecv(1040, req.dump(), code, body);
        check("订单查询 1040 -> 2040", ok && code == 2040);
        if (ok && code >= 2000 && code < 3000) {
            auto j = json::parse(body);
            check("订单列表非空", j["orders"].size() >= 1);
            if (j["orders"].size() >= 1) {
                check("订单含明细快照", j["orders"][0]["items"].size() >= 1);
            }
        }
        c.close();
    }

    // 8. 退货（原子化：申请后重复申请被拒）
    int returnId = 0;
    {
        Conn c; c.connect();
        int code = 0; std::string body;

        // 先查自己的订单，取第一个可退货订单 id（不再硬编码 order_id=1）
        json qreq = withAuth({{"user_id", userId}});
        int targetOrderId = 0;
        if (c.sendRecv(1040, qreq.dump(), code, body) && code == 2040) {
            auto j = json::parse(body);
            for (auto& o : j["orders"]) {
                int st = o.value("status", -1);
                if (st == ORDER_PAID || st == ORDER_SHIPPED || st == ORDER_COMPLETED) {
                    targetOrderId = o.value("order_id", 0);
                    break;
                }
            }
        }
        check("找到可退货订单", targetOrderId > 0);

        // 退货窗口从买家签收时间计算：先由管理员发货，再由买家确认收货。
        if (targetOrderId > 0) {
            Conn adminConn; adminConn.connect();
            json shipReq = {{"user_id", 1}, {"order_id", targetOrderId},
                            {"status", ORDER_SHIPPED}, {"token", g_adminToken}};
            bool shipped = adminConn.sendRecv(1095, shipReq.dump(), code, body) && code == 2095;
            check("管理员发货后可确认收货", shipped);
            adminConn.close();

            json receiveReq = withAuth({{"user_id", userId}, {"order_id", targetOrderId}});
            bool received = c.sendRecv(1110, receiveReq.dump(), code, body) && code == 2110;
            check("买家确认收货", received);
        }

        json req = withAuth({{"user_id", userId}, {"order_id", targetOrderId}, {"reason", "测试退货"}});
        bool ok = c.sendRecv(1050, req.dump(), code, body);
        check("退货申请 1050 -> 2050", ok && code == 2050);
        if (ok && code >= 2000 && code < 3000) {
            auto j = json::parse(body);
            returnId = j.value("return_id", 0);
            check("返回退货单号", returnId > 0);
        }
        // 重复退货应被拒（订单已置为退货中：状态校验或原子更新任一拦截）
        ok = c.sendRecv(1050, req.dump(), code, body);
        check("重复退货被拒 (1006/1008)", ok && code == 4001 &&
              (body.find("不允许退货") != std::string::npos ||
               body.find("已提交") != std::string::npos));
        c.close();
    }

    // 8.5 退货进度查询（1070/2070）
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        json req = withAuth({{"user_id", userId}});
        bool ok = c.sendRecv(1070, req.dump(), code, body);
        check("退货进度 1070 -> 2070", ok && code == 2070);
        if (ok && code >= 2000 && code < 3000) {
            auto j = json::parse(body);
            check("退货列表含刚提交的单", j["returns"].size() >= 1 &&
                  j["returns"][0]["return_id"] == returnId);
        }
        c.close();
    }

    // 8.6 非管理员审核被拒；admin 审核通过 -> 库存回补
    {
        // 用户无权审核
        Conn c; c.connect();
        int code = 0; std::string body;
        json req = withAuth({{"user_id", userId}, {"return_id", returnId}, {"approve", true}});
        bool ok = c.sendRecv(1080, req.dump(), code, body);
        check("普通用户审核被拒 (4002)", ok && code == 4002);
        c.close();

        // admin 审核通过：订单置已取消、库存回补
        Conn c2; c2.connect();
        code = 0; body.clear();
        json req2;
        req2["user_id"] = 1;
        req2["return_id"] = returnId;
        req2["approve"] = true;
        req2["token"] = g_adminToken;
        ok = c2.sendRecv(1080, req2.dump(), code, body);
        check("admin 审核通过 1080 -> 2080", ok && code == 2080);
        if (ok && code >= 2000 && code < 3000) {
            auto j = json::parse(body);
            check("审核通过提示库存回补", j["message"].get<std::string>().find("回补") != std::string::npos);
        }
        // 重复审核被拒（已审核）
        code = 0; body.clear();
        ok = c2.sendRecv(1080, req2.dump(), code, body);
        check("重复审核被拒 (1012)", ok && code == 4001);
        c2.close();
    }

    // 9. 并发防超卖（限量款背包 stock=1，8 线程同时抢购）
    {
        {
            Conn setup; setup.connect();
            int setupCode = 0; std::string setupBody;
            json stockReq = {{"user_id", 1}, {"product_id", 6}, {"action", "stock"}, {"value", 1}, {"token", g_adminToken}};
            setup.sendRecv(1190, stockReq.dump(), setupCode, setupBody);
            check("并发测试初始化库存", setupCode == 2190);
            setup.close();
        }
        std::atomic<int> successCount{0}, failCount{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i) {
            threads.emplace_back([&]() {
                Conn c;
                if (!c.connect()) { ++failCount; return; }
                int code = 0; std::string body;
                // 登录 admin（取会话 token）
                json loginReq;
                loginReq["username"] = "admin";
                loginReq["password"] = hashPassword("admin", "Admin#2026!");
                c.sendRecv(1006, loginReq.dump(), code, body);
                std::string token;
                try { token = json::parse(body).value("token", ""); } catch (...) {}
                json req;
                req["user_id"] = userId;
                req["items"] = json::array({ {{"product_id", 6}, {"quantity", 1}} });
                req["promo_ids"] = json::array();
                req["address_id"] = g_addressId;
                req["token"] = g_token;
                if (c.sendRecv(1030, req.dump(), code, body) && code == 2030 && payCreatedOrder(c, userId, code, body)) ++successCount;
                else { ++failCount; std::cerr << "concurrent request failed: code=" << code << " body=" << body << std::endl; }
                c.close();
            });
        }
        for (auto& t : threads) t.join();
        check("并发防超卖：仅 1 单成功", successCount.load() == 1,
              "成功=" + std::to_string(successCount.load()) +
              " 失败=" + std::to_string(failCount.load()));
    }

    // 10. 销售数据统计（服务端数据统计分析界面）
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        json req = withAuth({{"days", 0}, {"product_id", 0}, {"user_id", 0}});
        bool ok = c.sendRecv(1060, req.dump(), code, body);
        check("销售统计 1060 -> 2060", ok && code == 2060);
        if (ok && code >= 2000 && code < 3000) {
            auto j = json::parse(body);
            int orderCount = j["summary"]["order_count"];
            double finalAmount = j["summary"]["final_amount"];
            check("统计订单数 >= 1", orderCount >= 1,
                  "order_count=" + std::to_string(orderCount));
            check("统计实收金额 > 0", finalAmount > 0.0,
                  "final=" + std::to_string(finalAmount));
            check("按日趋势非空", j["by_day"].size() >= 1);
            // 商品排行应包含商品1（高端护肤品套装）
            bool foundP1 = false;
            for (auto& p : j["by_product"]) {
                if (p["product_id"] == 1) { foundP1 = true; break; }
            }
            check("热销排行含商品1", foundP1);
        }
        c.close();
    }

    // 11. 联系客服（社交：消息 + 自动应答）
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        json req = withAuth({{"user_id", userId}, {"content", "我想退货怎么操作？"}});
        bool ok = c.sendRecv(1065, req.dump(), code, body);
        check("客服消息 1065 -> 2065", ok && code == 2065);
        if (ok) {
            auto j = json::parse(body);
            check("客服回显收到内容", j["received"].get<std::string>().find("退货") != std::string::npos);
            check("客服自动应答非空", !j["reply"].get<std::string>().empty());
        }
        c.close();
    }

    std::cout << "\n========== 测试结果 ==========" << std::endl;
    std::cout << "通过: " << g_pass << "  失败: " << g_fail << std::endl;
    // New feature smoke tests: explainable promotions, group buying, and audit logs.
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        json explain = withAuth({{"user_id", userId},
            {"items", json::array({{{"product_id", 1}, {"quantity", 2}}})},
            {"promo_ids", json::array({5, 2})}});
        bool ok = c.sendRecv(1090, explain.dump(), code, body);
        check("智能促销解释 1090 -> 2090", ok && code == 2090);
        if (ok && code == 2090) {
            auto j = json::parse(body);
            check("返回当前与最优方案", j.contains("current") && j.contains("best"));
            check("返回方案差价", j.value("saving_vs_current", -1) >= 0);
            check("返回商品明细解释", j.contains("lines") && j["lines"].size() == 1);
        }

        json create = withAuth({{"user_id", userId}, {"product_id", 2},
                                {"target_count", 2}, {"hours", 1}});
        ok = c.sendRecv(1091, create.dump(), code, body);
        check("创建拼团 1091 -> 2091", ok && code == 2091);
        int groupId = 0;
        if (ok && code == 2091) groupId = json::parse(body).value("group_id", 0);
        c.close();

        Conn c2; c2.connect();
        json login2 = {{"username", "user2"}, {"password", hashPassword("user2", "User2#2026!")}};
        ok = c2.sendRecv(1006, login2.dump(), code, body);
        std::string user2Token;
        if (ok && code == 2006) user2Token = json::parse(body).value("token", "");
        json join = {{"user_id", 3}, {"group_id", groupId}, {"token", user2Token}};
        ok = c2.sendRecv(1092, join.dump(), code, body);
        check("加入拼团 1092 -> 2092", ok && code == 2092);
        if (ok && code == 2092) check("拼团达到目标人数", json::parse(body).value("status", 0) == 1);
        c2.close();
        Sleep(500);
        Conn c3; bool c3Connected = c3.connect();
        json query = withAuth({{"user_id", userId}});
        ok = c3.sendRecv(1093, query.dump(), code, body);
        check("查询拼团 1093 -> 2093", c3Connected && ok && code == 2093, std::to_string(code) + " " + body);
        int auditOrderId = 0;
        json orderQuery = withAuth({{"user_id", userId}});
        ok = c3.sendRecv(1040, orderQuery.dump(), code, body);
        if (ok && code == 2040 && !json::parse(body)["orders"].empty()) {
            auditOrderId = json::parse(body)["orders"][0].value("order_id", 0);
        }
        json audit = withAuth({{"user_id", userId}, {"order_id", auditOrderId}});
        ok = c3.sendRecv(1096, audit.dump(), code, body);
        check("查询订单审计 1096 -> 2096", ok && code == 2096, std::to_string(code) + " " + body);
        if (ok && code == 2096) check("审计记录非空", json::parse(body)["audit"].size() >= 1);
        c3.close();
        Sleep(500);
    }

    std::cout << "\n========== FINAL TEST RESULT ==========" << std::endl;
    std::cout << "passed: " << g_pass << " failed: " << g_fail << std::endl;
    return g_fail == 0 ? 0 : 1;
}
