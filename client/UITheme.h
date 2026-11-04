// ============================================================
// UITheme.h - 主题色定义（ANSI 16/256 色码，终端通用）
//   三套内置配色：蓝色 / 绿色 / 粉色
//   Windows 终端需在 main() 中启用 ENABLE_VIRTUAL_TERMINAL_PROCESSING
// ============================================================
#pragma once
#include <string>

// 主题色（一个配色族）：各 UI 元素的 ANSI 前景色码
struct UITheme {
    std::string name;    // 主题名："蓝色" "绿色" "粉色"
    std::string title;   // 标题（加粗）
    std::string accent;  // 高亮（表头/关键数字）
    std::string border;  // 边框/分割线
    std::string error;   // 错误（红色系）
    std::string info;    // 常规提示（主题色）
    std::string reset;   // 重置码 "\033[0m"

    // 给文本包裹主题色
    std::string paint(const std::string& s, const std::string& color) const {
        return color + s + reset;
    }
    std::string titleOf(const std::string& s)  const { return paint(s, title); }
    std::string accentOf(const std::string& s) const { return paint(s, accent); }
    std::string borderOf(const std::string& s) const { return paint(s, border); }
    std::string errorOf(const std::string& s)  const { return paint(s, error); }
    std::string infoOf(const std::string& s)   const { return paint(s, info); }
};

// 三套内置配色（抽象工厂的产品族共享同一配色）
inline const UITheme& themeBlue() {
    static UITheme t = {"蓝色", "\033[1;94m", "\033[94m", "\033[34m",
                        "\033[91m", "\033[94m", "\033[0m"};
    return t;
}
inline const UITheme& themeGreen() {
    static UITheme t = {"绿色", "\033[1;92m", "\033[92m", "\033[32m",
                        "\033[91m", "\033[92m", "\033[0m"};
    return t;
}
inline const UITheme& themePink() {
    static UITheme t = {"粉色", "\033[1;95m", "\033[95m", "\033[35m",
                        "\033[91m", "\033[95m", "\033[0m"};
    return t;
}
