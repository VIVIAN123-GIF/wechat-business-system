// ============================================================
// AdminPage.cpp - 卖家后台（商品上下架 / 发货录入）
// ============================================================
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
#include <QListWidget>
#include <QInputDialog>
#include <QMessageBox>
#include <functional>
#include <memory>
AdminPage::AdminPage(ApiClient* api, QWidget* parent) : QWidget(parent), api_(api) {
    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel(QStringLiteral("卖家管理")));
    stockFilter_ = new QComboBox;
    stockFilter_->addItem(QStringLiteral("全部库存"), 0);
    stockFilter_->addItem(QStringLiteral("库存预警（≤5）"), 1);
    root->addWidget(stockFilter_);
    connect(stockFilter_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) { loadProducts(); });
    todo_ = new QLabel;
    todo_->setStyleSheet("background:#fff7ed;border:1px solid #fed7aa;padding:8px;");
    root->addWidget(todo_);
    table_ = new QTableWidget(0, 5); table_->setHorizontalHeaderLabels({"ID", "商品", "价格", "库存", "操作"});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers); table_->horizontalHeader()->setStretchLastSection(true); root->addWidget(table_);
    // 订单管理：展示全部用户订单，支持手动刷新与定时自动刷新
    auto* orderHead = new QHBoxLayout;
    auto* orderTitleLbl = new QLabel(QStringLiteral("订单管理（全部用户）"));
    auto* orderRefresh = new QPushButton(QStringLiteral("刷新订单"));
    orderHead->addWidget(orderTitleLbl);
    orderHead->addStretch();
    orderHead->addWidget(orderRefresh);
    root->addLayout(orderHead);
    orderTable_ = new QTableWidget(0, 6);
    orderTable_->setHorizontalHeaderItem(5, new QTableWidgetItem(QStringLiteral("操作")));
    orderTable_->setHorizontalHeaderLabels(
        {QStringLiteral("订单号"), QStringLiteral("用户"),
         QStringLiteral("实付"), QStringLiteral("状态"),
         QStringLiteral("下单时间")});
    orderTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    orderTable_->verticalHeader()->setVisible(false);
    orderTable_->horizontalHeader()->setStretchLastSection(true);
    orderTable_->setColumnWidth(0, 200);
    root->addWidget(orderTable_);
    // 每 5 秒自动拉取一次，用户支付后管理员无需手动刷新即可看到状态变化
    orderTimer_ = new QTimer(this);
    orderTimer_->setInterval(5000);
    connect(orderTimer_, &QTimer::timeout, this, [this] { if (isVisible()) loadOrders(); });
    orderTimer_->start();
    connect(orderRefresh, &QPushButton::clicked, this, &AdminPage::loadOrders);

    auto* ship = new QHBoxLayout;
    orderId_ = new QLineEdit; orderId_->setPlaceholderText(QStringLiteral("订单 ID"));
    trackingNo_ = new QLineEdit; trackingNo_->setPlaceholderText(QStringLiteral("快递单号"));
    shipmentStatus_ = new QComboBox; shipmentStatus_->addItem(QStringLiteral("已揽收"), 0); shipmentStatus_->addItem(QStringLiteral("运输中"), 1); shipmentStatus_->addItem(QStringLiteral("派送中"), 2);
    auto* query = new QPushButton(QStringLiteral("查询物流"));
    auto* save = new QPushButton(QStringLiteral("保存物流")); ship->addWidget(orderId_); ship->addWidget(trackingNo_); ship->addWidget(shipmentStatus_); ship->addWidget(query); ship->addWidget(save); root->addLayout(ship);
    auto* orderFlow = new QHBoxLayout;
    auto* flowOrderId = new QLineEdit; flowOrderId->setPlaceholderText(QStringLiteral("订单 ID"));
    auto* flowStatus = new QComboBox; flowStatus->addItem(QStringLiteral("已发货"), 2); flowStatus->addItem(QStringLiteral("已完成"), 3);
    auto* flowBtn = new QPushButton(QStringLiteral("更新订单状态"));
    orderFlow->addWidget(flowOrderId); orderFlow->addWidget(flowStatus); orderFlow->addWidget(flowBtn); root->addLayout(orderFlow);
    connect(flowBtn, &QPushButton::clicked, this, [this, flowOrderId, flowStatus] {
        const int id = flowOrderId->text().toInt();
        if (id <= 0) { showTransient(tip_, QStringLiteral("请输入订单 ID")); return; }
        api_->updateOrderStatus(id, flowStatus->currentData().toInt(), [this](quint32 code, const json& result) {
            showTransient(tip_, code == 2095 ? QStringLiteral("订单状态已更新") : errMsg(result));
        });
    });
    auto* skuCreateBtn = new QPushButton(QStringLiteral("新增 SKU"));
    root->addWidget(skuCreateBtn);
    connect(skuCreateBtn, &QPushButton::clicked, this, [this] {
        bool ok = false;
        const int productId = QInputDialog::getInt(this, QStringLiteral("新增 SKU"), QStringLiteral("商品 ID"), 0, 1, 100000000, 1, &ok);
        if (!ok) return;
        const QString code = QInputDialog::getText(this, QStringLiteral("新增 SKU"), QStringLiteral("SKU 编码"), QLineEdit::Normal, {}, &ok);
        if (!ok || code.trimmed().isEmpty()) return;
        const QString color = QInputDialog::getText(this, QStringLiteral("新增 SKU"), QStringLiteral("颜色"), QLineEdit::Normal, {}, &ok);
        if (!ok) return;
        const QString size = QInputDialog::getText(this, QStringLiteral("新增 SKU"), QStringLiteral("尺码"), QLineEdit::Normal, {}, &ok);
        if (!ok) return;
        const int delta = QInputDialog::getInt(this, QStringLiteral("新增 SKU"), QStringLiteral("价格增量（分）"), 0, -100000, 100000, 1, &ok);
        if (!ok) return;
        const int stock = QInputDialog::getInt(this, QStringLiteral("新增 SKU"), QStringLiteral("库存"), 0, 0, 100000000, 1, &ok);
        if (!ok) return;
        api_->createSku(productId, code, color, size, delta, stock, [this](quint32 response, const json& result) {
            showTransient(tip_, response == 2190 ? QStringLiteral("SKU 已创建") : errMsg(result));
            if (response == 2190) loadProducts();
        });
    });
    tip_ = new QLabel; root->addWidget(tip_);
    auto* promoTitle = new QLabel(QStringLiteral("促销规则管理（有效期 / 互斥组 / 使用上限）"));
    auto* promoList = new QListWidget;
    auto* promoRefresh = new QPushButton(QStringLiteral("刷新促销规则"));
    auto* promoCreate = new QPushButton(QStringLiteral("新增促销"));
    auto* promoEdit = new QPushButton(QStringLiteral("编辑选中"));
    auto* promoToggle = new QPushButton(QStringLiteral("启用/停用"));
    auto* promoActions = new QHBoxLayout;
    promoActions->addWidget(promoRefresh); promoActions->addWidget(promoCreate); promoActions->addWidget(promoEdit); promoActions->addWidget(promoToggle); promoActions->addStretch();
    root->addWidget(promoTitle); root->addWidget(promoList); root->addLayout(promoActions);

    auto loadPromotions = std::make_shared<std::function<void()>>();
    auto editPromotion = [this, promoList, loadPromotions](json source, bool creating) {
        bool ok = false;
        QString name = QInputDialog::getText(this, creating ? QStringLiteral("新增促销") : QStringLiteral("编辑促销"), QStringLiteral("促销名称："), QLineEdit::Normal, QString::fromStdString(source.value("name", "")), &ok);
        if (!ok) return;
        int type = QInputDialog::getInt(this, QStringLiteral("促销类型"), QStringLiteral("类型（1折扣 / 2直降 / 3满减 / 4阶梯）："), source.value("type", 1), 1, 4, 1, &ok);
        if (!ok) return;
        json request = {{"action", creating ? "create" : "update"}, {"name", name.toStdString()}, {"type", type}};
        if (!creating) request["id"] = source.value("id", 0);
        for (int i = 1; i <= 6; ++i) {
            std::string key = "param" + std::to_string(i);
            double value = QInputDialog::getDouble(this, QStringLiteral("促销参数"), QStringLiteral("%1：").arg(QString::fromStdString(key)), source.value(key, 0.0), -100000000.0, 100000000.0, 2, &ok);
            if (!ok) return;
            request[key] = value;
        }
        QString start = QInputDialog::getText(this, QStringLiteral("有效期"), QStringLiteral("开始时间（YYYY-MM-DD HH:MM:SS，可空）："), QLineEdit::Normal, QString::fromStdString(source.value("start_time", "")), &ok);
        if (!ok) return;
        QString end = QInputDialog::getText(this, QStringLiteral("有效期"), QStringLiteral("结束时间（YYYY-MM-DD HH:MM:SS，可空）："), QLineEdit::Normal, QString::fromStdString(source.value("end_time", "")), &ok);
        if (!ok) return;
        QString group = QInputDialog::getText(this, QStringLiteral("促销互斥"), QStringLiteral("互斥组（同组促销不可叠加，可空）："), QLineEdit::Normal, QString::fromStdString(source.value("exclusive_group", "")), &ok);
        if (!ok) return;
        int maxUses = QInputDialog::getInt(this, QStringLiteral("使用上限"), QStringLiteral("总使用次数（0 表示不限）："), source.value("max_uses", 0), 0, 100000000, 1, &ok);
        if (!ok) return;
        int perUser = QInputDialog::getInt(this, QStringLiteral("使用上限"), QStringLiteral("每人使用次数（0 表示不限）："), source.value("per_user_limit", 0), 0, 100000000, 1, &ok);
        if (!ok) return;
        request["start_time"] = start.toStdString(); request["end_time"] = end.toStdString(); request["exclusive_group"] = group.toStdString(); request["max_uses"] = maxUses; request["per_user_limit"] = perUser; request["status"] = source.value("status", 1);
        api_->adminPromotions(request, [this, loadPromotions](quint32 code, const json& result) { showTransient(tip_, code == 2210 ? QStringLiteral("促销规则已保存") : errMsg(result)); if (code == 2210) (*loadPromotions)(); });
    };
    *loadPromotions = [this, promoList] {
        api_->adminPromotions({{"action", "list"}}, [this, promoList](quint32 code, const json& j) {
            if (code != 2210) { showTransient(tip_, errMsg(j)); return; }
            promoList->clear();
            for (const auto& p : j.value("promotions", json::array())) {
                auto* item = new QListWidgetItem(QStringLiteral("#%1 %2　类型:%3　状态:%4　有效期 %5 ~ %6　互斥组:%7　总上限:%8　个人上限:%9")
                    .arg(p.value("id",0)).arg(QString::fromStdString(p.value("name","")))
                    .arg(p.value("type",1)).arg(p.value("status",0) ? QStringLiteral("启用") : QStringLiteral("停用"))
                    .arg(QString::fromStdString(p.value("start_time",""))).arg(QString::fromStdString(p.value("end_time","")))
                    .arg(QString::fromStdString(p.value("exclusive_group",""))).arg(p.value("max_uses",0)).arg(p.value("per_user_limit",0)), promoList);
                item->setData(Qt::UserRole, p.value("id",0));
                item->setData(Qt::UserRole + 1, QString::fromStdString(p.dump()));
            }
        });
    };
    connect(promoRefresh, &QPushButton::clicked, this, [loadPromotions] { (*loadPromotions)(); });
    connect(promoCreate, &QPushButton::clicked, this, [editPromotion] { editPromotion(json::object(), true); });
    connect(promoEdit, &QPushButton::clicked, this, [this, promoList, editPromotion] {
        auto* item = promoList->currentItem(); if (!item) { showTransient(tip_, QStringLiteral("请先选择促销规则")); return; }
        try { editPromotion(json::parse(item->data(Qt::UserRole + 1).toString().toStdString()), false); } catch (...) { showTransient(tip_, QStringLiteral("促销规则数据无效")); }
    });
    connect(promoToggle, &QPushButton::clicked, this, [this, promoList, loadPromotions] {
        auto* item = promoList->currentItem(); if (!item) { showTransient(tip_, QStringLiteral("请先选择促销规则")); return; }
        json p; try { p = json::parse(item->data(Qt::UserRole + 1).toString().toStdString()); } catch (...) { showTransient(tip_, QStringLiteral("促销规则数据无效")); return; }
        api_->adminPromotions({{"action", "toggle"}, {"id", p.value("id", 0)}, {"status", p.value("status", 0) ? 0 : 1}}, [this, loadPromotions](quint32 code, const json& result) { showTransient(tip_, code == 2210 ? QStringLiteral("促销状态已更新") : errMsg(result)); if (code == 2210) (*loadPromotions)(); });
    });
    (*loadPromotions)();
    auto* ticketTitle = new QLabel(QStringLiteral("客服工单工作台"));
    auto* ticketList = new QListWidget;
    auto* ticketRefresh = new QPushButton(QStringLiteral("刷新客服工单"));
    auto* ticketActions = new QHBoxLayout;
    auto* replyEdit = new QLineEdit; replyEdit->setPlaceholderText(QStringLiteral("回复当前工单"));
    auto* replyBtn = new QPushButton(QStringLiteral("回复"));
    auto* ticketStatus = new QComboBox;
    ticketStatus->addItem(QStringLiteral("处理中"), "pending");
    ticketStatus->addItem(QStringLiteral("已解决"), "resolved");
    ticketStatus->addItem(QStringLiteral("已关闭"), "closed");
    auto* ticketStatusBtn = new QPushButton(QStringLiteral("更新工单状态"));
    ticketActions->addWidget(replyEdit, 1); ticketActions->addWidget(replyBtn);
    ticketActions->addWidget(ticketStatus); ticketActions->addWidget(ticketStatusBtn);
    root->addWidget(ticketTitle); root->addWidget(ticketList); root->addWidget(ticketRefresh); root->addLayout(ticketActions);
    auto selectedTicketUser = std::make_shared<int>(0);
    connect(ticketRefresh, &QPushButton::clicked, this, [this, ticketList] {
        api_->fetchServiceTickets([this, ticketList](quint32 code, const json& j) {
            if (code != 2202) { showTransient(tip_, errMsg(j)); return; }
            ticketList->clear();
            for (const auto& t : j.value("tickets", json::array())) {
                auto* item = new QListWidgetItem(QStringLiteral("#%1 %2　用户:%3　状态:%4　消息:%5")
                    .arg(t.value("ticket_id",0)).arg(QString::fromStdString(t.value("subject","")))
                    .arg(QString::fromStdString(t.value("username",""))).arg(QString::fromStdString(t.value("status",""))).arg(t.value("message_count",0)), ticketList);
                item->setData(Qt::UserRole, t.value("ticket_id",0));
                item->setData(Qt::UserRole + 2, t.value("user_id",0));
                item->setData(Qt::UserRole + 1, QString::fromStdString(t.value("messages", json::array()).dump()));
            }
        });
    });
    connect(ticketList, &QListWidget::itemClicked, this, [this, selectedTicketUser](QListWidgetItem* item) {
        *selectedTicketUser = item->data(Qt::UserRole + 2).toInt();
        json messages;
        try { messages = json::parse(item->data(Qt::UserRole + 1).toString().toStdString()); } catch (...) { return; }
        QString text;
        for (const auto& message : messages) {
            text += QStringLiteral("[%1] %2\n")
                .arg(message.value("direction", 0) == 0 ? QStringLiteral("客户") : QStringLiteral("客服"))
                .arg(QString::fromStdString(message.value("content", "")));
        }
        QMessageBox::information(this, QStringLiteral("客服工单 #%1").arg(item->data(Qt::UserRole).toInt()), text.isEmpty() ? QStringLiteral("暂无消息") : text);
    });
    connect(replyBtn, &QPushButton::clicked, this, [this, replyEdit, selectedTicketUser, ticketRefresh] {
        const QString content = replyEdit->text().trimmed();
        if (*selectedTicketUser <= 0 || content.isEmpty()) { showTransient(tip_, QStringLiteral("请先选择工单并输入回复")); return; }
        api_->replyService(*selectedTicketUser, content, [this, replyEdit, ticketRefresh](quint32 code, const json& result) {
            showTransient(tip_, code == 2201 ? QStringLiteral("回复已发送") : errMsg(result));
            if (code == 2201) { replyEdit->clear(); ticketRefresh->click(); }
        });
    });
    connect(ticketStatusBtn, &QPushButton::clicked, this, [this, ticketStatus, selectedTicketUser, ticketList, ticketRefresh] {
        auto* item = ticketList->currentItem();
        if (*selectedTicketUser <= 0 || !item) { showTransient(tip_, QStringLiteral("请先选择工单")); return; }
        api_->updateServiceTicket(item->data(Qt::UserRole).toInt(), ticketStatus->currentData().toString(), [this, ticketRefresh](quint32 code, const json& result) {
            showTransient(tip_, code == 2203 ? QStringLiteral("工单状态已更新") : errMsg(result));
            if (code == 2203) ticketRefresh->click();
        });
    });
    ticketRefresh->click();
    connect(query, &QPushButton::clicked, this, [this] {
        const int orderId = orderId_->text().toInt();
        if (orderId <= 0) { showTransient(tip_, QStringLiteral("请输入订单 ID")); return; }
        api_->fetchShipment(orderId, [this](quint32 code, const json& result) {
            if (code != 2180) { showTransient(tip_, errMsg(result)); return; }
            const auto tracking = QString::fromStdString(result.value("tracking_no", ""));
            if (tracking.isEmpty()) { showTransient(tip_, QStringLiteral("该订单暂无物流数据")); return; }
            trackingNo_->setText(tracking);
            shipmentStatus_->setCurrentIndex(shipmentStatus_->findData(result.value("status", -1)));
            showTransient(tip_, QStringLiteral("物流查询成功"));
        });
    });
    connect(save, &QPushButton::clicked, this, [this] {
        api_->saveShipment(orderId_->text().toInt(), trackingNo_->text().trimmed(), shipmentStatus_->currentData().toInt(), [this](quint32 code, const json& result) { showTransient(tip_, code == 2181 ? QStringLiteral("物流已保存") : errMsg(result)); });
    });
}
void AdminPage::refresh() {
    loadProducts();
    loadOrders();
    api_->fetchReturns([this](quint32 code, const json& j) {
        if (code != 2070) return;
        int pending = 0;
        for (const auto& item : j.value("returns", json::array())) if (item.value("status", 0) == 0) ++pending;
        todo_->setText(QStringLiteral("待办：待审核退货 %1 条。请优先处理售后申请，并核对退款金额。\n库存预警请在商品列表中查看库存并及时补货。").arg(pending));
    });
}
void AdminPage::loadProducts() {
    api_->fetchAdminProducts([this](quint32 code, const json& result) {
        if (code != 2191) { showTransient(tip_, errMsg(result)); return; }
        table_->setRowCount(0);
        for (const auto& product : result.value("products", json::array())) {
            int row = table_->rowCount(); table_->insertRow(row); int id = product.value("id", 0);
            table_->setItem(row, 0, new QTableWidgetItem(QString::number(id))); table_->setItem(row, 1, new QTableWidgetItem(QString::fromStdString(product.value("name", "")))); table_->setItem(row, 2, new QTableWidgetItem("¥" + yuan(product.value("price", 0)))); table_->setItem(row, 3, new QTableWidgetItem(QString::number(product.value("stock", 0))));
            table_->setRowHidden(row, stockFilter_ && stockFilter_->currentData().toInt() == 1 && product.value("stock", 0) > 5);
            auto* controls = new QWidget; auto* layout = new QHBoxLayout(controls); layout->setContentsMargins(0, 0, 0, 0);
            auto* stock = new QSpinBox; stock->setRange(0, 1000000); stock->setValue(product.value("stock", 0)); auto* saveStock = new QPushButton(QStringLiteral("库存")); auto* toggle = new QPushButton(product.value("status", 1) ? QStringLiteral("下架") : QStringLiteral("上架"));
            layout->addWidget(stock); layout->addWidget(saveStock); layout->addWidget(toggle); table_->setCellWidget(row, 4, controls);
            connect(saveStock, &QPushButton::clicked, this, [this, id, stock] { api_->updateProduct(id, "stock", stock->value(), [this](quint32 code, const json& result) { showTransient(tip_, code == 2190 ? QStringLiteral("库存已更新") : errMsg(result)); }); });
            int nextStatus = product.value("status", 1) ? 0 : 1; connect(toggle, &QPushButton::clicked, this, [this, id, nextStatus] { api_->updateProduct(id, "status", nextStatus, [this](quint32 code, const json& result) { showTransient(tip_, code == 2190 ? QStringLiteral("商品状态已更新") : errMsg(result)); loadProducts(); }); });
        }
    });
}

