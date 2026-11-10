// ============================================================
// NotificationPage.cpp - 消息通知列表
// ============================================================
#include "NotificationPage.h"
#include "../ApiClient.h"
#include "../UiUtil.h"
#include <QListWidget>
#include <QLabel>
#include <QVBoxLayout>

NotificationPage::NotificationPage(ApiClient* api,QWidget* parent):QWidget(parent),api_(api){auto* root=new QVBoxLayout(this);root->addWidget(new QLabel(QStringLiteral("消息通知中心")));list_=new QListWidget;root->addWidget(list_);tip_=new QLabel;root->addWidget(tip_);connect(list_,&QListWidget::itemDoubleClicked,this,[this](QListWidgetItem* item){api_->markNotificationRead(item->data(Qt::UserRole).toInt(),[this](quint32,const json&){refresh();});});}
void NotificationPage::refresh(){list_->clear();api_->fetchNotifications([this](quint32 code,const json& j){if(code!=2150)return;showTransient(tip_, QStringLiteral("未读消息：%1").arg(j.value("unread_count",0)));for(const auto& n:j.value("notifications",json::array())){auto* item=new QListWidgetItem(QStringLiteral("%1  %2").arg(QString::fromStdString(n.value("created_at",""))).arg(QString::fromStdString(n.value("content",""))),list_);item->setData(Qt::UserRole,n.value("notification_id",0));if(n.value("is_read",false))item->setForeground(Qt::gray);} });}
