#include "OrderPage.h"
#include "../ApiClient.h"
#include "../UiUtil.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QInputDialog>
#include <QVBoxLayout>

OrderPage::OrderPage(ApiClient* api, QWidget* parent)
    : QWidget(parent), api_(api) {

    auto* root = new QVBoxLayout(this);

    auto* top = new QHBoxLayout;
    top->addWidget(new QLabel(QStringLiteral("我的订单")));
    top->addStretch();
    auto* refreshBtn = new QPushButton(QStringLiteral("刷新"));
    top->addWidget(refreshBtn);
    root->addLayout(top);

    orderTable_ = new QTableWidget(0, 7);
    orderTable_->setHorizontalHeaderLabels(
        {QStringLiteral("订单号"), QStringLiteral("原价"),
         QStringLiteral("优惠"), QStringLiteral("实付"),
         QStringLiteral("状态"), QStringLiteral("退货"),
         QStringLiteral("操作")});
    orderTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    orderTable_->verticalHeader()->setVisible(false);
    orderTable_->horizontalHeader()->setStretchLastSection(true);
    orderTable_->setColumnWidth(0, 200);
    root->addWidget(orderTable_);

    root->addWidget(new QLabel(QStringLiteral("订单明细（选中订单）：")));
    itemTable_ = new QTableWidget(0, 4);
    itemTable_->setHorizontalHeaderLabels(
        {QStringLiteral("商品ID"), QStringLiteral("商品名称"),
         QStringLiteral("单价"), QStringLiteral("数量")});
    itemTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    itemTable_->verticalHeader()->setVisible(false);
    itemTable_->horizontalHeader()->setStretchLastSection(true);
    root->addWidget(itemTable_);

    tipLabel_ = new QLabel;
    tipLabel_->setStyleSheet("color:#dc2626;");
    root->addWidget(tipLabel_);

    connect(refreshBtn, &QPushButton::clicked, this, &OrderPage::refresh);
    connect(orderTable_, &QTableWidget::itemSelectionChanged, this, [this]() {
        auto sel = orderTable_->selectedItems();
        if (sel.isEmpty()) return;
        int row = orderTable_->currentRow();
        // 从隐藏列读订单明细较麻烦，这里用订单号匹配重新取
        // 简化：把明细数据存在 itemData 里
        QVariant data = orderTable_->item(row, 0)->data(Qt::UserRole);
        json items = json::parse(data.toString().toStdString());
        itemTable_->setRowCount(0);
        for (const auto& it : items) {
            int r = itemTable_->rowCount();
            itemTable_->insertRow(r);
            itemTable_->setItem(r, 0, new QTableWidgetItem(
                QString::number(it.value("product_id", 0))));
            itemTable_->setItem(r, 1, new QTableWidgetItem(
                QString::fromStdString(it.value("product_name", ""))));
            itemTable_->setItem(r, 2, new QTableWidgetItem(
                "¥" + yuan(it.value("price", 0))));
            itemTable_->setItem(r, 3, new QTableWidgetItem(
                QString::number(it.value("quantity", 0))));
        }
    });
}

void OrderPage::refresh() {
    tipLabel_->setText("");
    loadOrders();
}

