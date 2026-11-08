#include "pch.h"

#include "GlassMenu.h"
#include "GlassMenuButton.h"
#include "GlassMenuItems.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <optional>

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Input.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.Text.h>

#include "Corners.h"
#include "GlassMotion.h"
#include "GlassSurface.h"
#include "Motion.h"
#include "MotionCapture.h"

namespace glass_menu {
namespace {

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Controls::Primitives;
using namespace winrt::Microsoft::UI::Xaml::Media;
using winrt::Windows::Foundation::Numerics::float2;

constexpr double kGap = 6;       // 按钮和菜单之间的距离
constexpr double kMargin = 8;    // 菜单与窗口边缘的最小距离
constexpr double kMenuRadius = 18;
constexpr double kRowHeight = 30;
constexpr double kMinWidth = 150;

struct Source {
    winrt::weak_ref<FrameworkElement> button;
    bool hides = true;
};

struct Menu {
    Source source;
    std::vector<Entry> entries;
    FrameworkElement panel{nullptr};  // 在窗口坐标系中定位的玻璃表面
    FrameworkElement rows{nullptr};   // 预先测量的内容，决定展开终点
    Border highlight{nullptr};
    StackPanel stack{nullptr};
    winrt::Windows::Foundation::Rect anchor{};
    winrt::Windows::Foundation::Rect target{};
    bool above = false;
    int highlighted = -1;
    winrt::Windows::Foundation::Rect current{};
    winrt::Windows::Foundation::Rect from{};
    double radius = 18;
    double from_radius = 18;
    double content_opacity = 0;
    double from_content_opacity = 0;
    double opacity = 1;
    double from_opacity = 1;
    bool closing = false;
    winrt::Microsoft::UI::Composition::CompositionRoundedRectangleGeometry clip{nullptr};
    winrt::Microsoft::UI::Composition::CompositionGeometricClip visual_clip{nullptr};
    std::chrono::steady_clock::time_point started;
};

void highlight(int index);
void choose(int index);
void grow();
void finish();
void advance(double elapsed);

FrameworkElement root_{nullptr};
struct Opener {
    winrt::weak_ref<Controls::Button> button;
    std::function<void()> open;
};
std::vector<Opener> openers_;
struct Picker {
    winrt::weak_ref<Controls::Button> button;
    winrt::weak_ref<TextBlock> value;
    std::wstring title;
    std::vector<std::wstring> options;
    int selected = -1;
};
std::vector<std::shared_ptr<Picker>> pickers_;
Popup popup_{nullptr};
Menu menu_;
bool open_ = false;
std::optional<winrt::event_token> rendering_;

winrt::hstring glyph(const wchar_t* text) { return winrt::hstring(text); }

Brush brush(const wchar_t* key) {
    return Application::Current().Resources().Lookup(winrt::box_value(winrt::hstring(key))).as<Brush>();
}

Controls::ControlTemplate picker_template(bool on_glass) {
    // 画刷由元素的 ThemeResource 解析，不能在控件创建时固化为应用的当前主题。
    std::wstring markup =
        LR"(<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation'
            xmlns:local='using:MusicUnlock' TargetType='Button'>)";
    if (on_glass) {
        markup += LR"(<Border Background='{ThemeResource MuSurfaceBrush}' BorderBrush='{ThemeResource MuSeparatorBrush}'
            BorderThickness='1' CornerRadius='14' Padding='{TemplateBinding Padding}'>
            <ContentPresenter HorizontalAlignment='Center' VerticalAlignment='Center'/></Border>)";
    } else {
        markup += LR"(<local:GlassSurface Kind='Control' Radius='-1' Padding='{TemplateBinding Padding}'>
            <ContentPresenter HorizontalAlignment='Center' VerticalAlignment='Center'/></local:GlassSurface>)";
    }
    markup += L"</ControlTemplate>";
    return Markup::XamlReader::Load(markup).as<Controls::ControlTemplate>();
}

