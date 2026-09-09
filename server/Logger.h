// ============================================================
// Logger.h - 线程安全日志单例
// ============================================================
#pragma once
#include <iostream>
#include <string>
#include <mutex>
#include <chrono>
#include <ctime>
#include <sstream>

class Logger {
public:
    static Logger& instance() { static Logger l; return l; }

    void info(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mtx_);
        std::cout << "[INFO " << now() << "] " << msg << std::endl;
    }
    void warn(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mtx_);
        std::cout << "[WARN " << now() << "] " << msg << std::endl;
    }
    void error(const std::string& msg) {
        std::lock_guard<std::mutex> lock(mtx_);
        std::cerr << "[ERROR " << now() << "] " << msg << std::endl;
    }

private:
    static std::string now() {
        auto t = std::time(nullptr);
        std::tm tm{};
#ifdef _WIN32
        localtime_s(&tm, &t);
#else
        localtime_r(&t, &tm);
#endif
        char buf[32];
        std::strftime(buf, sizeof(buf), "%H:%M:%S", &tm);
        return buf;
    }
    std::mutex mtx_;
};

#define LOG_INFO(msg)  Logger::instance().info(msg)
#define LOG_WARN(msg)  Logger::instance().warn(msg)
#define LOG_ERROR(msg) Logger::instance().error(msg)
