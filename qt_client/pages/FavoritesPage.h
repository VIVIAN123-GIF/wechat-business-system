#pragma once
#include <QWidget>
#include <nlohmann/json.hpp>
class QListWidget;
class QLabel;
class ApiClient;
class FavoritesPage : public QWidget {
    Q_OBJECT
public:
    explicit FavoritesPage(ApiClient* api, QWidget* parent = nullptr);
    void refresh();
private:
    ApiClient* api_;
    QListWidget* list_;
    QLabel* tip_;
};
