#include "StatsPage.h"
#include "../ApiClient.h"
#include "../UiUtil.h"
#include <QLabel>
#include <QComboBox>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>

StatsPage::StatsPage(ApiClient* api, QWidget* parent)
    : QWidget(parent), api_(api) {

    auto* root = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(QStringLiteral("时间区间：")));
    daysCombo_ = new QComboBox;
    daysCombo_->addItem(QStringLiteral("全部"), 0);
    daysCombo_->addItem(QStringLiteral("最近7天"), 7);
    daysCombo_->addItem(QStringLiteral("最近30天"), 30);
    daysCombo_->addItem(QStringLiteral("最近90天"), 90);
    auto* goBtn = new QPushButton(QStringLiteral("统计"));
    top->addWidget(daysCombo_);
    top->addWidget(goBtn);
    top->addStretch();
    root->addLayout(top);

    summaryLbl_ = new QLabel;
    summaryLbl_->setStyleSheet(
        "background:#f0f9ff;border:1px solid #bae6fd;border-radius:6px;"
        "padding:10px;line-height:1.8;");
    root->addWidget(summaryLbl_);

    root->addWidget(new QLabel(QStringLiteral("按日销售趋势：")));
    dayTable_ = new QTableWidget(0, 3);
    dayTable_->setHorizontalHeaderLabels(
        {QStringLiteral("日期"), QStringLiteral("订单数"), QStringLiteral("销售额")});
    dayTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    dayTable_->verticalHeader()->setVisible(false);
    root->addWidget(dayTable_);

    root->addWidget(new QLabel(QStringLiteral("热销商品排行：")));
    prodTable_ = new QTableWidget(0, 4);
    prodTable_->setHorizontalHeaderLabels(
        {QStringLiteral("商品ID"), QStringLiteral("名称"),
         QStringLiteral("销量"), QStringLiteral("销售额")});
    prodTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    prodTable_->verticalHeader()->setVisible(false);
    prodTable_->horizontalHeader()->setStretchLastSection(true);
    prodTable_->setColumnWidth(1, 200);
    root->addWidget(prodTable_);

    root->addWidget(new QLabel(QStringLiteral("用户贡献排行：")));
    userTable_ = new QTableWidget(0, 4);
    userTable_->setHorizontalHeaderLabels(
        {QStringLiteral("用户ID"), QStringLiteral("用户名"),
         QStringLiteral("订单数"), QStringLiteral("消费额")});
    userTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    userTable_->verticalHeader()->setVisible(false);
    userTable_->horizontalHeader()->setStretchLastSection(true);
    userTable_->setColumnWidth(1, 200);
    root->addWidget(userTable_);

    connect(goBtn, &QPushButton::clicked, this, &StatsPage::load);
}

void StatsPage::refresh() { load(); }

void StatsPage::load() {
    int days = daysCombo_->currentData().toInt();
    api_->fetchStats(days, 0, 0, [this](quint32 code, const json& j) {
        if (code != 2060) return;

        auto s = j.value("summary", json::object());
        summaryLbl_->setText(QStringLiteral(
            "订单数：%1   商品总价：¥%2   优惠：¥%3   实收：¥%4   客单价：¥%5   退货申请：%6")
            .arg(s.value("order_count", 0))
            .arg(yuanD(s.value("total_amount", 0.0)))
            .arg(yuanD(s.value("discount", 0.0)))
            .arg(yuanD(s.value("final_amount", 0.0)))
            .arg(yuanD(s.value("avg_order", 0.0)))
            .arg(s.value("return_count", 0)));

        dayTable_->setRowCount(0);
        for (const auto& d : j.value("by_day", json::array())) {
            int r = dayTable_->rowCount();
            dayTable_->insertRow(r);
            dayTable_->setItem(r, 0, new QTableWidgetItem(
                QString::fromStdString(d.value("date", ""))));
            dayTable_->setItem(r, 1, new QTableWidgetItem(
                QString::number(d.value("orders", 0))));
            dayTable_->setItem(r, 2, new QTableWidgetItem(
                "¥" + yuanD(d.value("amount", 0.0))));
        }

        prodTable_->setRowCount(0);
        for (const auto& p : j.value("by_product", json::array())) {
            int r = prodTable_->rowCount();
            prodTable_->insertRow(r);
            prodTable_->setItem(r, 0, new QTableWidgetItem(
                QString::number(p.value("product_id", 0))));
            prodTable_->setItem(r, 1, new QTableWidgetItem(
                QString::fromStdString(p.value("name", ""))));
            prodTable_->setItem(r, 2, new QTableWidgetItem(
                QString::number(p.value("quantity", 0))));
            prodTable_->setItem(r, 3, new QTableWidgetItem(
                "¥" + yuanD(p.value("amount", 0.0))));
        }

        userTable_->setRowCount(0);
        for (const auto& u : j.value("by_user", json::array())) {
            int r = userTable_->rowCount();
            userTable_->insertRow(r);
            userTable_->setItem(r, 0, new QTableWidgetItem(
                QString::number(u.value("user_id", 0))));
            userTable_->setItem(r, 1, new QTableWidgetItem(
                QString::fromStdString(u.value("username", ""))));
            userTable_->setItem(r, 2, new QTableWidgetItem(
                QString::number(u.value("orders", 0))));
            userTable_->setItem(r, 3, new QTableWidgetItem(
                "¥" + yuanD(u.value("amount", 0.0))));
        }
    });
}
