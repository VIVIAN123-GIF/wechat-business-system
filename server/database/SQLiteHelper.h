// ============================================================
// SQLiteHelper.h - SQLite 封装 + 每线程连接管理
// ============================================================
#pragma once
#include <string>
#include <stdexcept>
#include <vector>
#include <mutex>
#include <memory>
#include <sqlite3.h>
#include "../Logger.h"

// SQLite 封装：安全打开/关闭/执行，prepare 防 SQL 注入
class SQLiteHelper {
public:
    explicit SQLiteHelper(const std::string& dbFile) : db_(nullptr) {
        int rc = sqlite3_open(dbFile.c_str(), &db_);
        if (rc != SQLITE_OK) {
            std::string msg = "无法打开数据库: " + std::string(sqlite3_errmsg(db_));
            if (db_) sqlite3_close(db_);
            db_ = nullptr;
            throw std::runtime_error(msg);
        }
        execute("PRAGMA foreign_keys = ON;");
        execute("PRAGMA busy_timeout = 5000;");
    }
    ~SQLiteHelper() { if (db_) sqlite3_close(db_); }

    sqlite3* raw() { return db_; }
    SQLiteHelper(const SQLiteHelper&) = delete;
    SQLiteHelper& operator=(const SQLiteHelper&) = delete;

    // 执行非查询语句（INSERT/UPDATE/DELETE/DDL）
    bool execute(const std::string& sql) {
        char* errMsg = nullptr;
        int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            LOG_ERROR(std::string("SQL执行失败: ") + sql + " | " +
                      (errMsg ? errMsg : "unknown"));
            if (errMsg) sqlite3_free(errMsg);
            return false;
        }
        return true;
    }

    // 预编译语句（调用方负责 sqlite3_finalize）
    sqlite3_stmt* prepare(const std::string& sql) {
        sqlite3_stmt* stmt = nullptr;
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr);
        if (rc != SQLITE_OK) {
            LOG_ERROR(std::string("SQL预编译失败: ") + sql + " | " + sqlite3_errmsg(db_));
            return nullptr;
        }
        return stmt;
    }

    long long lastInsertRowId() const { return sqlite3_last_insert_rowid(db_); }

private:
    sqlite3* db_;
};

// 线程局部连接管理器：每个线程独立 sqlite3 连接，避免多线程共享连接
class DatabaseManager {
public:
    static DatabaseManager& instance() { static DatabaseManager m; return m; }

    void init(const std::string& dbFile) {
        std::lock_guard<std::mutex> lock(initMtx_);
        if (initialized_) return;
        dbFile_ = dbFile;
        // 主线程连接用于初始化
        auto* conn = getConnection();
        if (conn) initialized_ = true;
    }

    sqlite3* getConnection() {
        // RAII 持有每线程连接：线程退出时 thread_local 析构自动 close+delete，杜绝连接泄漏
        struct ConnectionHolder {
            SQLiteHelper* helper = nullptr;
            ~ConnectionHolder() { if (helper) { delete helper; helper = nullptr; } }
        };
        thread_local ConnectionHolder holder;
        if (!holder.helper) {
            try {
                holder.helper = new SQLiteHelper(dbFile_);
                LOG_INFO("线程建立数据库连接");
            } catch (const std::exception& e) {
                LOG_ERROR(std::string("数据库连接失败: ") + e.what());
                return nullptr;
            }
        }
        return holder.helper->raw();
    }

    bool isInitialized() const { return initialized_; }

private:
    DatabaseManager() : initialized_(false) {}
    std::string dbFile_ = "ms.db";
    std::mutex initMtx_;
    bool initialized_;
};