void AdminPage::loadOrders() {
    // 管理员调 fetchOrders 时后端返回全部用户订单，每条带 username 字段
    api_->fetchOrders([this](quint32 code, const json& j) {
        if (code != 2040) return;
        orderTable_->setRowCount(0);
        for (const auto& o : j.value("orders", json::array())) {
            int row = orderTable_->rowCount();
            orderTable_->insertRow(row);
            orderTable_->setItem(row, 0, new QTableWidgetItem(
                QString::fromStdString(o.value("order_no", ""))));
            orderTable_->setItem(row, 1, new QTableWidgetItem(
                QString::fromStdString(o.value("username", ""))));
            orderTable_->setItem(row, 2, new QTableWidgetItem(
                "¥" + yuan(o.value("final", 0))));
            orderTable_->setItem(row, 3, new QTableWidgetItem(
                orderStatusText(o.value("status", 0))));
            orderTable_->setItem(row, 4, new QTableWidgetItem(
                QString::fromStdString(o.value("create_time", ""))));
            const int orderId = o.value("order_id", 0);
            const int status = o.value("status", 0);
            auto* action = new QPushButton;
            if (status == 1) action->setText(QStringLiteral("标记发货"));
            else if (status == 2) action->setText(QStringLiteral("标记完成"));
            else action->setText(QStringLiteral("无操作"));
            action->setEnabled(status == 1 || status == 2);
            orderTable_->setCellWidget(row, 5, action);
            connect(action, &QPushButton::clicked, this, [this, action, orderId, status] {
                api_->updateOrderStatus(orderId, status + 1, [this, action](quint32 response, const json& result) {
                    showTransient(tip_, response == 2095 ? QStringLiteral("订单状态已更新") : errMsg(result));
                    if (response == 2095) { action->setEnabled(false); loadOrders(); }
                });
            });
        }
    });
}
