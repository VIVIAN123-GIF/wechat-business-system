#include "CartPage.h"
#include "../ApiClient.h"
#include "../UiUtil.h"
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QHBoxLayout>

CartPage::CartPage(ApiClient* api, QWidget* parent)
    : QWidget(parent), api_(api) {

    auto* root = new QVBoxLayout(this);

    table_ = new QTableWidget(0, 6);
    table_->setHorizontalHeaderLabels(
        {QStringLiteral("ID"), QStringLiteral("名称"), QStringLiteral("单价"),
         QStringLiteral("数量"), QStringLiteral("小计"), QStringLiteral("操作")});
    table_->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table_->verticalHeader()->setVisible(false);
    table_->horizontalHeader()->setStretchLastSection(true);
    table_->setColumnWidth(1, 220);
    table_->setColumnWidth(3, 120);
    table_->setColumnWidth(5, 100);
    root->addWidget(table_);

    auto* bottom = new QHBoxLayout;
    countLbl_ = new QLabel;
    totalLbl_ = new QLabel;
    QFont f = totalLbl_->font();
    f.setBold(true); f.setPointSize(12);
    totalLbl_->setFont(f);
    auto* clearBtn = new QPushButton(QStringLiteral("清空购物车"));
    bottom->addWidget(countLbl_);
    bottom->addStretch();
    bottom->addWidget(totalLbl_);
    bottom->addWidget(clearBtn);
    root->addLayout(bottom);

    connect(clearBtn, &QPushButton::clicked, this, [this]{
        api_->cartClear();
        rebuild();
    });
}

void CartPage::refresh() { rebuild(); }

void CartPage::rebuild() {
    table_->setRowCount(0);
    const auto& lines = api_->cart();
    for (const auto& l : lines) {
        int row = table_->rowCount();
        table_->insertRow(row);

        table_->setItem(row, 0, new QTableWidgetItem(QString::number(l.productId)));
        table_->setItem(row, 1, new QTableWidgetItem(l.name));
        table_->setItem(row, 2, new QTableWidgetItem("¥" + yuan(l.priceCents)));

        auto* spin = new QSpinBox;
        spin->setRange(0, 999);
        spin->setValue(l.quantity);
        table_->setCellWidget(row, 3, spin);

        table_->setItem(row, 4, new QTableWidgetItem(
            "¥" + yuan(l.priceCents * l.quantity)));

        auto* delBtn = new QPushButton(QStringLiteral("删除"));
        table_->setCellWidget(row, 5, delBtn);

        int pid = l.productId;
        connect(spin, QOverload<int>::of(&QSpinBox::valueChanged),
                this, [this, pid](int v) {
            api_->cartSetQty(pid, v);
            rebuild();
        });
        connect(delBtn, &QPushButton::clicked, this, [this, pid]{
            api_->cartRemove(pid);
            rebuild();
        });
    }

    countLbl_->setText(QStringLiteral("共 %1 件商品").arg(api_->cartCount()));
    totalLbl_->setText(QStringLiteral("合计：¥%1").arg(yuan(api_->cartTotalCents())));
}
