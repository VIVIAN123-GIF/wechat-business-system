// ============================================================
// LoginPage.h - 登录 / 注册页（含连接服务端状态）
// ============================================================
#pragma once
#include <QWidget>

class QLineEdit;
class QLabel;
class QPushButton;
class ApiClient;

class LoginPage : public QWidget {
    Q_OBJECT
public:
    explicit LoginPage(ApiClient* api, QWidget* parent = nullptr);

signals:
    void loginSuccess();

private:
    ApiClient* api_;
    QLineEdit* userEdit_;
    QLineEdit* pwdEdit_;
    QLabel*    connStatus_;
    QLabel*    tipLabel_;
    QPushButton* loginBtn_;
    QPushButton* regBtn_;

    void doLogin();
    void doRegister();
};
