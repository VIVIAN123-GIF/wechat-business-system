// ============================================================
// ShopPage.h - 商城页（商品列表 / 搜索 / 详情 / 加入购物车）
// ============================================================
#pragma once
#include <QWidget>
#include <nlohmann/json.hpp>

class QTableWidget;
class QLineEdit;
class QLabel;
class QSpinBox;
class QPushButton;
class ApiClient;

class ShopPage : public QWidget {
    Q_OBJECT
public:
    explicit ShopPage(ApiClient* api, QWidget* parent = nullptr);
    void refresh();

private:
    ApiClient* api_;
    QTableWidget* table_;
    QLineEdit* searchEdit_;
    QLabel* dName_;
    QLabel* dPrice_;
    QLabel* dStock_;
    QLabel* dCat_;
    QLabel* dDesc_;
    QSpinBox* qtySpin_;
    QPushButton* addBtn_;

    int currentPid_ = 0;
    int currentStock_ = 0;
    int currentPrice_ = 0;
    QString currentName_;

    void loadProducts(const nlohmann::json& j);
    void doSearch();
    void showDetail(int pid);
    void addToCart();
};
