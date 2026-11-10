// ============================================================
// MainWindow.cpp - 主窗口：登录/主界面切换、左侧导航栏、主题切换
// ============================================================
#include "MainWindow.h"
#include "ApiClient.h"
#include "NetworkClient.h"
#include "pages/LoginPage.h"
#include "pages/ShopPage.h"
#include "pages/CartPage.h"
#include "pages/CheckoutPage.h"
#include "pages/OrderPage.h"
#include "pages/ReturnPage.h"
#include "pages/StatsPage.h"
#include "pages/ServicePage.h"
#include "pages/CouponPage.h"
#include "pages/NotificationPage.h"
#include "pages/FavoritesPage.h"
#include "pages/AdminPage.h"
#include "pages/ReferralPage.h"
#include "UiUtil.h"
#include <QStackedWidget>
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QFile>
#include <QApplication>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QList>

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent), api_(new ApiClient(this)) {
    setWindowTitle(QStringLiteral("微商系统 - 客户端"));
    resize(1080, 680);

    rootStack_ = new QStackedWidget;
    setCentralWidget(rootStack_);

    // 登录页
    auto* loginPage = new LoginPage(api_);
    rootStack_->addWidget(loginPage);
    connect(loginPage, &LoginPage::loginSuccess, this, &MainWindow::onLogin);

    // 主界面（先构建，登录后显示）
    buildMainUi();
}