/// 按钮在窗口根节点坐标系中的矩形。
winrt::Windows::Foundation::Rect anchor_of(FrameworkElement const& button) {
    const auto at = button.TransformToVisual(root_).TransformPoint({0, 0});
    return {static_cast<float>(at.X), static_cast<float>(at.Y), static_cast<float>(button.ActualWidth()),
            static_cast<float>(button.ActualHeight())};
}

void place(Menu& menu) {
    const auto window = winrt::Windows::Foundation::Size{static_cast<float>(root_.ActualWidth()),
                                                        static_cast<float>(root_.ActualHeight())};
    const double width = menu.rows.Width();
    const double height = menu.rows.Height();
    double x = menu.anchor.X;
    if (x + width > window.Width - kMargin) x = menu.anchor.X + menu.anchor.Width - width;
    x = std::clamp(x, kMargin, std::max(kMargin, static_cast<double>(window.Width) - kMargin - width));
    double y = menu.anchor.Y + menu.anchor.Height + kGap;
    menu.above = y + height > window.Height - kMargin;
    if (menu.above) y = menu.anchor.Y - kGap - height;
    y = std::max(y, kMargin);
    menu.target = {static_cast<float>(x), static_cast<float>(y), static_cast<float>(width), static_cast<float>(height)};
}

FrameworkElement build_rows(Menu const& menu) {
    const bool checks = std::any_of(menu.entries.begin(), menu.entries.end(),
                                    [](Entry const& entry) { return entry.kind == Entry::Kind::item && entry.checked; });
    StackPanel rows;
    rows.Padding(ThicknessHelper::FromUniformLength(6));
    rows.MinWidth(std::max(static_cast<double>(menu.anchor.Width), kMinWidth));
    for (const auto& entry : menu.entries) {
        if (entry.kind == Entry::Kind::divider) {
            Border line;
            line.Height(1);
            line.Margin(Thickness{10, 4, 10, 4});
            line.Background(brush(L"MuSeparatorBrush"));
            rows.Children().Append(line);
            continue;
        }
        TextBlock text;
        text.Text(entry.text);
        if (entry.kind == Entry::Kind::note) {
            text.FontSize(11);
            text.Foreground(brush(L"MuSecondaryBrush"));
            text.Margin(Thickness{12, 5, 12, 5});
            rows.Children().Append(text);
            continue;
        }
        Grid row;
        row.Height(kRowHeight);
        row.Padding(Thickness{12, 0, 12, 0});
        row.ColumnSpacing(8);
        int column = 0;
        if (checks) {
            ColumnDefinition check_column;
            check_column.Width(GridLengthHelper::Auto());
            row.ColumnDefinitions().Append(check_column);
            TextBlock mark;
            mark.Text(glyph(L"\uE73E"));  // 勾选图标
            mark.FontFamily(FontFamily(L"Segoe Fluent Icons, Segoe MDL2 Assets"));
            mark.FontSize(11);
            mark.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
            mark.Opacity(entry.checked ? 1 : 0);
            mark.Width(12);
            mark.VerticalAlignment(VerticalAlignment::Center);
            row.Children().Append(mark);
            ++column;
        }
        ColumnDefinition text_column;
        text_column.Width(GridLength{1, GridUnitType::Star});
        row.ColumnDefinitions().Append(text_column);
        text.FontSize(13);
        text.VerticalAlignment(VerticalAlignment::Center);
        if (!entry.enabled) text.Opacity(0.6), text.Foreground(brush(L"MuSecondaryBrush"));
        Grid::SetColumn(text, column);
        row.Children().Append(text);
        row.Background(SolidColorBrush(winrt::Windows::UI::Color{0, 0, 0, 0}));
        row.IsHitTestVisible(entry.enabled);
        const int index = static_cast<int>(rows.Children().Size());
        row.PointerEntered([index](auto&&, auto&&) { highlight(index); });
        auto item = winrt::make<winrt::MusicUnlock::implementation::GlassMenuButton>();
        auto state = winrt::get_self<winrt::MusicUnlock::implementation::GlassMenuButton>(item);
        state->menu_item = true;
        state->checkable = checks;
        state->selected = entry.checked;
        item.Content(row);
        item.Padding(ThicknessHelper::FromUniformLength(0));
        item.HorizontalContentAlignment(HorizontalAlignment::Stretch);
        item.IsEnabled(entry.enabled);
        item.IsTabStop(false);
        item.Template(Markup::XamlReader::Load(
            LR"(<ControlTemplate xmlns='http://schemas.microsoft.com/winfx/2006/xaml/presentation' TargetType='Button'>
                <Border Background='Transparent'><ContentPresenter/></Border></ControlTemplate>)")
                          .as<Controls::ControlTemplate>());
        Automation::AutomationProperties::SetName(item, entry.text);
        if (entry.checked) Automation::AutomationProperties::SetItemStatus(item, L"已选中");
        item.Click([index](auto&&, auto&&) { choose(index); });
        rows.Children().Append(item);
    }
    Grid content;
    Border highlight;
    highlight.Height(kRowHeight);
    highlight.HorizontalAlignment(HorizontalAlignment::Stretch);
    highlight.VerticalAlignment(VerticalAlignment::Top);
    highlight.Margin({6, 0, 6, 0});
    highlight.CornerRadius(CornerRadiusHelper::FromUniformRadius(12));
    highlight.IsHitTestVisible(false);
    highlight.Opacity(0);
    auto colour = brush(L"MuTextBrush").as<SolidColorBrush>().Color();
    colour.A = 20;
    highlight.Background(SolidColorBrush(colour));
    content.Children().Append(highlight);
    content.Children().Append(rows);
    rows.PointerExited([](auto&&, auto&&) { menu_.highlighted = -1; menu_.highlight.Opacity(0); });
    menu_.highlight = highlight;
    menu_.stack = rows;
    auto items = winrt::make<winrt::MusicUnlock::implementation::GlassMenuItems>();
    items.Content(content);
    items.IsTabStop(true);
    return items;
}

