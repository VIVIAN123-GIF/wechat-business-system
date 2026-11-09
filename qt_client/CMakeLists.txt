cmake_minimum_required(VERSION 3.16)
project(qt_client LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

add_executable(qt_client WIN32
    main.cpp
    NetworkClient.h NetworkClient.cpp
    ApiClient.h ApiClient.cpp
    MainWindow.h MainWindow.cpp
    pages/LoginPage.h pages/LoginPage.cpp
    pages/ShopPage.h pages/ShopPage.cpp
    pages/CartPage.h pages/CartPage.cpp
    pages/CheckoutPage.h pages/CheckoutPage.cpp
    pages/OrderPage.h pages/OrderPage.cpp
    pages/CouponPage.h pages/CouponPage.cpp
    pages/NotificationPage.h pages/NotificationPage.cpp
    pages/FavoritesPage.h pages/FavoritesPage.cpp
    pages/AdminPage.h pages/AdminPage.cpp
    pages/ReturnPage.h pages/ReturnPage.cpp
    pages/StatsPage.h pages/StatsPage.cpp
    pages/ServicePage.h pages/ServicePage.cpp
    resources.qrc
)

target_include_directories(qt_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    ${CMAKE_CURRENT_SOURCE_DIR}/../vendor
)

target_link_libraries(qt_client PRIVATE Qt6::Widgets Qt6::Network)
