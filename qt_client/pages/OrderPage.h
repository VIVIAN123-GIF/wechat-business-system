// ============================================================
// OrderPage.h - 订单查询 + 申请退货
// ============================================================
#pragma once
#include <QWidget>

class QTableWidget;
class QLabel;
class QComboBox;
class ApiClient;

class OrderPage : public QWidget {
    Q_OBJECT
public:
    explicit OrderPage(ApiClient* api, QWidget* parent = nullptr);
    void refresh();

private:
    ApiClient* api_;
    QTableWidget* orderTable_;
    QTableWidget* itemTable_;
    QLabel* tipLabel_;
    QComboBox* statusFilter_;

    void loadOrders();
    void applyReturn(int orderId);
};
