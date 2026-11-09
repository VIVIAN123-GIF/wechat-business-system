#pragma once
#include <QWidget>
class QTableWidget;
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
    void loadProducts();
};
