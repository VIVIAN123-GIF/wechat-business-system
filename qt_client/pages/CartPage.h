// ============================================================
// CartPage.h - 购物车管理（改数量 / 删除 / 清空）
// ============================================================
#pragma once
#include <QWidget>

class QTableWidget;
class QLabel;
class ApiClient;

class CartPage : public QWidget {
    Q_OBJECT
public:
    explicit CartPage(ApiClient* api, QWidget* parent = nullptr);
    void refresh();

private:
    ApiClient* api_;
    QTableWidget* table_;
    QLabel* totalLbl_;
    QLabel* countLbl_;

    void rebuild();
};
