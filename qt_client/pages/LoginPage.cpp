#include "LoginPage.h"
#include "../ApiClient.h"
#include "../NetworkClient.h"
#include "../UiUtil.h"
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>

LoginPage::LoginPage(ApiClient* api, QWidget* parent)
    : QWidget(parent), api_(api) {

    auto* root = new QVBoxLayout(this);
    root->addStretch();

    auto* card = new QWidget;
    card->setMinimumWidth(420);
    auto* form = new QFormLayout(card);

    connStatus_ = new QLabel(QStringLiteral("正在连接服务端 127.0.0.1:8888 ..."));
    connStatus_->setStyleSheet("color:#d97706;");
    form->addRow(connStatus_);

    userEdit_ = new QLineEdit;
    userEdit_->setPlaceholderText("admin");
    pwdEdit_ = new QLineEdit;
    pwdEdit_->setPlaceholderText("请输入密码");
    pwdEdit_->setEchoMode(QLineEdit::Password);
    form->addRow(QStringLiteral("用户名："), userEdit_);
    form->addRow(QStringLiteral("密码："), pwdEdit_);

    loginBtn_ = new QPushButton(QStringLiteral("登录"));
    regBtn_   = new QPushButton(QStringLiteral("注册"));
    loginBtn_->setEnabled(false);
    auto* btns = new QHBoxLayout;
    btns->addWidget(loginBtn_);
    btns->addWidget(regBtn_);
    form->addRow(btns);

    tipLabel_ = new QLabel;
    tipLabel_->setWordWrap(true);
    tipLabel_->setStyleSheet("color:#dc2626;");
    form->addRow(tipLabel_);

    auto* wrap = new QHBoxLayout;
    wrap->addStretch();
    wrap->addWidget(card);
    wrap->addStretch();
    root->addLayout(wrap);
    root->addStretch();

    connect(loginBtn_, &QPushButton::clicked, this, &LoginPage::doLogin);
    connect(regBtn_, &QPushButton::clicked, this, &LoginPage::doRegister);

    // 监听连接结果
    connect(api_->network(), &NetworkClient::connected, this, [this]() {
        connStatus_->setText(QStringLiteral("已连接服务端"));
        connStatus_->setStyleSheet("color:#15803d;");
        loginBtn_->setEnabled(true);
    });
    connect(api_->network(), &NetworkClient::connectionFailed, this,
            [this](const QString& r) {
        if (!api_->network()->isConnected()) {
            connStatus_->setText(QStringLiteral("连接失败：") + r);
            connStatus_->setStyleSheet("color:#dc2626;");
            loginBtn_->setEnabled(false);
        }
    });

    // 发起连接
    api_->network()->connectToServer("127.0.0.1", 8888);
}

void LoginPage::doLogin() {
    QString u = userEdit_->text().trimmed();
    QString p = pwdEdit_->text();
    if (u.isEmpty() || p.isEmpty()) {
        tipLabel_->setText(QStringLiteral("请输入用户名和密码"));
        return;
    }
    loginBtn_->setEnabled(false);
    api_->login(u, p, [this](quint32 code, const json& j) {
        loginBtn_->setEnabled(true);
        if (code == 2006) {
            tipLabel_->setText("");
            emit loginSuccess();
        } else {
            tipLabel_->setText(errMsg(j));
        }
    });
}

void LoginPage::doRegister() {
    QString u = userEdit_->text().trimmed();
    QString p = pwdEdit_->text();
    if (u.size() < 2 || p.size() < 4) {
        tipLabel_->setText(QStringLiteral("用户名至少2位、密码至少4位"));
        return;
    }
    regBtn_->setEnabled(false);
    api_->registerUser(u, p, [this](quint32 code, const json& j) {
        regBtn_->setEnabled(true);
        if (code == 2007) {
            tipLabel_->setStyleSheet("color:#15803d;");
            tipLabel_->setText(QStringLiteral("注册成功，请点击登录"));
        } else {
            tipLabel_->setStyleSheet("color:#dc2626;");
            tipLabel_->setText(errMsg(j));
        }
    });
}