/// 只有一块高亮在行间滑动；-1 清除高亮。
void highlight(int index) {
    if (!open_ || !menu_.rows) return;
    menu_.highlighted = index;
    if (index < 0 || index >= static_cast<int>(menu_.entries.size()) ||
        menu_.entries[index].kind != Entry::Kind::item || !menu_.entries[index].enabled) {
        menu_.highlight.Opacity(0);
        return;
    }
    const auto row = menu_.stack.Children().GetAt(index).as<FrameworkElement>();
    const float y = row.TransformToVisual(menu_.rows).TransformPoint({0, 0}).Y;
    auto visual = Hosting::ElementCompositionPreview::GetElementVisual(menu_.highlight);
    if (menu_.highlight.Opacity() == 0 || !motion::enabled()) {
        visual.Offset({0, y, 0});
    } else {
        auto animation = visual.Compositor().CreateScalarKeyFrameAnimation();
        animation.InsertExpressionKeyFrame(0, L"this.StartingValue");
        for (const auto& [time, value] : spring::keyframes(glass_motion::highlight)) {
            if (time <= 0) continue;
            // 非零起点也从当前值接续，避免公共零起点关键帧把高亮拉回首行。
            animation.InsertExpressionKeyFrame(time, L"this.StartingValue + (" + std::to_wstring(y) +
                L" - this.StartingValue) * " + std::to_wstring(value));
        }
        animation.Duration(std::chrono::duration_cast<winrt::Windows::Foundation::TimeSpan>(
            std::chrono::duration<double>(spring::settle_time(glass_motion::highlight))));
        visual.StartAnimation(L"Offset.Y", animation);
    }
    menu_.highlight.Opacity(1);
}

