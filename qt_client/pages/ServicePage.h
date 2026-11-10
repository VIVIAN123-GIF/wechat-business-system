// ============================================================
// ServicePage.h - 联系客服（社交消息）
// ============================================================
#pragma once
#include <QWidget>

class QTextEdit;
class QLineEdit;
class QLabel;
class ApiClient;

class ServicePage : public QWidget {
    Q_OBJECT
public:
    explicit ServicePage(ApiClient* api, QWidget* parent = nullptr);

private:
    ApiClient* api_;
    QTextEdit* chatView_;
    QLineEdit* input_;
    QLabel* tip_;

    void send();
};
