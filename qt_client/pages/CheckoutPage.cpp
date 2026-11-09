#include "CheckoutPage.h"
#include "../ApiClient.h"
#include "../UiUtil.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QListWidget>
#include <QComboBox>
#include <QInputDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <algorithm>

CheckoutPage::CheckoutPage(ApiClient* api, QWidget* parent)
    : QWidget(parent), api_(api) {

    auto* root = new QVBoxLayout(this);

    // 购物车预览
    table_ = new QTableWidget(0, 4);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("ID"), QStringLiteral("名称"),
         QStringLiteral("数量"), QStringLiteral("小计")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(1, 240);
    root->addWidget(table_);

    origLbl_ = new QLabel;
    QFont f = origLbl_->font(); f.setBold(true);
    origLbl_->setFont(f);
    root->addWidget(origLbl_);

    auto* addressRow = new QHBoxLayout;
    addressRow->addWidget(new QLabel(QStringLiteral("收货地址：")));
    addressBox_ = new QComboBox;
    addressRow->addWidget(addressBox_, 1);
    auto* addAddressBtn = new QPushButton(QStringLiteral("新增地址"));
    addressRow->addWidget(addAddressBtn);
    root->addLayout(addressRow);
    auto* couponRow = new QHBoxLayout;
    couponRow->addWidget(new QLabel(QStringLiteral("我的优惠券：")));
    couponBox_ = new QComboBox;
    couponBox_->addItem(QStringLiteral("不使用优惠券"), 0);
    couponRow->addWidget(couponBox_, 1);
    root->addLayout(couponRow);
    connect(addAddressBtn, &QPushButton::clicked, this, [this] {
        bool ok = false;
        QString receiver = QInputDialog::getText(this, QStringLiteral("新增地址"), QStringLiteral("收货人："), QLineEdit::Normal, {}, &ok);
        if (!ok || receiver.trimmed().isEmpty()) return;
        QString phone = QInputDialog::getText(this, QStringLiteral("新增地址"), QStringLiteral("手机号："), QLineEdit::Normal, {}, &ok);
        if (!ok || phone.trimmed().isEmpty()) return;
        QString detail = QInputDialog::getText(this, QStringLiteral("新增地址"), QStringLiteral("详细地址："), QLineEdit::Normal, {}, &ok);
        if (!ok || detail.trimmed().isEmpty()) return;
        api_->createAddress(receiver, phone, detail, addressBox_->count() == 0, [this](quint32 code, const json& j) { if (code == 2121) loadAddresses(); else resultLbl_->setText(errMsg(j)); });
    });

    root->addWidget(new QLabel(QStringLiteral("选择促销（可多选，自动去重）：")));
    promoList_ = new QListWidget;
    root->addWidget(promoList_);

    checkoutBtn_ = new QPushButton(QStringLiteral("提交结算"));
    root->addWidget(checkoutBtn_);

    resultLbl_ = new QLabel;
    resultLbl_->setWordWrap(true);
    resultLbl_->setStyleSheet("color:#15803d;");
    root->addWidget(resultLbl_);

    connect(checkoutBtn_, &QPushButton::clicked, this, &CheckoutPage::doCheckout);
}

void CheckoutPage::refresh() {
    resultLbl_->setText("");
    table_->setRowCount(0);
    for (const auto& l : api_->cart()) {
        int row = table_->rowCount();
        table_->insertRow(row);
        table_->setItem(row, 0, new QTableWidgetItem(QString::number(l.productId)));
        table_->setItem(row, 1, new QTableWidgetItem(l.name));
        table_->setItem(row, 2, new QTableWidgetItem(QString::number(l.quantity)));
        table_->setItem(row, 3, new QTableWidgetItem(
            "¥" + yuan(l.priceCents * l.quantity)));
    }
    origLbl_->setText(QStringLiteral("商品原价合计：¥%1").arg(yuan(api_->cartTotalCents())));
    loadPromotions();
}

void CheckoutPage::loadAddresses() {
    api_->fetchAddresses([this](quint32 code, const json& j) {
        if (code != 2120) return;
        addressBox_->clear();
        for (const auto& a : j.value("addresses", json::array())) {
            addressBox_->addItem(QStringLiteral("%1 %2 %3").arg(QString::fromStdString(a.value("receiver_name", ""))).arg(QString::fromStdString(a.value("phone", ""))).arg(QString::fromStdString(a.value("detail", ""))), a.value("id", 0));
        }
    });
}