void choose(int index) {
    if (!open_ || index < 0 || index >= static_cast<int>(menu_.entries.size())) return;
    if (menu_.entries[index].kind != Entry::Kind::item || !menu_.entries[index].enabled) return;
    auto action = menu_.entries[index].action;
    close();
    if (action) action();
}

void show(Source source, std::vector<Entry> entries) {
    if (!popup_ || !source.button.get()) return;
    finish();
    menu_ = {};
    menu_.source = std::move(source);
    menu_.entries = std::move(entries);
    const auto button = menu_.source.button.get();
    menu_.anchor = anchor_of(button);

    auto surface = winrt::make<winrt::MusicUnlock::implementation::GlassSurface>();
    surface.Kind(winrt::MusicUnlock::GlassKind::Menu);
    surface.Radius(kMenuRadius);
    menu_.rows = build_rows(menu_);
    Automation::AutomationProperties::SetName(menu_.rows, Automation::AutomationProperties::GetName(button));
    surface.Content(menu_.rows);
    menu_.panel = surface;
    // 展开前离屏测量，动画开始后不改变最终内容尺寸。
    menu_.panel.Measure({10000, 10000});
    menu_.panel.UpdateLayout();
    const auto desired = menu_.panel.DesiredSize();
    menu_.rows.Width(desired.Width);
    menu_.rows.Height(desired.Height);
    place(menu_);

    popup_.Child(menu_.panel);
    if (!popup_.XamlRoot()) popup_.XamlRoot(root_.XamlRoot());
    popup_.HorizontalOffset(menu_.anchor.X);
    popup_.VerticalOffset(menu_.anchor.Y);
    menu_.panel.Width(menu_.anchor.Width);
    menu_.panel.Height(menu_.anchor.Height);
    popup_.IsOpen(true);
    open_ = true;
    if (auto control = button.try_as<winrt::MusicUnlock::GlassMenuButton>()) {
        winrt::get_self<winrt::MusicUnlock::implementation::GlassMenuButton>(control)->Expanded(true);
    }
    if (menu_.source.hides) button.Opacity(0);
    menu_.panel.IsTabStop(true);
    menu_.panel.KeyDown([](auto&&, Input::KeyRoutedEventArgs const& args) {
        const auto key = args.Key();
        using winrt::Windows::System::VirtualKey;
        if (key == VirtualKey::Tab) { close(); return; }
        if (key != VirtualKey::Up && key != VirtualKey::Down && key != VirtualKey::Enter && key != VirtualKey::Escape) {
            return;
        }
        args.Handled(true);
        if (key == VirtualKey::Escape) {
            auto button = menu_.source.button.get();
            close();
            if (button) button.Focus(FocusState::Programmatic);
            return;
        }
        const int step = key == VirtualKey::Down ? 1 : -1;
        if (key == VirtualKey::Enter) {
            choose(menu_.highlighted);
            return;
        }
        std::vector<int> enabled;
        for (int i = 0; i < static_cast<int>(menu_.entries.size()); ++i) {
            if (menu_.entries[i].kind == Entry::Kind::item && menu_.entries[i].enabled) enabled.push_back(i);
        }
        if (enabled.empty()) return;
        const auto found = std::find(enabled.begin(), enabled.end(), menu_.highlighted);
        const int next = found == enabled.end() ? (step > 0 ? enabled.front() : enabled.back())
                                                 : enabled[(found - enabled.begin() + step + enabled.size()) % enabled.size()];
        highlight(next);
    });
    menu_.rows.Focus(FocusState::Programmatic);
    grow();
}

void stop_rendering() {
    if (!rendering_) return;
    CompositionTarget::Rendering(*rendering_);
    rendering_.reset();
}

void finish() {
    stop_rendering();
    if (auto button = menu_.source.button.get()) {
        button.Opacity(1);
        if (auto control = button.try_as<winrt::MusicUnlock::GlassMenuButton>()) {
            winrt::get_self<winrt::MusicUnlock::implementation::GlassMenuButton>(control)->Expanded(false);
        }
    }
    if (popup_) {
        popup_.IsOpen(false);
        popup_.Child(nullptr);
    }
    open_ = false;
    menu_ = {};
}

