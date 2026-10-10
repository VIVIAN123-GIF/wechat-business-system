// ============================================================
// UIFactory.h - UI 皮肤抽象工厂（抽象工厂模式）
//   产品族 = 同一主题色下的多款界面风格
//   具体工厂：BlueThemeFactory / GreenThemeFactory / PinkThemeFactory
//   客户端"一键换肤" = 切换具体工厂并创建对应风格产品
// ============================================================
#pragma once
#include <memory>
#include <string>
#include "UIRenderer.h"
#include "UITheme.h"

class UIFactory {
public:
    virtual ~UIFactory() = default;
    virtual std::unique_ptr<UIRenderer> createSimple() = 0;  // 极简风格产品
    virtual std::unique_ptr<UIRenderer> createFancy()  = 0;  // 详细风格产品
    virtual std::string getThemeName() const = 0;            // 本族主题色名
    virtual const UITheme& theme() const = 0;                // 本族配色
};

// ---------- 具体工厂：蓝色主题族 ----------
class BlueThemeFactory : public UIFactory {
public:
    std::unique_ptr<UIRenderer> createSimple() override;
    std::unique_ptr<UIRenderer> createFancy()  override;
    std::string getThemeName() const override { return themeBlue().name; }
    const UITheme& theme() const override { return themeBlue(); }
};

// ---------- 具体工厂：绿色主题族 ----------
class GreenThemeFactory : public UIFactory {
public:
    std::unique_ptr<UIRenderer> createSimple() override;
    std::unique_ptr<UIRenderer> createFancy()  override;
    std::string getThemeName() const override { return themeGreen().name; }
    const UITheme& theme() const override { return themeGreen(); }
};

// ---------- 具体工厂：粉色主题族 ----------
class PinkThemeFactory : public UIFactory {
public:
    std::unique_ptr<UIRenderer> createSimple() override;
    std::unique_ptr<UIRenderer> createFancy()  override;
    std::string getThemeName() const override { return themePink().name; }
    const UITheme& theme() const override { return themePink(); }
};
