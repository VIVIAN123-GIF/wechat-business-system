#include "ServicePage.h"
#include "../ApiClient.h"
#include "../UiUtil.h"
#include <QTextEdit>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>

ServicePage::ServicePage(ApiClient* api, QWidget* parent)
    : QWidget(parent), api_(api) {

    auto* root = new QVBoxLayout(this);

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
            chatView_->append(QStringLiteral(
                "<p style='color:#dc2626;'>%1</p>").arg(errMsg(j)));
        }
    });
}
