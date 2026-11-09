#include "CouponPage.h"
#include "../ApiClient.h"
#include "../UiUtil.h"
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>

CouponPage::CouponPage(ApiClient* api, QWidget* parent) : QWidget(parent), api_(api) {
    auto* root=new QVBoxLayout(this); root->addWidget(new QLabel(QStringLiteral("领券中心 / 我的券包")));
    list_=new QListWidget; root->addWidget(list_); tip_=new QLabel; root->addWidget(tip_);
}
void CouponPage::refresh() {
    list_->clear(); tip_->clear();
    api_->fetchPromotions([this](quint32 code,const json& j){
        if(code!=2020) return;
        for(const auto& p:j.value("promotions",json::array())) {
            auto* w=new QWidget; auto* row=new QHBoxLayout(w); row->setContentsMargins(4,2,4,2);
            auto* label=new QLabel(QStringLiteral("#%1 %2").arg(p.value("id",0)).arg(QString::fromStdString(p.value("name",""))));
            auto* button=new QPushButton(QStringLiteral("领取")); row->addWidget(label); row->addStretch(); row->addWidget(button);
            auto* item=new QListWidgetItem(list_); item->setSizeHint(w->sizeHint()); list_->setItemWidget(item,w);
            int id=p.value("id",0); connect(button,&QPushButton::clicked,this,[this,id,button]{button->setEnabled(false);api_->claimCoupon(id,[this](quint32 c,const json& r){tip_->setText(c==2141?QStringLiteral("领取成功"):errMsg(r));loadCoupons();});});
        }
    });
}
void CouponPage::loadCoupons() {
    api_->fetchCoupons([this](quint32 code,const json& j){
        if(code!=2140) return;
        for(const auto& c:j.value("coupons",json::array())) { if(c.value("status",1)!=0) continue; auto* item=new QListWidgetItem(QStringLiteral("已领取：%1").arg(QString::fromStdString(c.value("name",""))),list_); item->setData(Qt::UserRole,c.value("coupon_id",0)); }
    });
}
