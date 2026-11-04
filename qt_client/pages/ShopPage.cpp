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

    dDesc_ = new QLabel;
    dDesc_->setWordWrap(true);
    dDesc_->setMinimumHeight(120);
    dDesc_->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    right->addWidget(new QLabel(QStringLiteral("商品描述：")));
    right->addWidget(dDesc_);

    auto* addRow = new QHBoxLayout;
    qtySpin_ = new QSpinBox;
    qtySpin_->setMinimum(1);
    qtySpin_->setMaximum(999);
    addBtn_ = new QPushButton(QStringLiteral("加入购物车"));
    addBtn_->setEnabled(false);
    addRow->addWidget(new QLabel(QStringLiteral("数量：")));
    addRow->addWidget(qtySpin_);
    addRow->addWidget(addBtn_);
    right->addLayout(addRow);
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
}

void ShopPage::refresh() {
    api_->fetchProducts([this](quint32 code, const json& j) {
        if (code == 2001) loadProducts(j);
    });
}

void ShopPage::doSearch() {
    QString kw = searchEdit_->text().trimmed();
    if (kw.isEmpty()) { refresh(); return; }
    api_->search(kw, [this](quint32 code, const json& j) {
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
        currentStock_ = j.value("stock", 0);
        dName_->setText(currentName_);
        dPrice_->setText("¥" + yuan(currentPrice_));
        dStock_->setText(QString::number(currentStock_));
        dCat_->setText(QString::fromStdString(j.value("category", "")));
        dDesc_->setText(QString::fromStdString(j.value("desc", "")));
        qtySpin_->setMaximum(currentStock_ > 0 ? currentStock_ : 1);
        qtySpin_->setValue(1);
        addBtn_->setEnabled(currentStock_ > 0);
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
            api_->cartAdd(currentPid_, currentName_, currentPrice_, qty);
            addBtn_->setText(QStringLiteral("已加入 ✓"));
            QTimer::singleShot(1200, this, [this]{
                addBtn_->setText(QStringLiteral("加入购物车"));
            });
        } else {
            dDesc_->setText(errMsg(j));
        }
    });
}
