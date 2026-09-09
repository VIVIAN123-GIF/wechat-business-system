// ============================================================
// main.cpp - 微商系统客户端入口
// ============================================================
#ifdef _WIN32
#include <winsock2.h>  // 必须先于 windows.h
#include <windows.h>
#endif
#include "ClientController.h"

int main() {
#ifdef _WIN32
    SetConsoleOutputCP(65001);  // 控制台 UTF-8 输出
#endif
    ClientController controller;
    controller.run();
    return 0;
}
