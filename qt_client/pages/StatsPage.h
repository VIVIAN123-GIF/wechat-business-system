// ============================================================
// StatsPage.h - 销售数据统计分析
// ============================================================
#pragma once
#include <QWidget>

class QLabel;
class QTableWidget;
class QComboBox;
class ApiClient;

class StatsPage : public QWidget {
    Q_OBJECT
public:
    explicit StatsPage(ApiClient* api, QWidget* parent = nullptr);
    void refresh();

private:
    ApiClient* api_;
    QComboBox* daysCombo_;
    QLabel* summaryLbl_;
    QTableWidget* dayTable_;
    QTableWidget* prodTable_;
    QTableWidget* userTable_;

    void load();
};