void CheckoutPage::loadPromotions() {
    promoList_->clear();
    api_->fetchPromotions([this](quint32 code, const json& j) {
        if (code != 2020) return;
        for (const auto& p : j["promotions"]) {
            QString label = QStringLiteral("#%1  %2")
                .arg(p.value("id", 0))
                .arg(QString::fromStdString(p.value("name", "")));
            auto* item = new QListWidgetItem(label, promoList_);
            item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
            item->setCheckState(Qt::Unchecked);
        }
        api_->fetchAddresses([this](quint32 addressCode, const json& addresses) {
            if (addressCode != 2120) return;
            addressBox_->clear();
            for (const auto& a : addresses.value("addresses", json::array())) {
                addressBox_->addItem(QStringLiteral("%1 %2 %3")
                    .arg(QString::fromStdString(a.value("receiver_name", "")))
                    .arg(QString::fromStdString(a.value("phone", "")))
                    .arg(QString::fromStdString(a.value("detail", ""))), a.value("id", 0));
            }
            api_->fetchCoupons([this](quint32 code, const json& coupons) {
                if (code != 2140) return;
                couponBox_->clear(); couponBox_->addItem(QStringLiteral("不使用优惠券"), 0);
                for (const auto& c : coupons.value("coupons", json::array())) if (c.value("status", 1) == 0) {
                    couponBox_->addItem(QString::fromStdString(c.value("name", "优惠券")), c.value("coupon_id", 0));
                    couponBox_->setItemData(couponBox_->count() - 1, c.value("promotion_id", 0), Qt::UserRole + 1);
                }
            });
        });
    });
}

void CheckoutPage::doCheckout() {
    if (api_->cart().empty()) {
        resultLbl_->setStyleSheet("color:#dc2626;");
        resultLbl_->setText(QStringLiteral("购物车为空，无法结算"));
        return;
    }
    if (addressBox_->currentData().toInt() <= 0) {
        resultLbl_->setStyleSheet("color:#dc2626;");
        resultLbl_->setText(QStringLiteral("请先选择收货地址"));
        return;
    }
    std::vector<int> ids;
    for (int i = 0; i < promoList_->count(); ++i) {
        auto* it = promoList_->item(i);
        if (it->checkState() == Qt::Checked) {
            ids.push_back(it->text().section('#', 1, 1).section(' ', 0, 0).toInt());
        }
    }
    int couponPromotionId = couponBox_->currentData(Qt::UserRole + 1).toInt();
    if (couponPromotionId > 0 && std::find(ids.begin(), ids.end(), couponPromotionId) == ids.end()) ids.push_back(couponPromotionId);
    checkoutBtn_->setEnabled(false);
    api_->explainPromotions(ids, [this, ids](quint32 code, const json& explanation) {
        if (code != 2090) {
            checkoutBtn_->setEnabled(true);
            resultLbl_->setStyleSheet("color:#dc2626;");
            resultLbl_->setText(errMsg(explanation));
            return;
        }
        api_->checkout(ids, addressBox_->currentData().toInt(), couponBox_->currentData().toInt(), [this, explanation](quint32 checkoutCode, const json& result) {
            checkoutBtn_->setEnabled(true);
            if (checkoutCode == 2030) {
                resultLbl_->setStyleSheet("color:#15803d;");
                QString txt = QStringLiteral(
                    "订单号：%1\n商品原价：¥%2\n优惠：¥%3\n实付：¥%4\n促销明细：%5\n最优方案：¥%6")
                    .arg(QString::fromStdString(result.value("order_no", "")))
                    .arg(yuan(result.value("original", 0)))
                    .arg(yuan(result.value("discount", 0)))
                    .arg(yuan(result.value("final", 0)))
                    .arg(QString::fromStdString(result.value("promo_desc", "")))
                    .arg(yuan(explanation.value("best", json::object()).value("amount", 0)));
                resultLbl_->setText(txt);
                api_->cartClear();
            } else {
                resultLbl_->setStyleSheet("color:#dc2626;");
                resultLbl_->setText(errMsg(result));
            }
        });
    });
}
