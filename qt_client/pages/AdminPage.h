// ============================================================
// AdminPage.h - 卖家后台（商品上下架 / 发货录入）
// ============================================================
#pragma once
#include <QWidget>
class QTableWidget;
class QLabel;
class QComboBox;
class QLabel;
class QSpinBox;
class QLineEdit;
class QComboBox;
class ApiClient;
class AdminPage : public QWidget {
    Q_OBJECT
public:
    explicit AdminPage(ApiClient* api, QWidget* parent = nullptr);
    void refresh();
private:
    ApiClient* api_;
    QTableWidget* table_;
    QLineEdit* orderId_;
    QLineEdit* trackingNo_;
    QComboBox* shipmentStatus_;
    QLabel* tip_;
    QLabel* todo_;
    QComboBox* stockFilter_;
    QTableWidget* orderTable_;
    QTimer* orderTimer_;
    void loadProducts();
    void loadOrders();
};
