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
    // 启用 VT 转义（ANSI 颜色码），Windows 10+ 终端生效
    HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    DWORD mode = 0;
    if (GetConsoleMode(hOut, &mode)) {
        SetConsoleMode(hOut, mode | 0x0004 /* ENABLE_VIRTUAL_TERMINAL_PROCESSING */);
    }
#endif
    ClientController controller;
    controller.run();
    return 0;
}
