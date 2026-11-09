// ============================================================
// CheckoutPage.h - 促销选择 + 结算
// ============================================================
#pragma once
#include <QWidget>

class QTableWidget;
class QLabel;
class QPushButton;
class QListWidget;
class QComboBox;
class ApiClient;

class CheckoutPage : public QWidget {
    Q_OBJECT
public:
    explicit CheckoutPage(ApiClient* api, QWidget* parent = nullptr);
    void refresh();

private:
    ApiClient* api_;
    QTableWidget* table_;
    QLabel* origLbl_;
    QLabel* resultLbl_;
    QListWidget* promoList_;
    QPushButton* checkoutBtn_;
    QComboBox* addressBox_;
    QComboBox* couponBox_;

    void loadPromotions();
    void loadAddresses();
    void doCheckout();
};
