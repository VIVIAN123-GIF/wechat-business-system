// ============================================================
// ShopPage.cpp - 商品列表 / 搜索 / 详情 / 加购
// ============================================================
#include "ShopPage.h"
#include "../ApiClient.h"
#include "../UiUtil.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QLineEdit>
#include <QLabel>
#include <QSpinBox>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QClipboard>
#include <QApplication>
#include <QComboBox>
#include <algorithm>

ShopPage::ShopPage(ApiClient* api, QWidget* parent)
    : QWidget(parent), api_(api) {

    auto* root = new QHBoxLayout(this);

    // ---------- 左：商品列表 ----------
    auto* left = new QVBoxLayout;

    auto* searchRow = new QHBoxLayout;
    searchEdit_ = new QLineEdit;
    searchEdit_->setPlaceholderText(QStringLiteral("按名称 / 分类 / 描述搜索"));
    auto* searchBtn = new QPushButton(QStringLiteral("搜索"));
    auto* allBtn = new QPushButton(QStringLiteral("全部商品"));
    searchRow->addWidget(searchEdit_);
    searchRow->addWidget(searchBtn);
    searchRow->addWidget(allBtn);
    minPriceSpin_ = new QSpinBox; minPriceSpin_->setRange(0, 100000000);
    maxPriceSpin_ = new QSpinBox; maxPriceSpin_->setRange(0, 100000000);
    sortBox_ = new QComboBox; sortBox_->addItem(QStringLiteral("default"), ""); sortBox_->addItem(QStringLiteral("price asc"), "price_asc"); sortBox_->addItem(QStringLiteral("price desc"), "price_desc"); sortBox_->addItem(QStringLiteral("stock"), "stock_desc");
    searchRow->addWidget(minPriceSpin_); searchRow->addWidget(maxPriceSpin_); searchRow->addWidget(sortBox_);
    left->addLayout(searchRow);

    table_ = new QTableWidget(0, 5);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("ID"), QStringLiteral("名称"), QStringLiteral("分类"),
         QStringLiteral("价格"), QStringLiteral("库存")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->setSelectionBehavior(QAbstractItemView::SelectRows);
    table_->setSelectionMode(QAbstractItemView::SingleSelection);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(0, 50);
    table_->setColumnWidth(1, 200);
    table_->setColumnWidth(2, 80);
    table_->setColumnWidth(3, 100);
    left->addWidget(table_);

    root->addLayout(left, 3);

    // ---------- 右：详情 + 加购 ----------
    auto* right = new QVBoxLayout;
    auto* form = new QFormLayout;
    dName_  = new QLabel(QStringLiteral("（请在左侧选择商品）"));
    dPrice_ = new QLabel("--");
    dStock_ = new QLabel("--");
    dCat_   = new QLabel("--");
    form->addRow(QStringLiteral("名称："), dName_);
    form->addRow(QStringLiteral("价格："), dPrice_);
    form->addRow(QStringLiteral("库存："), dStock_);
    form->addRow(QStringLiteral("分类："), dCat_);
    right->addLayout(form);
    skuBox_ = new QComboBox;
    skuBox_->addItem(QStringLiteral("default sku"), 0);
    right->addWidget(skuBox_);

    dDesc_ = new QLabel;
    dDesc_->setWordWrap(true);
    dDesc_->setMinimumHeight(120);
    dDesc_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    right->addWidget(new QLabel(QStringLiteral("商品描述：")));
    right->addWidget(dDesc_);

    dReviews_ = new QLabel;
    dReviews_->setWordWrap(true);
    dReviews_->setMinimumHeight(100);
    dReviews_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    right->addWidget(new QLabel(QStringLiteral("商品评价：")));
    right->addWidget(dReviews_);
    auto* refreshReviewsBtn = new QPushButton(QStringLiteral("刷新评价"));
    right->addWidget(refreshReviewsBtn);

    auto* addRow = new QHBoxLayout;
    auto* shareBtn = new QPushButton(QStringLiteral("分享给好友"));
    addRow->addWidget(shareBtn);
    auto* favoriteBtn = new QPushButton(QStringLiteral("收藏商品"));
    addRow->addWidget(favoriteBtn);
    qtySpin_ = new QSpinBox;
    qtySpin_->setMinimum(1);
    qtySpin_->setMaximum(999);
    addBtn_ = new QPushButton(QStringLiteral("加入购物车"));
    addBtn_->setEnabled(false);
    addRow->addWidget(new QLabel(QStringLiteral("数量：")));
    addRow->addWidget(qtySpin_);
    addRow->addWidget(addBtn_);
    right->addLayout(addRow);
    auto* groupRow = new QHBoxLayout;
    groupTargetSpin_ = new QSpinBox;
    groupTargetSpin_->setRange(2, 20); groupTargetSpin_->setValue(2);
    groupHoursSpin_ = new QSpinBox;
    groupHoursSpin_->setRange(1, 168); groupHoursSpin_->setValue(24);
    createGroupBtn_ = new QPushButton(QStringLiteral("发起拼团"));
    groupRow->addWidget(new QLabel(QStringLiteral("拼团人数")));
    groupRow->addWidget(groupTargetSpin_);
    groupRow->addWidget(new QLabel(QStringLiteral("有效小时")));
    groupRow->addWidget(groupHoursSpin_);
    groupRow->addWidget(createGroupBtn_);
    right->addLayout(groupRow);
    groupTable_ = new QTableWidget(0, 4);
    groupTable_->setHorizontalHeaderLabels({QStringLiteral("拼团编号"), QStringLiteral("进度"), QStringLiteral("截止时间"), QStringLiteral("操作")});
    groupTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    groupTable_->verticalHeader()->setVisible(false);
    groupTable_->horizontalHeader()->setStretchLastSection(true);
    right->addWidget(groupTable_);
    right->addStretch();

    root->addLayout(right, 2);

    connect(searchBtn, &QPushButton::clicked, this, &ShopPage::doSearch);
    connect(allBtn, &QPushButton::clicked, this, [this]{ searchEdit_->clear(); refresh(); });
    connect(searchEdit_, &QLineEdit::returnPressed, this, &ShopPage::doSearch);
    connect(table_, &QTableWidget::itemSelectionChanged, this, [this]() {
        auto sel = table_->selectedItems();
        if (!sel.isEmpty()) showDetail(sel.first()->text().toInt());
    });
    connect(addBtn_, &QPushButton::clicked, this, &ShopPage::addToCart);
    connect(createGroupBtn_, &QPushButton::clicked, this, &ShopPage::createGroup);
    connect(skuBox_, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this](int) {
        currentSkuId_ = skuBox_->currentData().toInt();
        currentSkuPrice_ = currentPrice_;
        if (currentSkuId_ <= 0) {
            currentStock_ = currentProductStock_;
            dPrice_->setText(QStringLiteral("¥") + yuan(currentPrice_));
            qtySpin_->setMaximum(std::max(1, currentStock_));
            addBtn_->setEnabled(currentStock_ > 0);
            return;
        }
        api_->fetchSkus(currentPid_, [this](quint32 code, const json& result) {
            if (code != 2200) return;
            for (const auto& sku : result.value("skus", json::array())) {
                if (sku.value("sku_id", 0) != currentSkuId_) continue;
                currentSkuPrice_ = currentPrice_ + sku.value("price_delta", 0);
                currentStock_ = sku.value("stock", 0);
                dPrice_->setText(QStringLiteral("¥") + yuan(currentSkuPrice_));
                qtySpin_->setMaximum(std::max(1, currentStock_));
                addBtn_->setEnabled(currentStock_ > 0);
                break;
            }
        });
    });
    connect(shareBtn, &QPushButton::clicked, this, [this] {
        QString text = QStringLiteral("【XX 小店】我看中了《%1》，只要 ¥%2，点链接查看 ms://product/%3")
            .arg(currentName_).arg(yuan(currentPrice_)).arg(currentPid_);
        QApplication::clipboard()->setText(text);
        dDesc_->setText(QStringLiteral("分享文案已复制：\n") + text);
    });
    connect(favoriteBtn, &QPushButton::clicked, this, [this] {
        api_->toggleFavorite(currentPid_, [this](quint32 code, const json& result) {
            dDesc_->setText(code == 2161 ? (result.value("favorite", false) ? QStringLiteral("已收藏商品") : QStringLiteral("已取消收藏")) : errMsg(result));
        });
    });
    connect(refreshReviewsBtn, &QPushButton::clicked, this, [this] {
        if (currentPid_ <= 0) return;
        api_->fetchReviews(currentPid_, [this](quint32 code, const json& result) {
            if (code != 2131) { showTransient(dReviews_, errMsg(result)); return; }
            QString text = QStringLiteral("平均分：%1（%2 条）")
                .arg(result.value("review_average", 0.0)).arg(result.value("review_count", 0));
            for (const auto& review : result.value("reviews", json::array())) {
                text += QStringLiteral("\n%1：%2 分 %3")
                    .arg(QString::fromStdString(review.value("username", "匿名")))
                    .arg(review.value("rating", 0))
                    .arg(QString::fromStdString(review.value("content", "")));
            }
            dReviews_->setText(text);
        });
    });
}

