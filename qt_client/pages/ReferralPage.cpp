#include "ReferralPage.h"
#include "../ApiClient.h"
#include "../UiUtil.h"
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QVBoxLayout>
#include <QHBoxLayout>

ReferralPage::ReferralPage(ApiClient* api, QWidget* parent) : QWidget(parent), api_(api) {
    auto* root = new QVBoxLayout(this);
    auto* top = new QHBoxLayout;
    inviteCode_ = new QLabel;
    inviter_ = new QLabel;
    summary_ = new QLabel;
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新"));
    top->addWidget(new QLabel(QStringLiteral("我的邀请码："))); top->addWidget(inviteCode_);
    top->addSpacing(20); top->addWidget(new QLabel(QStringLiteral("邀请人："))); top->addWidget(inviter_);
    top->addStretch(); top->addWidget(refreshBtn); root->addLayout(top);
    root->addWidget(summary_);
    root->addWidget(new QLabel(QStringLiteral("我的邀请用户")));
    inviteeTable_ = new QTableWidget(0, 3);
    inviteeTable_->setHorizontalHeaderLabels({QStringLiteral("用户ID"), QStringLiteral("用户名"), QStringLiteral("注册时间")});
    inviteeTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    inviteeTable_->verticalHeader()->setVisible(false); inviteeTable_->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(inviteeTable_);
    root->addWidget(new QLabel(QStringLiteral("佣金明细")));
    commissionTable_ = new QTableWidget(0, 6);
    commissionTable_->setHorizontalHeaderLabels({QStringLiteral("订单号"), QStringLiteral("被邀请用户"), QStringLiteral("佣金"), QStringLiteral("状态"), QStringLiteral("时间"), QStringLiteral("记录ID")});
    commissionTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    commissionTable_->verticalHeader()->setVisible(false); commissionTable_->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(commissionTable_);
    tip_ = new QLabel; tip_->setStyleSheet("color:#dc2626;"); root->addWidget(tip_);
    connect(refreshBtn, &QPushButton::clicked, this, &ReferralPage::refresh);
}

void ReferralPage::refresh() { load(); }

void ReferralPage::load() {
    api_->fetchReferral([this](quint32 code, const json& j) {
        if (code != 2204) { showTransient(tip_, errMsg(j)); return; }
        inviteCode_->setText(QString::fromStdString(j.value("invite_code", "")));
        auto inviter = j.value("inviter", json());
        inviter_->setText(inviter.is_object() ? QString::fromStdString(inviter.value("username", "")) : QStringLiteral("无"));
        auto s = j.value("summary", json::object());
        summary_->setText(QStringLiteral("累计佣金：¥%1    有效：¥%2    已失效：¥%3")
            .arg(yuan(s.value("total", 0))).arg(yuan(s.value("valid", 0))).arg(yuan(s.value("invalid", 0))));
        inviteeTable_->setRowCount(0);
        for (const auto& u : j.value("invitees", json::array())) {
            int r = inviteeTable_->rowCount(); inviteeTable_->insertRow(r);
            inviteeTable_->setItem(r, 0, new QTableWidgetItem(QString::number(u.value("user_id", 0))));
            inviteeTable_->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(u.value("username", ""))));
            inviteeTable_->setItem(r, 2, new QTableWidgetItem(QString::fromStdString(u.value("register_date", ""))));
        }
        commissionTable_->setRowCount(0);
        for (const auto& c : j.value("commissions", json::array())) {
            int r = commissionTable_->rowCount(); commissionTable_->insertRow(r);
            commissionTable_->setItem(r, 0, new QTableWidgetItem(QString::number(c.value("order_id", 0))));
            commissionTable_->setItem(r, 1, new QTableWidgetItem(QString::fromStdString(c.value("invitee_username", ""))));
            commissionTable_->setItem(r, 2, new QTableWidgetItem(QStringLiteral("¥") + yuan(c.value("amount", 0))));
            commissionTable_->setItem(r, 3, new QTableWidgetItem(c.value("status", 0) == 2 ? QStringLiteral("已失效") : QStringLiteral("有效")));
            commissionTable_->setItem(r, 4, new QTableWidgetItem(QString::fromStdString(c.value("created_at", ""))));
            commissionTable_->setItem(r, 5, new QTableWidgetItem(QString::number(c.value("commission_id", 0))));
        }
    });
}