void paint() {
    const auto rect = menu_.current;
    popup_.HorizontalOffset(rect.X);
    popup_.VerticalOffset(rect.Y);
    menu_.panel.Width(rect.Width);
    menu_.panel.Height(rect.Height);
    menu_.panel.as<winrt::MusicUnlock::GlassSurface>().Radius(menu_.radius);
    menu_.panel.Opacity(menu_.opacity);
    menu_.rows.Opacity(menu_.content_opacity);
    // 行的排版始终保持最终尺寸，随面板扩张显示，而不是把文字挤进按钮里。
    menu_.rows.HorizontalAlignment(HorizontalAlignment::Left);
    menu_.rows.VerticalAlignment(VerticalAlignment::Top);
    menu_.rows.Margin({menu_.target.X - rect.X, menu_.target.Y - rect.Y, 0, 0});
    auto visual = Hosting::ElementCompositionPreview::GetElementVisual(menu_.panel);
    auto compositor = visual.Compositor();
    if (!menu_.clip) {
        menu_.clip = compositor.CreateRoundedRectangleGeometry();
        menu_.visual_clip = compositor.CreateGeometricClip(menu_.clip);
    }
    menu_.visual_clip.Geometry(menu_.clip);
    menu_.clip.Size({rect.Width, rect.Height});
    menu_.clip.CornerRadius({static_cast<float>(menu_.radius), static_cast<float>(menu_.radius)});
    visual.Clip(menu_.visual_clip);
    auto rows = Hosting::ElementCompositionPreview::GetElementVisual(menu_.rows);
    rows.CenterPoint({menu_.target.Width / 2, menu_.above ? menu_.target.Height : 0, 0});
    const float scale = static_cast<float>(0.9 + 0.1 * menu_.content_opacity);
    rows.Scale({scale, scale, 1});
    if (menu_.source.hides) {
        if (auto button = menu_.source.button.get()) button.Opacity(1 - menu_.opacity);
    }
}

void animate(bool opening) {
    stop_rendering();
    menu_.closing = !opening;
    menu_.from = menu_.current;
    menu_.from_radius = menu_.radius;
    menu_.from_content_opacity = menu_.content_opacity;
    menu_.from_opacity = menu_.opacity;
    menu_.started = std::chrono::steady_clock::now();
    const auto to = opening ? menu_.target : menu_.anchor;
    const double radius = opening ? kMenuRadius : std::min(menu_.anchor.Height / 2.0, kMenuRadius);
    const auto curve = opening ? glass_motion::open : glass_motion::close;
    if (!motion::enabled()) {
        if (!opening) { finish(); return; }
        menu_.current = to;
        menu_.radius = radius;
        menu_.content_opacity = 1;
        menu_.opacity = 1;
        paint();
        return;
    }
    // Rendering 的下一帧开始展开，不插入定时器或人为等待；中断从当前几何状态接续。
    rendering_ = CompositionTarget::Rendering([](auto&&, auto&&) {
        advance(std::chrono::duration<double>(std::chrono::steady_clock::now() - menu_.started).count());
    });
}

