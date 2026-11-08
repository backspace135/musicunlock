#pragma once
// 窗口共享的玻璃菜单（#63），对应 macOS 的 GlassMenu.swift。
// 窗口级 Popup 避免菜单被面板裁剪；同一时刻只有一个菜单，从来源按钮展开并向其收起。

#include <functional>
#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.h>

namespace glass_menu {

namespace xaml = winrt::Microsoft::UI::Xaml;

/// 一行菜单项；开始收起后执行 action，说明和分隔线没有 action。
struct Entry {
    enum class Kind { item, note, divider } kind = Kind::item;
    std::wstring text;
    bool checked = false;
    bool enabled = true;
    std::function<void()> action;
};

/// 将共享菜单挂到窗口根节点，只调用一次。
void attach(xaml::FrameworkElement const& root);

/// 创建来源按钮。每次打开时重新读取 entries；hides=false 时保留标题栏绘制的按钮。
xaml::Controls::Button make_button(std::wstring title, std::function<std::vector<Entry>()> entries, bool hides = true);

/// 当前值和上下箭头组成的胶囊选择器；on_glass 使用普通表面，避免玻璃叠加。
/// minimum 限定最小宽度，changed 接收选择索引。
xaml::FrameworkElement make_picker(std::wstring title, std::vector<std::wstring> options, int selected, double minimum,
                                   bool on_glass, std::function<void(int)> changed);

/// 模型更新选择器时同步显示值和无障碍名称，不触发 changed。
void select(xaml::FrameworkElement const& picker, int index);

/// 从已有按钮打开菜单；标题栏账号按钮使用 hides=false，从按钮后方展开。
void show_entries(xaml::FrameworkElement const& button, std::wstring title, bool hides,
                  std::function<std::vector<Entry>()> entries);

/// 静态预览打开菜单；未能匹配按钮或打开失败时抛出异常。
void open_for_preview(xaml::FrameworkElement const& button);

/// 收起当前菜单，供交互、窗口失活及预览使用。
void close();

}  // namespace glass_menu
