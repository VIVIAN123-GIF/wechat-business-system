#include "AdminPage.h"
#include "../ApiClient.h"
#include "../UiUtil.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QComboBox>
#include <QPushButton>
#include <QSpinBox>
#include <QHBoxLayout>
#include <QVBoxLayout>
AdminPage::AdminPage(ApiClient* api, QWidget* parent) : QWidget(parent), api_(api) {
    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel(QStringLiteral("卖家管理")));
    table_ = new QTableWidget(0, 5); table_->setHorizontalHeaderLabels({"ID", "商品", "价格", "库存", "操作"});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->horizontalHeader()->setStretchLastSection(true); root->addWidget(table_);
    auto* ship = new QHBoxLayout;
    orderId_ = new QLineEdit; orderId_->setPlaceholderText(QStringLiteral("订单 ID"));
    trackingNo_ = new QLineEdit; trackingNo_->setPlaceholderText(QStringLiteral("快递单号"));
    shipmentStatus_ = new QComboBox; shipmentStatus_->addItem(QStringLiteral("已揽收"), 0); shipmentStatus_->addItem(QStringLiteral("运输中"), 1); shipmentStatus_->addItem(QStringLiteral("派送中"), 2);
    auto* save = new QPushButton(QStringLiteral("保存物流")); ship->addWidget(orderId_); ship->addWidget(trackingNo_); ship->addWidget(shipmentStatus_); ship->addWidget(save); root->addLayout(ship);
    tip_ = new QLabel; root->addWidget(tip_);
    connect(save, &QPushButton::clicked, this, [this] {
        api_->saveShipment(orderId_->text().toInt(), trackingNo_->text().trimmed(), shipmentStatus_->currentData().toInt(), [this](quint32 code, const json& result) { tip_->setText(code == 2181 ? QStringLiteral("物流已保存") : errMsg(result)); });
    });
}
void AdminPage::refresh() { loadProducts(); }
void AdminPage::loadProducts() {
    api_->fetchProducts([this](quint32 code, const json& result) {
        if (code != 2001) { tip_->setText(errMsg(result)); return; }
        table_->setRowCount(0);
        for (const auto& product : result.value("products", json::array())) {
            int row = table_->rowCount(); table_->insertRow(row); int id = product.value("id", 0);
            table_->setItem(row, 0, new QTableWidgetItem(QString::number(id))); table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(product.value("name", "")))); table_->setItem(row, 2, new QTableWidgetItem("¥" + yuan(product.value("price", 0)))); table_->setItem(row, 3, new QTableWidgetItem(QString::number(product.value("stock", 0))));
            auto* controls = new QWidget; auto* layout = new QHBoxLayout(controls); layout->setContentsMargins(0, 0, 0, 0);
            auto* stock = new QSpinBox; stock->setRange(0, 1000000); stock->setValue(product.value("stock", 0)); auto* saveStock = new QPushButton(QStringLiteral("库存")); auto* toggle = new QPushButton(product.value("status", 1) ? QStringLiteral("下架") : QStringLiteral("上架"));
            layout->addWidget(stock); layout->addWidget(saveStock); layout->addWidget(toggle); table_->setCellWidget(row, 4, controls);
            connect(saveStock, &QPushButton::clicked, this, [this, id, stock] { api_->updateProduct(id, "stock", stock->value(), [this](quint32 code, const json& result) { tip_->setText(code == 2190 ? QStringLiteral("库存已更新") : errMsg(result)); }); });
            int nextStatus = product.value("status", 1) ? 0 : 1; connect(toggle, &QPushButton::clicked, this, [this, id, nextStatus] { api_->updateProduct(id, "status", nextStatus, [this](quint32 code, const json& result) { tip_->setText(code == 2190 ? QStringLiteral("商品状态已更新") : errMsg(result)); loadProducts(); }); });
        }
    });
}
