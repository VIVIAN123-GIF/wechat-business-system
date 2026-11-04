// ============================================================
// ReturnPage.h - 退货进度（用户）/ 退货审核（管理员）
// ============================================================
#pragma once
#include <QWidget>

class QTableWidget;
class QLabel;
class ApiClient;

class ReturnPage : public QWidget {
    Q_OBJECT
public:
    explicit ReturnPage(ApiClient* api, QWidget* parent = nullptr);
    void refresh();

private:
    ApiClient* api_;
    QTableWidget* table_;
    QLabel* titleLbl_;
    QLabel* tipLabel_;

    void load();
};
