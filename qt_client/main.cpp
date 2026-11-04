// ============================================================
// main.cpp - Qt 客户端入口
// ============================================================
#include <QApplication>
#include "MainWindow.h"

int main(int argc, char** argv) {
    QApplication app(argc, argv);
    MainWindow w;
    w.show();
    return app.exec();
}
