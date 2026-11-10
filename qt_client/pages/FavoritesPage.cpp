// ============================================================
// FavoritesPage.cpp - 我的收藏
// ============================================================
#include "FavoritesPage.h"
#include "../ApiClient.h"
#include "../UiUtil.h"
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QHBoxLayout>
#include <QVBoxLayout>
FavoritesPage::FavoritesPage(ApiClient* api, QWidget* parent) : QWidget(parent), api_(api) {
    auto* root = new QVBoxLayout(this);
    root->addWidget(new QLabel(QStringLiteral("我的收藏")));
    list_ = new QListWidget; root->addWidget(list_);
    tip_ = new QLabel; root->addWidget(tip_);
}
void FavoritesPage::refresh() {
    list_->clear(); tip_->clear();
    api_->fetchFavorites([this](quint32 code, const json& result) {
        if (code != 2160) { showTransient(tip_, errMsg(result)); return; }
        for (const auto& favorite : result.value("favorites", json::array())) {
            auto* item = new QListWidgetItem(list_);
            auto* row = new QWidget;
            auto* layout = new QHBoxLayout(row); layout->setContentsMargins(4, 2, 4, 2);
            layout->addWidget(new QLabel(QStringLiteral("#%1  %2  ¥%3  库存 %4")
                .arg(favorite.value("product_id", 0))
                .arg(QString::fromStdString(favorite.value("name", "")))
                .arg(yuan(favorite.value("price", 0)))
                .arg(favorite.value("stock", 0))));
            layout->addStretch();
            auto* remove = new QPushButton(QStringLiteral("取消收藏")); layout->addWidget(remove);
            item->setSizeHint(row->sizeHint()); list_->setItemWidget(item, row);
            int productId = favorite.value("product_id", 0);
            connect(remove, &QPushButton::clicked, this, [this, productId] {
                api_->toggleFavorite(productId, [this](quint32, const json&) { refresh(); });
            });
        }
    });
}