void advance(double elapsed) {
    if (!menu_.panel) return;
    const bool opening = !menu_.closing;
    const auto to = opening ? menu_.target : menu_.anchor;
    const double radius = opening ? kMenuRadius : std::min(menu_.anchor.Height / 2.0, kMenuRadius);
    const auto curve = opening ? glass_motion::open : glass_motion::close;
    const double duration = spring::settle_time(curve);
    const double progress = elapsed >= duration ? 1 : spring::value(curve, elapsed);
    const auto lerp = [progress](double a, double b) { return static_cast<float>(a + (b - a) * progress); };
    menu_.current = {lerp(menu_.from.X, to.X), lerp(menu_.from.Y, to.Y),
                     lerp(menu_.from.Width, to.Width), lerp(menu_.from.Height, to.Height)};
    menu_.radius = lerp(menu_.from_radius, radius);
    menu_.content_opacity = std::clamp(static_cast<double>(lerp(menu_.from_content_opacity, opening ? 1 : 0)), 0.0, 1.0);
    if (!opening) {
        const double handoff = std::clamp((elapsed - glass_motion::handoff_delay) / glass_motion::handoff_duration, 0.0, 1.0);
        const double eased = handoff * handoff * (3 - 2 * handoff);
        menu_.opacity = menu_.from_opacity * (1 - eased);
    } else {
        menu_.opacity = std::clamp(static_cast<double>(lerp(menu_.from_opacity, 1)), 0.0, 1.0);
    }
    paint();
    if (!opening && elapsed >= glass_motion::handoff_delay + glass_motion::handoff_duration) finish();
    else if (opening && elapsed >= duration) {
        stop_rendering();
        menu_.visual_clip.Geometry(corners::geometry(
            Hosting::ElementCompositionPreview::GetElementVisual(menu_.panel).Compositor(),
            {menu_.current.Width, menu_.current.Height}, static_cast<float>(kMenuRadius)));
    }
}

void grow() {
    menu_.current = menu_.anchor;
    menu_.radius = std::min(menu_.anchor.Height / 2.0, kMenuRadius);
    menu_.content_opacity = 0;
    paint();
    animate(true);
}

}  // namespace

void attach(FrameworkElement const& root) {
    root_ = root;
    popup_ = Popup();
    // Inside the window's own composition: place() already keeps the menu within the window, and a windowed
    // popup (a separate HWND) did not show on a real desktop.
    popup_.ShouldConstrainToRootBounds(true);
    if (auto grid = root.try_as<Grid>()) {
        grid.Children().Append(popup_);
    }
    root.ActualThemeChanged([](auto&&, auto&&) {
        if (open_) close();
    });
    root.PointerPressed([](auto&&, Input::PointerRoutedEventArgs const& args) {
        if (!open_) return;
        const auto point = args.GetCurrentPoint(root_).Position();
        const auto anchor = menu_.anchor;
        if (point.X >= anchor.X && point.X <= anchor.X + anchor.Width &&
            point.Y >= anchor.Y && point.Y <= anchor.Y + anchor.Height) return;
        const auto panel = menu_.current;
        if (point.X >= panel.X && point.X <= panel.X + panel.Width &&
            point.Y >= panel.Y && point.Y <= panel.Y + panel.Height) return;
        close();
    });
    root.SizeChanged([](auto&&, auto&&) { if (open_) close(); });
    const auto verify_motion = [](motion_capture::Driver& driver) {
        finish();
        auto found = std::find_if(openers_.begin(), openers_.end(), [](const auto& opener) {
            auto button = opener.button.get();
            return button && button.IsLoaded() && button.ActualWidth() > 0 && button.ActualHeight() > 0;
        });
        if (found == openers_.end()) throw winrt::hresult_error(E_FAIL, L"动效场景缺少菜单按钮");
        if (!motion::enabled()) {
            found->open();
            if (!open_ || menu_.content_opacity != 1 || rendering_) {
                throw winrt::hresult_error(E_FAIL, L"减少动态效果时菜单没有立即展开");
            }
            close();
            if (menu_.panel || rendering_) throw winrt::hresult_error(E_FAIL, L"减少动态效果时菜单没有立即关闭");
            driver.note(L"PASS: reduced motion opens and closes immediately without a frame callback");
            return;
        }
        driver.note(L"geometry samples from the production animation clock; not captured pixel frames");
        driver.note(L"open response=0.32 damping=0.78; close response=0.26 damping=1; handoff delay=0.12 duration=0.08");
        found->open();
        for (int i = 0; i <= 60; ++i) advance(i / 120.0);
        if (!open_ || menu_.content_opacity != 1) throw winrt::hresult_error(E_FAIL, L"菜单展开未完成");
        const auto anchor = menu_.anchor;
        driver.note(L"opened size=" + std::to_wstring(menu_.current.Width) + L"x" + std::to_wstring(menu_.current.Height));
        close();
        for (int i = 0; i <= 24 && menu_.panel; ++i) {
            advance(i / 120.0);
            if (!menu_.panel) break;
            if (menu_.current.Width + 0.01 < anchor.Width || menu_.current.Height + 0.01 < anchor.Height) {
                throw winrt::hresult_error(E_FAIL, L"收起尺寸小于按钮");
            }
            if (auto button = menu_.source.button.get(); menu_.source.hides &&
                std::abs(button.Opacity() + menu_.opacity - 1) > 0.001) {
                throw winrt::hresult_error(E_FAIL, L"菜单交接不透明度不互补");
            }
            driver.note(L"sample=" + std::to_wstring(i / 120.0) + L"s size=" + std::to_wstring(menu_.current.Width) +
                L"x" + std::to_wstring(menu_.current.Height) + L" opacity=" + std::to_wstring(menu_.opacity));
        }
        if (menu_.panel) throw winrt::hresult_error(E_FAIL, L"透明菜单未清理");
        driver.note(L"PASS: no shrink undershoot, complementary handoff, transparent removal");
        found->open();
        advance(0.05);
        close();
        advance(0.03);
        const auto interrupted = menu_.current;
        found->open();
        if (std::abs(menu_.current.Width - interrupted.Width) > 0.01 ||
            std::abs(menu_.current.Height - interrupted.Height) > 0.01) {
            throw winrt::hresult_error(E_FAIL, L"中断后菜单尺寸跳变");
        }
        advance(0.5);
        finish();
        driver.note(L"PASS: closing can reopen from its current rectangle");
    };
    motion_capture::add(L"glass-menu", verify_motion);
    motion_capture::add(L"glass-menu-reduced", [verify_motion](motion_capture::Driver& driver) {
        motion::suspend(true);
        try {
            verify_motion(driver);
        } catch (...) {
            motion::suspend(false);
            throw;
        }
        motion::suspend(false);
    });
}

