#pragma once
#include <QWidget>

class ApiClient;
class QLabel;
class QTableWidget;
class QPushButton;

class ReferralPage : public QWidget {
    Q_OBJECT
public:
    explicit ReferralPage(ApiClient* api, QWidget* parent = nullptr);
    void refresh();
private:
    ApiClient* api_;
    QLabel* inviteCode_;
    QLabel* inviter_;
    QLabel* summary_;
    QLabel* tip_;
    QTableWidget* inviteeTable_;
    QTableWidget* commissionTable_;
    void load();
};
