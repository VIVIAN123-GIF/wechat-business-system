@echo off
cd /d %~dp0

if not exist build mkdir build

echo [1/3] Compiling SQLite (sqlite3.o) ...
gcc -O1 -c vendor\sqlite-amalgamation-3530400\sqlite3.c -o build\sqlite3.o
if errorlevel 1 goto :err

echo [2/3] Compiling server.exe ...
g++ -std=c++17 -O1 -Ivendor -Ivendor\sqlite-amalgamation-3530400 -I. ^
    server\main.cpp server\TcpServer.cpp server\WorkerPool.cpp server\HandlerFactory.cpp ^
    server\handlers\handlers.cpp server\repository\repositories.cpp server\promotion\promotions.cpp ^
    common\ProtocolEncoder.cpp build\sqlite3.o -o build\server.exe -lws2_32 -pthread
if errorlevel 1 goto :err

echo [3/3] Compiling client.exe ...
g++ -std=c++17 -O1 -Ivendor -I. ^
    client\main.cpp client\ClientController.cpp client\TcpClient.cpp client\LocalCart.cpp ^
    client\UIFactory.cpp ^
    common\ProtocolEncoder.cpp -o build\client.exe -lws2_32
if errorlevel 1 goto :err

echo.
echo Build successful:
echo   build\server.exe   (server)
echo   build\client.exe   (client)
echo.
echo Optional: compile integration test with:
echo   g++ -std=c++17 -O1 -Ivendor -I. test\test_client.cpp common\ProtocolEncoder.cpp -o build\test_client.exe -lws2_32
goto :eof

:err
echo.
echo Build FAILED. Please check errors above.
exit /b 1
