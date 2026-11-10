// ============================================================
// MainWindow.h - 主窗口（登录页与主界面切换、导航、主题切换）
// ============================================================
#pragma once
#include <QMainWindow>

class QStackedWidget;
class QLabel;
class QComboBox;
class ApiClient;
class LoginPage;
class ShopPage;
class CartPage;
class CheckoutPage;
class OrderPage;
class ReturnPage;
class StatsPage;
class ServicePage;
class CouponPage;
class NotificationPage;
class FavoritesPage;
class AdminPage;
class ReferralPage;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);

private:
    ApiClient* api_;
    QStackedWidget* rootStack_;
    QStackedWidget* pageStack_;
    QLabel* userLbl_;
    QLabel* noticeLbl_;
    QComboBox* themeCombo_;

    ShopPage*     shopPage_;
    CartPage*     cartPage_;
    CheckoutPage* checkoutPage_;
    OrderPage*    orderPage_;
    ReturnPage*   returnPage_;
    StatsPage*    statsPage_;
    ServicePage*  servicePage_;
    CouponPage* couponPage_;
    NotificationPage* notificationPage_;
    FavoritesPage* favoritesPage_;
    AdminPage* adminPage_;
    ReferralPage* referralPage_;

    void buildMainUi();
    void onLogin();
    void logout();
    void applyTheme(const QString& name);
};