void ShopPage::refresh() {
    api_->fetchProducts([this](quint32 code, const json& j) {
        if (code == 2001) loadProducts(j);
    });
}

void ShopPage::doSearch() {
    QString kw = searchEdit_->text().trimmed();
    api_->search(kw, minPriceSpin_->value(), maxPriceSpin_->value(), {}, sortBox_->currentData().toString(), [this](quint32 code, const json& j) {
        if (code == 2005) loadProducts(j);
    });
}

void ShopPage::loadProducts(const nlohmann::json& j) {
    table_->setRowCount(0);
    if (!j.contains("products")) return;
    for (const auto& p : j["products"]) {
        int row = table_->rowCount();
        table_->insertRow(row);
        table_->setItem(row, 0, new QTableWidgetItem(QString::number(p.value("id", 0))));
        table_->setItem(row, 1, new QTableWidgetItem(
            QString::fromStdString(p.value("name", ""))));
        table_->setItem(row, 2, new QTableWidgetItem(
            QString::fromStdString(p.value("category", ""))));
        table_->setItem(row, 3, new QTableWidgetItem("¥" + yuan(p.value("price", 0))));
        table_->setItem(row, 4, new QTableWidgetItem(
            QString::number(p.value("stock", 0))));
    }
}

void ShopPage::showDetail(int pid) {
    api_->fetchDetail(pid, [this](quint32 code, const json& j) {
        if (code != 2002) return;
        currentPid_   = j.value("id", 0);
        currentName_  = QString::fromStdString(j.value("name", ""));
        currentPrice_ = j.value("price", 0);
        currentSkuPrice_ = currentPrice_;
        currentSkuId_ = 0;
        currentStock_ = j.value("stock", 0);
        currentProductStock_ = currentStock_;
        dName_->setText(currentName_);
        dPrice_->setText("¥" + yuan(currentPrice_));
        dStock_->setText(QString::number(currentStock_));
        dCat_->setText(QString::fromStdString(j.value("category", "")));
        dDesc_->setText(QString::fromStdString(j.value("desc", "")));
        skuBox_->clear(); skuBox_->addItem(QStringLiteral("default sku"), 0);
        QString reviewText = QStringLiteral("平均分：%1（%2 条）")
            .arg(j.value("review_average", 0.0))
            .arg(j.value("review_count", 0));
        for (const auto& review : j.value("reviews", json::array())) {
            reviewText += QStringLiteral("\n%1：%2 分 %3")
                .arg(QString::fromStdString(review.value("username", "匿名")))
                .arg(review.value("rating", 0))
                .arg(QString::fromStdString(review.value("content", "")));
        }
        dReviews_->setText(reviewText);
        api_->fetchSkus(currentPid_, [this](quint32 skuCode, const json& skuResult) {
            if (skuCode != 2200) return;
            QString skuText = QStringLiteral("\n规格：");
            for (const auto& sku : skuResult.value("skus", json::array())) {
                skuBox_->addItem(QStringLiteral("%1 / %2 stock %3")
                    .arg(QString::fromStdString(sku.value("color", "")))
                    .arg(QString::fromStdString(sku.value("size", "")))
                    .arg(sku.value("stock", 0)), sku.value("sku_id", 0));
                skuText += QStringLiteral("\n%1 / %2，库存 %3")
                    .arg(QString::fromStdString(sku.value("color", "")))
                    .arg(QString::fromStdString(sku.value("size", "")))
                    .arg(sku.value("stock", 0));
            }
            dReviews_->setText(dReviews_->text() + skuText);
        });
        qtySpin_->setMaximum(currentStock_ > 0 ? currentStock_ : 1);
        qtySpin_->setValue(1);
        addBtn_->setEnabled(currentStock_ > 0);
        loadGroups();
    });
}