void MainWindow::buildMainUi() {
    shopPage_     = new ShopPage(api_);
    cartPage_     = new CartPage(api_);
    checkoutPage_ = new CheckoutPage(api_);
    orderPage_    = new OrderPage(api_);
    returnPage_   = new ReturnPage(api_);
    statsPage_    = new StatsPage(api_);
    servicePage_  = new ServicePage(api_);
    couponPage_ = new CouponPage(api_);
    notificationPage_ = new NotificationPage(api_);
    favoritesPage_ = new FavoritesPage(api_);
    adminPage_ = new AdminPage(api_);
    referralPage_ = new ReferralPage(api_);

    auto* mainWidget = new QWidget;
    auto* root = new QVBoxLayout(mainWidget);
    root->setContentsMargins(0, 0, 0, 0);

    // 顶部栏
    auto* top = new QHBoxLayout;
    top->setContentsMargins(12, 8, 12, 8);
    auto* title = new QLabel(QStringLiteral("微商系统"));
    QFont tf = title->font(); tf.setBold(true); tf.setPointSize(12);
    title->setFont(tf);
    userLbl_ = new QLabel;
    noticeLbl_ = new QLabel;
    noticeLbl_->setStyleSheet("color:#2563eb;");
    themeCombo_ = new QComboBox;
    themeCombo_->addItem(QStringLiteral("绿色主题"), "green");
    themeCombo_->addItem(QStringLiteral("蓝色主题"), "blue");
    themeCombo_->addItem(QStringLiteral("粉色主题"), "pink");
    auto* logoutBtn = new QPushButton(QStringLiteral("退出登录"));
    auto* checkinBtn = new QPushButton(QStringLiteral("每日签到"));
    top->addWidget(title);
    top->addStretch();
    top->addWidget(userLbl_);
    top->addWidget(noticeLbl_);
    top->addWidget(new QLabel(QStringLiteral("主题：")));
    top->addWidget(themeCombo_);
    top->addWidget(logoutBtn);
    top->addWidget(checkinBtn);
    root->addLayout(top);

    // 主体：左导航 + 右页面
    auto* body = new QHBoxLayout;
    body->setContentsMargins(8, 0, 8, 8);

    auto* nav = new QVBoxLayout;
    struct NavItem { const char* name; int index; };
    const NavItem items[] = {
        {QT_TRANSLATE_NOOP("nav", "商品浏览"), 0},
        {QT_TRANSLATE_NOOP("nav", "购物车"), 1},
        {QT_TRANSLATE_NOOP("nav", "促销结算"), 2},
        {QT_TRANSLATE_NOOP("nav", "我的订单"), 3},
        {QT_TRANSLATE_NOOP("nav", "售后退货"), 4},
        {QT_TRANSLATE_NOOP("nav", "销售统计"), 5},
        {QT_TRANSLATE_NOOP("nav", "联系客服"), 6},
    };
    pageStack_ = new QStackedWidget;
    pageStack_->addWidget(shopPage_);
    pageStack_->addWidget(cartPage_);
    pageStack_->addWidget(checkoutPage_);
    pageStack_->addWidget(orderPage_);
    pageStack_->addWidget(returnPage_);
    pageStack_->addWidget(statsPage_);
    pageStack_->addWidget(servicePage_);
    pageStack_->addWidget(couponPage_);
    pageStack_->addWidget(notificationPage_);
    pageStack_->addWidget(favoritesPage_);
    pageStack_->addWidget(adminPage_);
    pageStack_->addWidget(referralPage_);

    QList<QPushButton*> navButtons;
    for (const auto& it : items) {
        auto* b = new QPushButton(QString::fromUtf8(it.name));
        b->setCheckable(true);
        b->setObjectName("sideNavButton");
        b->setProperty("sideNavButton", true);
        navButtons.push_back(b);
        b->setMinimumHeight(40);
        nav->addWidget(b);
        connect(b, &QPushButton::clicked, this, [this, it, b] {
            pageStack_->setCurrentIndex(it.index);
            for (auto* o : findChildren<QPushButton*>()) {
                if (o->property("sideNavButton").toBool()) o->setChecked(false);
            }
            b->setChecked(true);
            // 进入页面时刷新
            switch (it.index) {
                case 0: shopPage_->refresh(); break;
                case 1: cartPage_->refresh(); break;
                case 2: checkoutPage_->refresh(); break;
                case 3: orderPage_->refresh(); break;
                case 4: returnPage_->refresh(); break;
                case 5: statsPage_->refresh(); break;
                default: break;
            }
        });
    }
    if (!navButtons.isEmpty()) navButtons.front()->setChecked(true);
    auto* couponNav = new QPushButton(QStringLiteral("领券中心"));
    couponNav->setCheckable(true); couponNav->setObjectName("sideNavButton"); couponNav->setProperty("sideNavButton", true); navButtons.push_back(couponNav);
    nav->addWidget(couponNav);
    connect(couponNav, &QPushButton::clicked, this, [this, couponNav] { for (auto* b : findChildren<QPushButton*>()) if (b->property("sideNavButton").toBool()) b->setChecked(false); couponNav->setChecked(true); pageStack_->setCurrentWidget(couponPage_); couponPage_->refresh(); });
    auto* notificationNav = new QPushButton(QStringLiteral("消息通知"));
    notificationNav->setCheckable(true); notificationNav->setObjectName("sideNavButton"); notificationNav->setProperty("sideNavButton", true); navButtons.push_back(notificationNav);
    nav->addWidget(notificationNav);
    connect(notificationNav, &QPushButton::clicked, this, [this, notificationNav] { for (auto* b : findChildren<QPushButton*>()) if (b->property("sideNavButton").toBool()) b->setChecked(false); notificationNav->setChecked(true); pageStack_->setCurrentWidget(notificationPage_); notificationPage_->refresh(); });
    auto* favoritesNav = new QPushButton(QStringLiteral("我的收藏"));
    favoritesNav->setCheckable(true); favoritesNav->setObjectName("sideNavButton"); favoritesNav->setProperty("sideNavButton", true); navButtons.push_back(favoritesNav);
    nav->addWidget(favoritesNav);
    connect(favoritesNav, &QPushButton::clicked, this, [this, favoritesNav] { for (auto* b : findChildren<QPushButton*>()) if (b->property("sideNavButton").toBool()) b->setChecked(false); favoritesNav->setChecked(true); pageStack_->setCurrentWidget(favoritesPage_); favoritesPage_->refresh(); });
    auto* adminNav = new QPushButton(QStringLiteral("卖家管理"));
    adminNav->setCheckable(true); adminNav->setObjectName("sideNavButton"); adminNav->setProperty("sideNavButton", true); navButtons.push_back(adminNav);
    adminNav->setVisible(false); nav->addWidget(adminNav);
    connect(adminNav, &QPushButton::clicked, this, [this, adminNav] { for (auto* b : findChildren<QPushButton*>()) if (b->property("sideNavButton").toBool()) b->setChecked(false); adminNav->setChecked(true); pageStack_->setCurrentWidget(adminPage_); adminPage_->refresh(); });
    auto* referralNav = new QPushButton(QStringLiteral("我的分销"));
    referralNav->setCheckable(true); referralNav->setObjectName("sideNavButton"); referralNav->setProperty("sideNavButton", true);
    navButtons.push_back(referralNav); nav->addWidget(referralNav);
    connect(referralNav, &QPushButton::clicked, this, [this, referralNav] {
        for (auto* b : findChildren<QPushButton*>()) if (b->property("sideNavButton").toBool()) b->setChecked(false);
        referralNav->setChecked(true); pageStack_->setCurrentWidget(referralPage_); referralPage_->refresh();
    });
    nav->addStretch();

    auto* navWidget = new QWidget;
    navWidget->setLayout(nav);
    navWidget->setFixedWidth(150);
    body->addWidget(navWidget);
    body->addWidget(pageStack_, 1);
    root->addLayout(body);

    connect(themeCombo_, &QComboBox::currentIndexChanged, this, [this](int) {
        applyTheme(themeCombo_->currentData().toString());
    });
    connect(logoutBtn, &QPushButton::clicked, this, &MainWindow::logout);
    connect(checkinBtn, &QPushButton::clicked, this, [this] {
        api_->checkIn([this](quint32 code, const json& result) {
            noticeLbl_->setText(code == 2170 ? QStringLiteral("签到成功，获得 %1 积分").arg(result.value("points_awarded", 0)) : errMsg(result));
            QTimer::singleShot(3500, this, [this] { noticeLbl_->clear(); });
        });
    });

    rootStack_->addWidget(mainWidget);
}

void MainWindow::onLogin() {
    userLbl_->setText(QStringLiteral("当前用户：%1（%2）")
        .arg(api_->username())
        .arg(api_->isAdmin() ? QStringLiteral("管理员") : QStringLiteral("普通用户")));
    rootStack_->setCurrentIndex(1);
    for (auto* button : findChildren<QPushButton*>()) {
        if (button->text() == QStringLiteral("销售统计")) button->setVisible(api_->isAdmin());
        if (button->text() == QStringLiteral("联系客服")) button->setVisible(!api_->isAdmin());
        if (button->text() == QStringLiteral("卖家管理")) button->setVisible(api_->isAdmin());
    }
    shopPage_->refresh();
    notificationPage_->refresh();
    applyTheme("green");
}

void MainWindow::applyTheme(const QString& name) {
    QFile f(":/themes/" + name + ".qss");
    if (f.open(QFile::ReadOnly | QFile::Text)) {
        qApp->setStyleSheet(QString::fromUtf8(f.readAll()));
    }
}

void MainWindow::logout() {
    api_->network()->disconnect();
    // 简单处理：提示重新登录需要重启；这里直接关闭
    close();
}
