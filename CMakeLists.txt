# ============================================================
# CMakeLists.txt - 微商系统面向对象课设 构建脚本
# 用法（MinGW）：
#   cmake -S . -B build -G "MinGW Makefiles"
#   cmake --build build
# 或直接沿用 build.bat（g++ 命令行）
# ============================================================
cmake_minimum_required(VERSION 3.15)
project(wechat_mall LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 第三方头文件根目录
include_directories(${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/vendor)

# ---------- SQLite amalgamation 静态库 ----------
add_library(sqlite3 STATIC vendor/sqlite-amalgamation-3530400/sqlite3.c)
target_include_directories(sqlite3 PUBLIC
    ${CMAKE_SOURCE_DIR}/vendor/sqlite-amalgamation-3530400)

# ---------- common 协议层静态库（客户端/服务端/测试共用） ----------
add_library(common STATIC common/ProtocolEncoder.cpp)
target_include_directories(common PUBLIC ${CMAKE_SOURCE_DIR})
if(WIN32)
    target_link_libraries(common PUBLIC ws2_32)
endif()

# ---------- 服务端 ----------
add_executable(server
    server/main.cpp
    server/TcpServer.cpp
    server/WorkerPool.cpp
    server/HandlerFactory.cpp
    server/handlers/handlers.cpp
    server/repository/repositories.cpp
    server/promotion/promotions.cpp)
target_link_libraries(server PRIVATE common sqlite3)
target_include_directories(server PRIVATE ${CMAKE_SOURCE_DIR}/server)

# ---------- 客户端 ----------
add_executable(client
    client/main.cpp
    client/ClientController.cpp
    client/TcpClient.cpp
    client/LocalCart.cpp
    client/UIFactory.cpp)
target_link_libraries(client PRIVATE common)
target_include_directories(client PRIVATE ${CMAKE_SOURCE_DIR}/client)

# ---------- 集成测试（走真实 socket，需先启动 server） ----------
add_executable(test_integration test/test_client.cpp)
target_link_libraries(test_integration PRIVATE common)
if(WIN32)
    target_link_libraries(test_integration PRIVATE ws2_32)
endif()

# ---------- 单元测试（纯逻辑，无需启动 server） ----------
add_executable(unit_tests
    test/unit_tests.cpp
    server/promotion/promotions.cpp
    server/repository/repositories.cpp)
target_link_libraries(unit_tests PRIVATE common sqlite3)
target_include_directories(unit_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/server ${CMAKE_SOURCE_DIR}/vendor/sqlite-amalgamation-3530400)

enable_testing()
add_test(NAME unit_tests COMMAND unit_tests)
if(WIN32)
    add_test(NAME integration_tests
        COMMAND powershell -NoProfile -ExecutionPolicy Bypass
        -File ${CMAKE_SOURCE_DIR}/test/run_integration.ps1
        -ServerExe $<TARGET_FILE:server>
        -TestExe $<TARGET_FILE:test_integration>
        -ProjectRoot ${CMAKE_SOURCE_DIR})
endif()
