#pragma once
#include <QWidget>
class QListWidget;
class QLabel;
class ApiClient;
class CouponPage : public QWidget {
    Q_OBJECT
public:
    explicit CouponPage(ApiClient* api, QWidget* parent=nullptr);
    void refresh();
private:
    ApiClient* api_;
    QListWidget* list_;
    QLabel* tip_;
    void loadCoupons();
};
