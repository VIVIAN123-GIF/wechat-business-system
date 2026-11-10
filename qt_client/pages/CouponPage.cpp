#include "CouponPage.h"
#include "../ApiClient.h"
#include "../UiUtil.h"
#include <QListWidget>
#include <QPushButton>
#include <QLabel>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <set>

CouponPage::CouponPage(ApiClient* api, QWidget* parent) : QWidget(parent), api_(api) {
    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel(QStringLiteral("领券中心 / 我的券包")));
    list_ = new QListWidget;
    root->addWidget(list_);
    tip_ = new QLabel;
    root->addWidget(tip_);
}

void CouponPage::refresh() {
    list_->clear();
    tip_->clear();
    api_->fetchCoupons([this](quint32 couponCode, const json& coupons) {
        if (couponCode != 2140) return;
        std::set<int> claimed;
        for (const auto& c : coupons.value("coupons", json::array()))
            claimed.insert(c.value("promotion_id", 0));
        api_->fetchPromotions([this, claimed](quint32 code, const json& j) {
            if (code != 2020) return;
            for (const auto& p : j.value("promotions", json::array())) {
                const int id = p.value("id", 0);
                const bool alreadyClaimed = claimed.count(id) > 0;
                auto* w = new QWidget;
                auto* row = new QHBoxLayout(w);
                row->setContentsMargins(4, 2, 4, 2);
                row->addWidget(new QLabel(QStringLiteral("#%1 %2").arg(id).arg(QString::fromStdString(p.value("name", "")))));
                row->addStretch();
                auto* button = new QPushButton(alreadyClaimed ? QStringLiteral("已领取") : QStringLiteral("领取"));
                button->setEnabled(!alreadyClaimed);
                row->addWidget(button);
                auto* item = new QListWidgetItem(list_);
                item->setSizeHint(w->sizeHint());
                list_->setItemWidget(item, w);
                if (!alreadyClaimed) {
                    connect(button, &QPushButton::clicked, this, [this, id, button] {
                        button->setEnabled(false);
                        api_->claimCoupon(id, [this, button](quint32 c, const json& r) {
                            if (c == 2141) {
                                button->setText(QStringLiteral("已领取"));
                                showTransient(tip_, QStringLiteral("领取成功"));
                                refresh();
                            } else {
                                button->setEnabled(true);
                                showTransient(tip_, errMsg(r));
                            }
                        });
                    });
                }
            }
            loadCoupons();
        });
    });
}

void CouponPage::loadCoupons() {
    api_->fetchCoupons([this](quint32 code, const json& j) {
        if (code != 2140) return;
        auto* header = new QListWidgetItem(QStringLiteral("我的优惠券"), list_);
        header->setFlags(header->flags() & ~Qt::ItemIsSelectable);
        for (const auto& c : j.value("coupons", json::array())) {
            auto* item = new QListWidgetItem(QStringLiteral("已领取：%1").arg(QString::fromStdString(c.value("name", ""))), list_);
            item->setData(Qt::UserRole, c.value("coupon_id", 0));
            item->setFlags(item->flags() & ~Qt::ItemIsSelectable);
        }
    });
}
