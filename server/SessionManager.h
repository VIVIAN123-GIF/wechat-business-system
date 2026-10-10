// ============================================================
// SessionManager.h - 会话管理（登录态 token 校验）
// 登录成功后生成 token 并绑定 user_id；业务请求携带 token 完成鉴权
// ============================================================
#pragma once
#include <string>
#include <mutex>
#include <unordered_map>
#include <random>
#include <chrono>
#include <sstream>

class SessionManager {
public:
    static SessionManager& instance() {
        static SessionManager s;
        return s;
    }

    // 登录成功后创建会话，返回 token
    std::string create(int userId) {
        std::lock_guard<std::mutex> lock(mtx_);
        std::string token = genToken(userId);
        sessions_[token] = userId;
        return token;
    }

    // 校验 token 是否有效；有效则通过 outUserId 返回绑定的用户
    bool getUserId(const std::string& token, int& outUserId) const {
        std::lock_guard<std::mutex> lock(mtx_);
        auto it = sessions_.find(token);
        if (it == sessions_.end()) return false;
        outUserId = it->second;
        return true;
    }

    // 注销会话
    void remove(const std::string& token) {
        std::lock_guard<std::mutex> lock(mtx_);
        sessions_.erase(token);
    }

private:
    SessionManager() = default;
    SessionManager(const SessionManager&) = delete;
    SessionManager& operator=(const SessionManager&) = delete;

    // token = user_id + 时间戳 + 双随机数（足够区分，课程演示级）
    static std::string genToken(int userId) {
        std::random_device rd;
        auto now = std::chrono::system_clock::now().time_since_epoch().count();
        std::ostringstream oss;
        oss << userId << "-" << now << "-" << rd() << "-" << rd();
        return oss.str();
    }

    mutable std::mutex mtx_;
    std::unordered_map<std::string, int> sessions_;
};