Controls::Button make_button(std::wstring title, std::function<std::vector<Entry>()> entries, bool hides) {
    auto button = winrt::make<winrt::MusicUnlock::implementation::GlassMenuButton>();
    Automation::AutomationProperties::SetName(button, title);
    auto open = [title = std::move(title), entries = std::move(entries), hides, weak = winrt::make_weak(button)] {
        if (auto self = weak.get()) show_entries(self, title, hides, entries);
    };
    button.Click([open](auto&&, auto&&) { open(); });
    auto state = winrt::get_self<winrt::MusicUnlock::implementation::GlassMenuButton>(button);
    state->expand = open;
    state->collapse = [] { close(); };
    std::erase_if(openers_, [](const auto& opener) { return !opener.button.get(); });
    openers_.push_back({winrt::make_weak(button.as<Controls::Button>()), std::move(open)});
    return button;
}

FrameworkElement make_picker(std::wstring title, std::vector<std::wstring> options, int selected, double minimum,
                             bool on_glass, std::function<void(int)> changed) {
    auto value = TextBlock();
    value.FontSize(on_glass ? 12 : 13);
    value.FontWeight(winrt::Windows::UI::Text::FontWeights::Medium());
    value.VerticalAlignment(VerticalAlignment::Center);
    FontIcon chevron;
    chevron.Glyph(glyph(L"\uE70D"));  // 上下箭头图标
    chevron.FontSize(9);
    chevron.Foreground(brush(L"MuSecondaryBrush"));
    chevron.VerticalAlignment(VerticalAlignment::Center);
    StackPanel content;
    content.Orientation(Orientation::Horizontal);
    content.Spacing(6);
    content.Children().Append(value);
    content.Children().Append(chevron);

    auto picker = std::make_shared<Picker>();
    picker->title = title;
    picker->options = std::move(options);
    picker->value = winrt::make_weak(value);
    auto button = make_button(title, [picker, changed] {
        std::vector<Entry> entries;
        for (int i = 0; i < static_cast<int>(picker->options.size()); ++i) {
            Entry entry;
            entry.text = picker->options[i];
            entry.checked = picker->selected == i;
            entry.action = [picker, changed, i] {
                if (auto button = picker->button.get()) select(button, i);
                if (changed) changed(i);
            };
            entries.push_back(std::move(entry));
        }
        return entries;
    });
    button.MinWidth(minimum);
    button.Padding(Thickness{12, on_glass ? 6.0 : 8.0, 12, on_glass ? 6.0 : 8.0});
    button.Content(content);
    button.Template(picker_template(on_glass));
    button.CornerRadius(CornerRadiusHelper::FromUniformRadius(14));
    picker->button = winrt::make_weak(button);
    std::erase_if(pickers_, [](const auto& item) { return !item->button.get(); });
    pickers_.push_back(picker);
    select(button, selected);
    return button;
}

