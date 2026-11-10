// ============================================================
// ServicePage.cpp - 联系客服
// ============================================================
#include "ServicePage.h"
#include "../ApiClient.h"
#include "../UiUtil.h"
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>

ServicePage::ServicePage(ApiClient* api, QWidget* parent)
    : QWidget(parent), api_(api) {

    auto* root = new QVBoxLayout(this);

    tip_ = new QLabel;
    tip_->setStyleSheet("color:#dc2626;");
    root->addWidget(tip_);

    chatView_ = new QTextEdit;
    chatView_->setReadOnly(true);
    chatView_->setHtml(QStringLiteral(
        "<p><b>客服小助手：</b>您好，这里是微商系统在线客服，"
        "有任何问题请在下方输入，我们会尽快回复。</p>"));
    root->addWidget(chatView_);

    auto* row = new QHBoxLayout;
    input_ = new QLineEdit;
    input_->setMaxLength(500);
    input_->setPlaceholderText(QStringLiteral("输入消息（500字以内），回车发送"));
    auto* sendBtn = new QPushButton(QStringLiteral("发送"));
    row->addWidget(input_);
    row->addWidget(sendBtn);
    root->addLayout(row);

    auto* historyBtn = new QPushButton(QStringLiteral("历史工单"));
    root->addWidget(historyBtn);
    connect(historyBtn, &QPushButton::clicked, this, [this] {
        api_->fetchServiceTickets([this](quint32 code, const json& j) {
            if (code != 2202) { showTransient(tip_, errMsg(j)); return; }
            chatView_->append(QStringLiteral("<hr><b>历史工单</b>"));
            for (const auto& t : j.value("tickets", json::array())) {
                chatView_->append(QStringLiteral("<p>工单 #%1　%2　状态：%3　最后更新：%4</p>")
                    .arg(t.value("ticket_id", 0))
                    .arg(QString::fromStdString(t.value("subject", "在线咨询")))
                    .arg(QString::fromStdString(t.value("status", "open")))
                    .arg(QString::fromStdString(t.value("last_message_at", ""))));
            }
        });
    });

    connect(sendBtn, &QPushButton::clicked, this, &ServicePage::send);
    connect(input_, &QLineEdit::returnPressed, this, &ServicePage::send);
}

void ServicePage::send() {
    QString msg = input_->text().trimmed();
    if (msg.isEmpty()) return;
    input_->clear();

    chatView_->append(QStringLiteral("<p><b>我：</b>%1</p>").arg(msg.toHtmlEscaped()));

    api_->sendService(msg, [this](quint32 code, const json& j) {
        if (code == 2065) {
            QString received = QString::fromStdString(j.value("received", ""));
            QString reply = QString::fromStdString(j.value("reply", ""));
            chatView_->append(QStringLiteral(
                "<p style='color:#64748b;'>（客服已收到：%1）</p>")
                .arg(received.toHtmlEscaped()));
            chatView_->append(QStringLiteral(
                "<p><b>客服小助手：</b>%1</p>").arg(reply.toHtmlEscaped()));
        } else {
            showTransient(tip_, errMsg(j));
        }
    });
}