void OrderPage::loadOrders() {
    api_->fetchOrders([this](quint32 code, const json& j) {
        orderTable_->setRowCount(0);
        itemTable_->setRowCount(0);
        if (code != 2040) return;

        static const QString retText[] = {
            QStringLiteral("无"), QStringLiteral("待审核"),
            QStringLiteral("已通过"), QStringLiteral("已拒绝")};

        for (const auto& o : j["orders"]) {
            int row = orderTable_->rowCount();
            orderTable_->insertRow(row);

            int status = o.value("status", 0);
            int ret    = o.value("return_status", 0);

            auto* noItem = new QTableWidgetItem(
                QString::fromStdString(o.value("order_no", "")));
            // 明细 JSON 存到 UserRole，供选中时展示
            json items = o.value("items", json::array());
            noItem->setData(Qt::UserRole, QString::fromStdString(items.dump()));
            orderTable_->setItem(row, 0, noItem);
            orderTable_->setItem(row, 1, new QTableWidgetItem(
                "¥" + yuan(o.value("original", 0))));
            orderTable_->setItem(row, 2, new QTableWidgetItem(
                "¥" + yuan(o.value("discount", 0))));
            orderTable_->setItem(row, 3, new QTableWidgetItem(
                "¥" + yuan(o.value("final", 0))));
            orderTable_->setItem(row, 4, new QTableWidgetItem(orderStatusText(status)));
            orderTable_->setItem(row, 5, new QTableWidgetItem(
                ret >= 0 && ret <= 3 ? retText[ret] : QStringLiteral("无")));

            // 可退货：状态 1/2/3 且无退货
            auto* retBtn = new QPushButton(QStringLiteral("申请退货"));
            bool canReturn = (status == 1 || status == 2 || status == 3) && ret == 0;
            retBtn->setEnabled(canReturn);

            int orderId = o.value("order_id", 0);
            auto* actionBtn = new QPushButton;
            if (status == 0) actionBtn->setText(QStringLiteral("支付"));
            else if (status == 2) actionBtn->setText(QStringLiteral("确认收货"));
            else if (status == 3) actionBtn->setText(QStringLiteral("评价"));
            else actionBtn->setText(QStringLiteral("无操作"));
            auto* actionCell = new QWidget;
            auto* actionLayout = new QHBoxLayout(actionCell);
            actionLayout->setContentsMargins(0, 0, 0, 0);
            actionLayout->addWidget(retBtn);
            actionLayout->addWidget(actionBtn);
            QPushButton* cancelBtn = nullptr;
            if (status == 0) {
                cancelBtn = new QPushButton(QStringLiteral("取消订单"));
                actionLayout->addWidget(cancelBtn);
            }
            orderTable_->setCellWidget(row, 6, actionCell);
            connect(actionBtn, &QPushButton::clicked, this, [this, orderId, status, items]{
                if (status == 0) {
                    api_->payOrder(orderId, [this](quint32 code, const json& j) { tipLabel_->setText(code == 2100 ? QStringLiteral("支付成功") : errMsg(j)); loadOrders(); });
                } else if (status == 2) {
                    api_->confirmReceipt(orderId, [this](quint32 code, const json& j) { tipLabel_->setText(code == 2110 ? QStringLiteral("确认收货成功") : errMsg(j)); loadOrders(); });
                } else if (status == 3 && !items.empty()) {
                    bool ok = false;
                    int rating = QInputDialog::getInt(this, QStringLiteral("商品评价"), QStringLiteral("评分（1-5）："), 5, 1, 5, 1, &ok);
                    if (!ok) return;
                    QString content = QInputDialog::getText(this, QStringLiteral("商品评价"), QStringLiteral("评价内容："), QLineEdit::Normal, {}, &ok);
                    if (!ok) return;
                    int productId = items.front().value("product_id", 0);
                    api_->createReview(orderId, productId, rating, content, [this](quint32 code, const json& j) { tipLabel_->setText(code == 2130 ? QStringLiteral("评价成功") : errMsg(j)); });
                }
            });
            connect(retBtn, &QPushButton::clicked, this, [this, orderId]{
                applyReturn(orderId);
            });
            if (cancelBtn) {
                connect(cancelBtn, &QPushButton::clicked, this, [this, orderId] {
                    api_->cancelOrder(orderId, [this](quint32 code, const json& j) {
                        tipLabel_->setText(code == 2101 ? QStringLiteral("订单已取消") : errMsg(j));
                        loadOrders();
                    });
                });
            }
        }
    });
}

void OrderPage::applyReturn(int orderId) {
    bool ok = false;
    QString reason = QInputDialog::getText(
        this, QStringLiteral("申请退货"),
        QStringLiteral("退货原因："), QLineEdit::Normal,
        QStringLiteral("不想要了"), &ok);
    if (!ok) return;
    api_->applyReturn(orderId, reason, [this](quint32 code, const json& j) {
        if (code == 2050) {
            tipLabel_->setStyleSheet("color:#15803d;");
            tipLabel_->setText(QStringLiteral("退货申请已提交，等待审核"));
            loadOrders();
        } else {
            tipLabel_->setStyleSheet("color:#dc2626;");
            tipLabel_->setText(errMsg(j));
        }
    });
}
