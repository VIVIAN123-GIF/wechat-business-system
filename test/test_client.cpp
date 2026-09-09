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
#include <nlohmann/json.hpp>

using json = nlohmann::json;

struct Conn {
    SOCKET fd = INVALID_SOCKET;
    bool connect(const char* ip = "127.0.0.1", int port = 8888) {
        WSADATA wsa;
        WSAStartup(MAKEWORD(2, 2), &wsa);
        fd = socket(AF_INET, SOCK_STREAM, 0);
        if (fd == INVALID_SOCKET) return false;
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(static_cast<u_short>(port));
        addr.sin_addr.s_addr = inet_addr(ip);
        return ::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0;
    }
    bool sendRecv(int code, const std::string& body, int& respCode, std::string& respBody) {
        auto buf = ProtocolEncoder::encode(static_cast<uint32_t>(code), body);
        int sent = 0;
        while (sent < static_cast<int>(buf.size())) {
            int n = send(fd, buf.data() + sent, static_cast<int>(buf.size()) - sent, 0);
            if (n == SOCKET_ERROR) return false;
            sent += n;
        }
        char headBuf[sizeof(ProtocolHeader)];
        int got = 0;
        while (got < static_cast<int>(sizeof(ProtocolHeader))) {
            int n = recv(fd, headBuf + got, static_cast<int>(sizeof(ProtocolHeader)) - got, 0);
            if (n <= 0) return false;
            got += n;
        }
        ProtocolHeader h;
        std::memcpy(&h, headBuf, sizeof(h));
        respCode = static_cast<int>(ntohl(h.code));
        uint32_t len = ntohl(h.length);
        respBody.resize(len);
        got = 0;
        while (got < static_cast<int>(len)) {
            int n = recv(fd, &respBody[0] + got, static_cast<int>(len) - got, 0);
            if (n <= 0) return false;
            got += n;
        }
        return true;
    }
    void close() { if (fd != INVALID_SOCKET) closesocket(fd); }
};

int g_pass = 0, g_fail = 0;
void check(const std::string& name, bool ok, const std::string& detail = "") {
    if (ok) { std::cout << "[PASS] " << name << std::endl; ++g_pass; }
    else { std::cout << "[FAIL] " << name << (detail.empty() ? "" : " | " + detail) << std::endl; ++g_fail; }
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
        bool ok = c.sendRecv(1006, "{\"username\":\"user1\",\"password\":\"123456\"}", code, body);
        check("登录 user1 -> 2006", ok && code == 2006);
        if (ok) {
            auto j = json::parse(body);
            check("登录成功标志", j["success"].get<bool>());
            userId = j["user_id"];
        }
        // 错误密码
        ok = c.sendRecv(1006, "{\"username\":\"user1\",\"password\":\"wrong\"}", code, body);
        check("错误密码被拒 (4001)", ok && code == 4001);
        c.close();
    }

    // 3. 商品列表
    int productCount = 0;
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        bool ok = c.sendRecv(1001, "{}", code, body);
        check("商品列表 1001 -> 2001", ok && code == 2001);
        if (ok) {
            auto j = json::parse(body);
            productCount = static_cast<int>(j["products"].size());
            check("商品数量为 6", productCount == 6, "实际=" + std::to_string(productCount));
        }
        c.close();
    }

    // 4. 加购校验
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        json req = {{"user_id", userId}, {"product_id", 1}, {"quantity", 2}};
        bool ok = c.sendRecv(1010, req.dump(), code, body);
        check("加购校验 1010 -> 2010", ok && code == 2010);
        // 超量校验
        json req2 = {{"user_id", userId}, {"product_id", 1}, {"quantity", 9999}};
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
        if (ok) {
            auto j = json::parse(body);
            check("促销数量为 4", j["promotions"].size() == 4);
        }
        c.close();
    }

    // 6. 结算（含促销链：满200减30）
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        json req;
        req["user_id"] = userId;
        req["items"] = json::array({ {{"product_id", 1}, {"quantity", 2}} });
        req["promo_ids"] = json::array({2});  // 满200减30
        bool ok = c.sendRecv(1030, req.dump(), code, body);
        check("结算 1030 -> 2030", ok && code == 2030);
        if (ok) {
            auto j = json::parse(body);
            // 299*2 = 598，满200减30 -> 568（user1 非 VIP 无折上折）
            double final = j["final"];
            check("促销计算正确 (598-30=568)", std::abs(final - 568.0) < 0.001,
                  "final=" + std::to_string(final));
            check("订单号格式 MS+时间", j["order_no"].get<std::string>().rfind("MS", 0) == 0);
            check("促销描述包含满减", j["promo_desc"].get<std::string>().find("满200减30") != std::string::npos);
        }
        c.close();
    }

    // 7. 订单查询
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        json req = {{"user_id", userId}};
        bool ok = c.sendRecv(1040, req.dump(), code, body);
        check("订单查询 1040 -> 2040", ok && code == 2040);
        if (ok) {
            auto j = json::parse(body);
            check("订单列表非空", j["orders"].size() >= 1);
            if (j["orders"].size() >= 1) {
                check("订单含明细快照", j["orders"][0]["items"].size() >= 1);
            }
        }
        c.close();
    }

    // 8. 退货
    {
        Conn c; c.connect();
        int code = 0; std::string body;
        json req = {{"user_id", userId}, {"order_id", 1}, {"reason", "测试退货"}};
        bool ok = c.sendRecv(1050, req.dump(), code, body);
        check("退货申请 1050 -> 2050", ok && code == 2050);
        // 重复退货应被拒（状态已为退货中）
        ok = c.sendRecv(1050, req.dump(), code, body);
        check("重复退货被拒 (1006)", ok && code == 4001);
        c.close();
    }

    // 9. 并发防超卖（限量款背包 stock=1，8 线程同时抢购）
    {
        std::atomic<int> successCount{0}, failCount{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < 8; ++i) {
            threads.emplace_back([&]() {
                Conn c;
                if (!c.connect()) { ++failCount; return; }
                int code = 0; std::string body;
                // 登录 admin
                c.sendRecv(1006, "{\"username\":\"admin\",\"password\":\"123456\"}", code, body);
                json req;
                req["user_id"] = 1;
                req["items"] = json::array({ {{"product_id", 6}, {"quantity", 1}} });
                req["promo_ids"] = json::array();
                if (c.sendRecv(1030, req.dump(), code, body) && code == 2030) {
                    ++successCount;
                } else {
                    ++failCount;
                }
                c.close();
            });
        }
        for (auto& t : threads) t.join();
        check("并发防超卖：仅 1 单成功", successCount.load() == 1,
              "成功=" + std::to_string(successCount.load()) +
              " 失败=" + std::to_string(failCount.load()));
    }

    std::cout << "\n========== 测试结果 ==========" << std::endl;
    std::cout << "通过: " << g_pass << "  失败: " << g_fail << std::endl;
    return g_fail == 0 ? 0 : 1;
}
