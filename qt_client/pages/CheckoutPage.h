#pragma once

#include <QWidget>
#include <cstdint>
#include <vector>

class QComboBox;
class QLabel;
class QPushButton;
class QSpinBox;
class QTableWidget;
class ApiClient;
struct CartLine;

class CheckoutPage : public QWidget {
    Q_OBJECT
public:
    explicit CheckoutPage(ApiClient* api, QWidget* parent = nullptr);
    void refresh();

private:
    ApiClient* api_;
    QTableWidget* table_;
    QLabel* origLbl_;
    QLabel* recommendationLbl_;
    QLabel* resultLbl_;
    QPushButton* checkoutBtn_;
    QComboBox* addressBox_;
    bool cartReady_ = false;
    bool addressReady_ = false;
    bool recommendationReady_ = false;
    bool recommendationOk_ = false;
    std::uint64_t requestSerial_ = 0;
    std::uint64_t recommendationSerial_ = 0;

    void doCheckout();
    void loadAddresses(std::uint64_t serial);
    void recommendPromotions(std::uint64_t serial);
    void updateCheckoutState();
    std::vector<CartLine> selectedLines() const;
};
