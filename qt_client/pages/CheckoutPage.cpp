// CheckoutPage.cpp - promotion preview and checkout
#include "CheckoutPage.h"
#include "../ApiClient.h"
#include "../UiUtil.h"

#include <QCheckBox>
#include <QComboBox>
#include <QInputDialog>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QHBoxLayout>
#include <QVBoxLayout>

CheckoutPage::CheckoutPage(ApiClient* api, QWidget* parent)
    : QWidget(parent), api_(api) {
    auto* root = new QVBoxLayout(this);

    table_ = new QTableWidget(0, 5, this);
    table_->setHorizontalHeaderLabels({QStringLiteral("购买"), QStringLiteral("ID"), QStringLiteral("名称"), QStringLiteral("数量"), QStringLiteral("小计")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(0, 60);
    table_->setColumnWidth(2, 240);
    root->addWidget(table_);

    origLbl_ = new QLabel(this);
    QFont font = origLbl_->font();
    font.setBold(true);
    origLbl_->setFont(font);
    root->addWidget(origLbl_);

    recommendationLbl_ = new QLabel(this);
    recommendationLbl_->setWordWrap(true);
    recommendationLbl_->setStyleSheet("color:#166534;");
    root->addWidget(recommendationLbl_);

    auto* addressRow = new QHBoxLayout;
    addressRow->addWidget(new QLabel(QStringLiteral("收货地址："), this));
    addressBox_ = new QComboBox(this);
    addressRow->addWidget(addressBox_, 1);
    auto* addAddressBtn = new QPushButton(QStringLiteral("新增地址"), this);
    addressRow->addWidget(addAddressBtn);
    root->addLayout(addressRow);

    checkoutBtn_ = new QPushButton(QStringLiteral("提交结算"), this);
    checkoutBtn_->setEnabled(false);
    root->addWidget(checkoutBtn_);

    resultLbl_ = new QLabel(this);
    resultLbl_->setWordWrap(true);
    root->addWidget(resultLbl_);

    connect(addressBox_, &QComboBox::currentIndexChanged, this, [this](int) { updateCheckoutState(); });
    connect(checkoutBtn_, &QPushButton::clicked, this, &CheckoutPage::doCheckout);
    connect(addAddressBtn, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const QString receiver = QInputDialog::getText(this, QStringLiteral("新增地址"), QStringLiteral("收货人："), QLineEdit::Normal, {}, &ok);
        if (!ok || receiver.trimmed().isEmpty()) return;
        const QString phone = QInputDialog::getText(this, QStringLiteral("新增地址"), QStringLiteral("手机号："), QLineEdit::Normal, {}, &ok);
        if (!ok || phone.trimmed().isEmpty()) return;
        const QString detail = QInputDialog::getText(this, QStringLiteral("新增地址"), QStringLiteral("详细地址："), QLineEdit::Normal, {}, &ok);
        if (!ok || detail.trimmed().isEmpty()) return;
        api_->createAddress(receiver, phone, detail, addressBox_->count() == 0,
            [this](quint32 code, const json& result) {
                if (code == 2121) loadAddresses(requestSerial_);
                else showTransient(resultLbl_, errMsg(result));
            });
    });
}

void CheckoutPage::refresh() {
    const auto serial = ++requestSerial_;
    cartReady_ = false;
    addressReady_ = false;
    recommendationReady_ = false;
    recommendationOk_ = false;
    ++recommendationSerial_;
    checkoutBtn_->setEnabled(false);
    resultLbl_->clear();
    recommendationLbl_->setText(QStringLiteral("正在加载结算信息…"));

    api_->fetchCart([this, serial](quint32 code, const json& result) {
        if (serial != requestSerial_) return;
        if (code != 2011) {
            table_->setRowCount(0);
            origLbl_->setText(QStringLiteral("购物车加载失败"));
            recommendationLbl_->setText(errMsg(result));
            updateCheckoutState();
            return;
        }
        api_->cartReplace(result.value("items", json::array()));
        table_->setRowCount(0);
        for (const auto& line : api_->cart()) {
            const int row = table_->rowCount();
            table_->insertRow(row);
            auto* selected = new QCheckBox(table_);
            selected->setChecked(true);
            table_->setCellWidget(row, 0, selected);
            table_->setItem(row, 1, new QTableWidgetItem(QString::number(line.productId)));
            table_->setItem(row, 2, new QTableWidgetItem(line.name));
            auto* quantity = new QSpinBox(table_);
            quantity->setRange(1, line.quantity);
            quantity->setValue(line.quantity);
            table_->setCellWidget(row, 3, quantity);
            table_->setItem(row, 4, new QTableWidgetItem(QStringLiteral("¥%1").arg(yuan(line.priceCents * line.quantity))));
            connect(selected, &QCheckBox::stateChanged, this, [this, serial](int) {
                if (serial == requestSerial_) recommendPromotions(serial);
            });
            connect(quantity, QOverload<int>::of(&QSpinBox::valueChanged), this, [this, serial, row, quantity, line](int value) {
                if (auto* subtotal = table_->item(row, 4))
                    subtotal->setText(QStringLiteral("¥%1").arg(yuan(line.priceCents * value)));
                if (serial == requestSerial_) recommendPromotions(serial);
            });
        }
        cartReady_ = true;
        recommendPromotions(serial);
        updateCheckoutState();
    });
    loadAddresses(serial);
}

std::vector<CartLine> CheckoutPage::selectedLines() const {
    std::vector<CartLine> lines;
    const auto& cart = api_->cart();
    for (int row = 0; row < table_->rowCount() && row < static_cast<int>(cart.size()); ++row) {
        auto* selected = qobject_cast<QCheckBox*>(table_->cellWidget(row, 0));
        auto* quantity = qobject_cast<QSpinBox*>(table_->cellWidget(row, 3));
        if (!selected || !selected->isChecked() || !quantity) continue;
        auto line = cart[static_cast<size_t>(row)];
        line.quantity = quantity->value();
        lines.push_back(line);
    }
    return lines;
}

void CheckoutPage::loadAddresses(std::uint64_t serial) {
    api_->fetchAddresses([this, serial](quint32 code, const json& result) {
        if (serial != requestSerial_) return;
        addressBox_->clear();
        if (code != 2120) {
            addressReady_ = false;
            recommendationLbl_->setText(QStringLiteral("地址加载失败：%1").arg(errMsg(result)));
            updateCheckoutState();
            return;
        }
        for (const auto& address : result.value("addresses", json::array())) {
            addressBox_->addItem(
                QStringLiteral("%1 %2 %3")
                    .arg(QString::fromStdString(address.value("receiver_name", "")))
                    .arg(QString::fromStdString(address.value("phone", "")))
                    .arg(QString::fromStdString(address.value("detail", ""))),
                address.value("id", 0));
        }
        addressReady_ = true;
        updateCheckoutState();
    });
}

void CheckoutPage::recommendPromotions(std::uint64_t serial) {
    const auto recommendationSerial = ++recommendationSerial_;
    const auto lines = selectedLines();
    int total = 0;
    for (const auto& line : lines) total += line.priceCents * line.quantity;
    origLbl_->setText(QStringLiteral("本次选中商品原价：¥%1").arg(yuan(total)));
    recommendationReady_ = true;
    recommendationOk_ = false;
    if (lines.empty()) {
        recommendationLbl_->setText(QStringLiteral("请至少选择一件商品"));
        updateCheckoutState();
        return;
    }
    recommendationReady_ = false;
    recommendationLbl_->setText(QStringLiteral("正在计算最优促销…"));
    api_->explainPromotions(lines, {}, [this, serial, recommendationSerial](quint32 code, const json& result) {
        if (serial != requestSerial_ || recommendationSerial != recommendationSerial_) return;
        recommendationReady_ = true;
        if (code != 2090) {
            recommendationLbl_->setText(QStringLiteral("促销计算失败：%1").arg(errMsg(result)));
            recommendationOk_ = false;
            updateCheckoutState();
            return;
        }
        const auto best = result.value("best", json::object());
        const auto description = QString::fromStdString(best.value("description", "无可用促销"));
        recommendationLbl_->setText(QStringLiteral("最优促销：%1\n优惠：¥%2\n预计实付：¥%3")
            .arg(description)
            .arg(yuan(best.value("discount", 0)))
            .arg(yuan(best.value("amount", 0))));
        recommendationOk_ = true;
        updateCheckoutState();
    });
}

void CheckoutPage::updateCheckoutState() {
    checkoutBtn_->setEnabled(cartReady_ && addressReady_ && recommendationReady_ && recommendationOk_ && !selectedLines().empty());
}

void CheckoutPage::doCheckout() {
    const auto lines = selectedLines();
    if (lines.empty()) {
        showTransient(resultLbl_, QStringLiteral("请至少选择一件商品"));
        return;
    }
    const int addressId = addressBox_->currentData().toInt();
    if (addressId <= 0) {
        showTransient(resultLbl_, QStringLiteral("请先选择收货地址"));
        return;
    }
    checkoutBtn_->setEnabled(false);
    const auto serial = requestSerial_;
    api_->explainPromotions(lines, {}, [this, lines, addressId, serial](quint32 code, const json& explanation) {
        if (serial != requestSerial_) return;
        if (code != 2090) {
            checkoutBtn_->setEnabled(true);
            showTransient(resultLbl_, QStringLiteral("促销计算失败：%1").arg(errMsg(explanation)));
            return;
        }
        std::vector<int> promoIds;
        const auto best = explanation.value("best", json::object());
        for (const auto& id : best.value("promo_ids", json::array()))
            if (id.is_number_integer()) promoIds.push_back(id.get<int>());
        api_->checkout(lines, promoIds, addressId, {}, [this, explanation, lines](quint32 checkoutCode, const json& result) {
            if (checkoutCode != 2030) {
                checkoutBtn_->setEnabled(true);
                showTransient(resultLbl_, errMsg(result));
                updateCheckoutState();
                return;
            }
            const QString text = QStringLiteral("订单号：%1\n商品原价：¥%2\n优惠：¥%3\n实付：¥%4\n促销明细：%5\n最优方案：¥%6")
                .arg(QString::fromStdString(result.value("order_no", "")))
                .arg(yuan(result.value("original", 0)))
                .arg(yuan(result.value("discount", 0)))
                .arg(yuan(result.value("final", 0)))
                .arg(QString::fromStdString(result.value("promo_desc", "无")))
                .arg(yuan(explanation.value("best", json::object()).value("amount", 0)));
            refresh();
            showTransient(resultLbl_, text);
        });
    });
}
