@echo off
chcp 65001 >nul
cd /d %~dp0

if not exist build mkdir build

echo [1/3] 编译 SQLite (sqlite3.o) ...
gcc -O1 -c vendor\sqlite-amalgamation-3530400\sqlite3.c -o build\sqlite3.o
if errorlevel 1 goto :err

echo [2/3] 编译服务端 server.exe ...
g++ -std=c++17 -O1 -Ivendor -Ivendor\sqlite-amalgamation-3530400 -I. ^
    server\main.cpp server\TcpServer.cpp server\WorkerPool.cpp server\HandlerFactory.cpp ^
    server\handlers\handlers.cpp server\repository\repositories.cpp server\promotion\promotions.cpp ^
    common\ProtocolEncoder.cpp build\sqlite3.o -o build\server.exe -lws2_32 -pthread
if errorlevel 1 goto :err

echo [3/3] 编译客户端 client.exe ...
g++ -std=c++17 -O1 -Ivendor -I. ^
    client\main.cpp client\ClientController.cpp client\TcpClient.cpp client\LocalCart.cpp ^
    client\UIFactory.cpp ^
    common\ProtocolEncoder.cpp -o build\client.exe -lws2_32
if errorlevel 1 goto :err

echo.
echo 构建成功：
echo   build\server.exe   服务端
echo   build\client.exe   客户端
echo 集成测试可选编译：g++ -std=c++17 -O1 -Ivendor -I. test\test_client.cpp common\ProtocolEncoder.cpp -o build\test_client.exe -lws2_32
goto :eof

:err
echo.
echo 构建失败，请检查上方错误信息。
exit /b 1
