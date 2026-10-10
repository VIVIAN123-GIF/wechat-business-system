// ============================================================
// UIFactory.cpp - 抽象工厂具体实现
//   每个具体工厂以本族主题色创建极简/详细两种风格产品
// ============================================================
#include "UIFactory.h"
#include "Renderers.h"

// ---------- 蓝色主题族 ----------
std::unique_ptr<UIRenderer> BlueThemeFactory::createSimple() {
    return std::make_unique<SimpleRenderer>(themeBlue());
}
std::unique_ptr<UIRenderer> BlueThemeFactory::createFancy() {
    return std::make_unique<FancyRenderer>(themeBlue());
}

// ---------- 绿色主题族 ----------
std::unique_ptr<UIRenderer> GreenThemeFactory::createSimple() {
    return std::make_unique<SimpleRenderer>(themeGreen());
}
std::unique_ptr<UIRenderer> GreenThemeFactory::createFancy() {
    return std::make_unique<FancyRenderer>(themeGreen());
}

// ---------- 粉色主题族 ----------
std::unique_ptr<UIRenderer> PinkThemeFactory::createSimple() {
    return std::make_unique<SimpleRenderer>(themePink());
}
std::unique_ptr<UIRenderer> PinkThemeFactory::createFancy() {
    return std::make_unique<FancyRenderer>(themePink());
}
