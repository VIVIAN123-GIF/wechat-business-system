// ============================================================
// NotificationPage.h - 消息通知（订单状态变更 / 活动推送）
// ============================================================
#pragma once
#include <QWidget>
class QListWidget;
class QLabel;
class ApiClient;
class NotificationPage : public QWidget {
    Q_OBJECT
public:
    explicit NotificationPage(ApiClient* api, QWidget* parent=nullptr);
    void refresh();
private:
    ApiClient* api_;
    QListWidget* list_;
    QLabel* tip_;
};