void ShopPage::addToCart() {
    int qty = qtySpin_->value();
    if (qty > currentStock_) return;
    addBtn_->setEnabled(false);
    api_->addToCart(currentPid_, qty, [this, qty](quint32 code, const json& j) {
        addBtn_->setEnabled(true);
        if (code == 2010) {
            // 服务端校验通过，同步加入本地购物车
            api_->cartAdd(currentPid_, currentName_, currentSkuPrice_, qty, currentSkuId_, skuBox_->currentText());
            addBtn_->setText(QStringLiteral("已加入 ✓"));
            QTimer::singleShot(1200, this, [this]{
                addBtn_->setText(QStringLiteral("加入购物车"));
            });
        } else {
            dDesc_->setText(errMsg(j));
        }
    }, currentSkuId_);
}

void ShopPage::loadGroups() {
    groupTable_->setRowCount(0);
    if (currentPid_ <= 0) return;
    api_->fetchGroups([this](quint32 code, const json& j) {
        if (code != 2093) return;
        for (const auto& g : j.value("groups", json::array())) {
            if (g.value("product_id", 0) != currentPid_) continue;
            int row = groupTable_->rowCount(); groupTable_->insertRow(row);
            groupTable_->setItem(row, 0, new QTableWidgetItem(QString::fromStdString(g.value("group_no", ""))));
            groupTable_->setItem(row, 1, new QTableWidgetItem(QStringLiteral("%1/%2").arg(g.value("member_count", 0)).arg(g.value("target_count", 0))));
            groupTable_->setItem(row, 2, new QTableWidgetItem(QString::fromStdString(g.value("deadline", ""))));
            auto* join = new QPushButton(QStringLiteral("加入"));
            groupTable_->setCellWidget(row, 3, join);
            int groupId = g.value("group_id", 0);
            connect(join, &QPushButton::clicked, this, [this, groupId] {
                api_->joinGroup(groupId, [this](quint32 joinCode, const json& result) {
                    if (joinCode == 2092) {
                        showTransient(dDesc_, result.value("status", 0) == 1 ? QStringLiteral("拼团成功，请到我的订单完成支付") : QStringLiteral("加入拼团成功"));
                        loadGroups();
                    } else showTransient(dDesc_, errMsg(result));
                });
            });
        }
    });
}

void ShopPage::createGroup() {
    if (currentPid_ <= 0) return;
    createGroupBtn_->setEnabled(false);
    api_->createGroup(currentPid_, groupTargetSpin_->value(), groupHoursSpin_->value(), [this](quint32 code, const json& result) {
        createGroupBtn_->setEnabled(true);
        if (code == 2091) {
            showTransient(dDesc_, QStringLiteral("拼团已创建：%1").arg(QString::fromStdString(result.value("group_no", ""))));
            loadGroups();
        } else showTransient(dDesc_, errMsg(result));
    });
}