void select(FrameworkElement const& picker, int index) {
    if (!picker || index < 0) return;
    for (const auto& state : pickers_) {
        auto button = state->button.get();
        if (!button || button.as<winrt::Windows::Foundation::IInspectable>() !=
                           picker.as<winrt::Windows::Foundation::IInspectable>()) continue;
        if (index >= static_cast<int>(state->options.size())) return;
        state->selected = index;
        if (auto value = state->value.get()) value.Text(state->options[index]);
        Automation::AutomationProperties::SetName(button, state->title + L"：" + state->options[index]);
        return;
    }
}

void show_entries(FrameworkElement const& button, std::wstring title, bool hides,
                   std::function<std::vector<Entry>()> entries) {
    if (Automation::AutomationProperties::GetName(button).empty()) Automation::AutomationProperties::SetName(button, title);
    auto current = menu_.source.button.get();
    if (current && current.as<winrt::Windows::Foundation::IInspectable>() ==
                       button.as<winrt::Windows::Foundation::IInspectable>()) {
        if (open_) close();
        else if (menu_.closing) {
            open_ = true;
            if (auto control = button.try_as<winrt::MusicUnlock::GlassMenuButton>()) {
                winrt::get_self<winrt::MusicUnlock::implementation::GlassMenuButton>(control)->Expanded(true);
            }
            menu_.panel.IsHitTestVisible(true);
            menu_.rows.Focus(FocusState::Programmatic);
            animate(true);
        }
        return;
    }
    show(Source{winrt::make_weak(button), hides}, entries());
}

void open_for_preview(FrameworkElement const& button) {
    if (!button) throw winrt::hresult_invalid_argument(L"玻璃菜单预览缺少按钮");
    bool matched = false;
    for (auto& opener : openers_) {
        auto stored = opener.button.get();
        if (stored && stored.as<winrt::Windows::Foundation::IInspectable>() ==
                          button.as<winrt::Windows::Foundation::IInspectable>()) {
            matched = true;
            opener.open();
            break;
        }
    }
    if (!matched || !open_) throw winrt::hresult_error(E_FAIL, L"玻璃菜单预览未打开");
    menu_.panel.UpdateLayout();
}

void close() {
    if (!open_) return;
    open_ = false;
    if (auto button = menu_.source.button.get()) {
        button.Focus(FocusState::Programmatic);
        if (auto control = button.try_as<winrt::MusicUnlock::GlassMenuButton>()) {
            winrt::get_self<winrt::MusicUnlock::implementation::GlassMenuButton>(control)->Expanded(false);
        }
    }
    menu_.panel.IsHitTestVisible(false);
    animate(false);
}

}  // namespace glass_menu
