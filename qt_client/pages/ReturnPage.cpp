// ============================================================
// ReturnPage.cpp - 退货进度（用户）/ 退货审核（管理员）
// ============================================================
#include "ReturnPage.h"
#include "../ApiClient.h"
#include "../UiUtil.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QInputDialog>

ReturnPage::ReturnPage(ApiClient* api, QWidget* parent)
    : QWidget(parent), api_(api) {

    auto* root = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    titleLbl_ = new QLabel;
    QFont f = titleLbl_->font(); f.setBold(true); f.setPointSize(11);
    titleLbl_->setFont(f);
    top->addWidget(titleLbl_);
    top->addStretch();
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新"));
    top->addWidget(refreshBtn);
    root->addLayout(top);

    table_ = new QTableWidget(0, 9);
    table_->setHorizontalHeaderLabels({QStringLiteral("ID"), QStringLiteral("订单"), QStringLiteral("用户"), QStringLiteral("原因"), QStringLiteral("状态"), QStringLiteral("申请时间"), QStringLiteral("操作"), QStringLiteral("审核意见"), QStringLiteral("退款金额")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(3, 160);
    table_->setColumnWidth(5, 160);
    root->addWidget(table_);

    tipLabel_ = new QLabel;
    tipLabel_->setStyleSheet("color:#15803d;");
    root->addWidget(tipLabel_);

    connect(refreshBtn, &QPushButton::clicked, this, &ReturnPage::refresh);
}

void ReturnPage::refresh() {
    tipLabel_->setText("");
    load();
}

void ReturnPage::load() {
    bool admin = api_->isAdmin();
    titleLbl_->setText(admin ? QStringLiteral("退货审核（全部用户）")
                             : QStringLiteral("我的退货进度"));
    api_->fetchReturns([this, admin](quint32 code, const json& j) {
        table_->setRowCount(0);
        if (code != 2070) return;

        for (const auto& r : j["returns"]) {
            int row = table_->rowCount();
            table_->insertRow(row);
            int status = r.value("status", 0);

            table_->setItem(row, 0, new QTableWidgetItem(
                QString::number(r.value("return_id", 0))));
            table_->setItem(row, 1, new QTableWidgetItem(
                QString::number(r.value("order_id", 0))));
            table_->setItem(row, 2, new QTableWidgetItem(
                QString::number(r.value("user_id", 0))));
            table_->setItem(row, 3, new QTableWidgetItem(
                QString::fromStdString(r.value("reason", ""))));
            QString statusText = returnStatusText(status);
            if (r.value("is_overdue", false)) statusText += QStringLiteral("（审核超时）");
            table_->setItem(row, 4, new QTableWidgetItem(statusText));
            table_->setItem(row, 5, new QTableWidgetItem(
                QString::fromStdString(r.value("apply_time", ""))));
            table_->setItem(row, 7, new QTableWidgetItem(QString::fromStdString(r.value("audit_comment", ""))));
            table_->setItem(row, 8, new QTableWidgetItem("￥" + yuan(r.value("refund_amount", 0))));

            if (admin && (status == 0 || status == 1)) {
                auto* w = new QWidget;
                auto* hl = new QHBoxLayout(w);
                hl->setContentsMargins(2, 2, 2, 2);
                auto* okBtn = new QPushButton(QStringLiteral("通过"));
                auto* noBtn = status == 0 ? new QPushButton(QStringLiteral("拒绝")) : nullptr;
                if (status == 0) { okBtn->setText(QStringLiteral("同意退货")); hl->addWidget(okBtn); hl->addWidget(noBtn); }
                if (status == 1) { okBtn->setText(QStringLiteral("完成退款")); hl->addWidget(okBtn); }
                table_->setCellWidget(row, 6, w);

                int rid = r.value("return_id", 0);
                connect(okBtn, &QPushButton::clicked, this,
                        [this, rid, status]{
                    if (status == 1) {
                        api_->completeReturn(rid, [this](quint32 c, const json& x){
                            showTransient(tipLabel_, c == 2081 ? QString::fromStdString(x.value("message", "处理成功")) : errMsg(x));
                            load();
                        });
                        return;
                    }
                    bool ok = false;
                    QString comment = QInputDialog::getText(this, QStringLiteral("退货审核"), QStringLiteral("审核意见："), QLineEdit::Normal, {}, &ok);
                    if (!ok) return;
                    int amount = QInputDialog::getInt(this, QStringLiteral("退款金额"), QStringLiteral("退款金额（分）："), 0, 0, 100000000, 1, &ok);
                    if (!ok) return;
                    api_->auditReturn(rid, true, comment, amount, [this](quint32 c, const json& x){
                        showTransient(tipLabel_, c == 2080
                            ? QString::fromStdString(x.value("message", "处理成功"))
                            : errMsg(x));
                        load();
                    });
                });
                if (noBtn) connect(noBtn, &QPushButton::clicked, this, [this, rid]{
                    bool ok = false;
                    QString comment = QInputDialog::getText(this, QStringLiteral("拒绝退货"), QStringLiteral("拒绝原因："), QLineEdit::Normal, {}, &ok);
                    if (!ok || comment.trimmed().isEmpty()) return;
                    api_->auditReturn(rid, false, comment, 0, [this](quint32 c, const json& x){
                        showTransient(tipLabel_, c == 2080
                            ? QString::fromStdString(x.value("message", "处理成功"))
                            : errMsg(x));
                        load();
                    });
                });
            } else {
                table_->setItem(row, 6, new QTableWidgetItem(
                    admin ? QStringLiteral("已处理") : QStringLiteral("—")));
            }
        }
    });
}
