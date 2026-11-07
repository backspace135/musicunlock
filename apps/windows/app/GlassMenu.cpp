#include "pch.h"

#include "GlassMenu.h"

#include <algorithm>
#include <cmath>

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>
#include <winrt/Microsoft.UI.Xaml.Controls.Primitives.h>
#include <winrt/Microsoft.UI.Xaml.Input.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>
#include <winrt/Microsoft.UI.Xaml.Hosting.h>
#include <winrt/Microsoft.UI.Xaml.Shapes.h>
#include <winrt/Windows.UI.Text.h>

#include "Corners.h"
#include "GlassMotion.h"
#include "GlassSurface.h"
#include "Motion.h"

namespace glass_menu {
namespace {

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Microsoft::UI::Xaml::Controls;
using namespace winrt::Microsoft::UI::Xaml::Controls::Primitives;
using namespace winrt::Microsoft::UI::Xaml::Media;
using winrt::Windows::Foundation::Numerics::float2;

constexpr double kGap = 6;       // between the button and the menu
constexpr double kMargin = 8;    // kept clear of the window edges
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
    FrameworkElement panel{nullptr};  // the glass, sized and positioned over the window
    FrameworkElement rows{nullptr};   // measured once, so the panel knows where it is growing to
    winrt::Windows::Foundation::Rect anchor{};
    winrt::Windows::Foundation::Rect target{};
    bool above = false;
    int highlighted = -1;
};

void highlight(int index);
void choose(int index);
void grow();

FrameworkElement root_{nullptr};
Popup popup_{nullptr};
Menu menu_;
bool open_ = false;

winrt::hstring glyph(const wchar_t* text) { return winrt::hstring(text); }

Brush brush(const wchar_t* key) {
    return Application::Current().Resources().Lookup(winrt::box_value(winrt::hstring(key))).as<Brush>();
}

/// The button's rectangle in the root's coordinate space.
winrt::Windows::Foundation::Rect anchor_of(FrameworkElement const& button) {
    const auto at = button.TransformToVisual(root_).TransformPoint({0, 0});
    return {static_cast<float>(at.X), static_cast<float>(at.Y), static_cast<float>(button.ActualWidth()),
            static_cast<float>(button.ActualHeight())};
}

void place(Menu& menu) {
    const auto window = winrt::Windows::Foundation::Size{static_cast<float>(root_.ActualWidth()),
                                                        static_cast<float>(root_.ActualHeight())};
    const double width = menu.rows.ActualWidth();
    const double height = menu.rows.ActualHeight();
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
            mark.Text(glyph(L"\uE73E"));  // CheckMark
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
        row.IsHitTestVisible(entry.enabled);
        const int index = static_cast<int>(rows.Children().Size());
        row.PointerEntered([index](auto&&, auto&&) { highlight(index); });
        row.PointerExited([index](auto&&, auto&&) {
            if (menu_.highlighted == index) highlight(-1);
        });
        row.Tapped([index](auto&&, Input::TappedRoutedEventArgs const& args) {
            args.Handled(true);
            choose(index);
        });
        rows.Children().Append(row);
    }
    return rows;
}

/// The one highlight, sliding between rows. -1 clears it.
void highlight(int index) {
    if (!open_ || !menu_.rows) return;
    menu_.highlighted = index;
    auto stack = menu_.rows.try_as<StackPanel>();
    if (!stack) return;
    for (uint32_t i = 0; i < stack.Children().Size(); ++i) {
        auto row = stack.Children().GetAt(i).try_as<Grid>();
        if (!row) continue;
        const bool on = static_cast<int>(i) == index;
        if (!on) {
            row.Background(nullptr);
            continue;
        }
        // 8 % of the text colour behind the glyphs.
        auto colour = brush(L"MuTextBrush").as<SolidColorBrush>().Color();
        colour.A = 20;
        row.Background(SolidColorBrush(colour));
    }
}

void choose(int index) {
    if (!open_ || index < 0 || index >= static_cast<int>(menu_.entries.size())) return;
    auto action = menu_.entries[index].action;
    close();
    if (action) action();
}

void show(Source source, std::vector<Entry> entries) {
    if (!popup_ || !source.button.get()) return;
    menu_ = {};
    menu_.source = std::move(source);
    menu_.entries = std::move(entries);
    const auto button = menu_.source.button.get();
    menu_.anchor = anchor_of(button);

    auto surface = winrt::make<winrt::MusicUnlock::implementation::GlassSurface>();
    surface.Kind(winrt::MusicUnlock::GlassKind::Menu);
    surface.Radius(kMenuRadius);
    menu_.rows = build_rows(menu_);
    surface.Content(menu_.rows);
    menu_.panel = surface;
    // Measure off screen: the panel has to know its final size before it starts growing.
    menu_.rows.Measure({10000, 10000});
    menu_.rows.Arrange({0, 0, menu_.rows.DesiredSize().Width, menu_.rows.DesiredSize().Height});
    place(menu_);

    popup_.Child(menu_.panel);
    popup_.HorizontalOffset(menu_.anchor.X);
    popup_.VerticalOffset(menu_.anchor.Y);
    menu_.panel.Width(menu_.anchor.Width);
    menu_.panel.Height(menu_.anchor.Height);
    popup_.IsOpen(true);
    open_ = true;
    if (menu_.source.hides) button.Opacity(0);
    menu_.panel.Focus(FocusState::Programmatic);
    grow();
}

/// Moves the panel between its button and its final place. `opening` uses the slightly overshooting curve;
/// closing uses the critically damped one, so the panel never shrinks inside the button.
void animate(bool opening) {
    const auto& from = opening ? menu_.anchor : menu_.target;
    const auto& to = opening ? menu_.target : menu_.anchor;
    if (!motion::enabled()) {
        menu_.panel.Width(to.Width);
        menu_.panel.Height(to.Height);
        popup_.HorizontalOffset(to.X);
        popup_.VerticalOffset(to.Y);
        return;
    }
    auto visual = Hosting::ElementCompositionPreview::GetElementVisual(menu_.panel);
    Hosting::ElementCompositionPreview::SetIsTranslationEnabled(menu_.panel, true);
    // The panel stays where the popup put it; the visual's translation covers the rest of the travel, and the
    // size animates on the element. Both start from the current value, so an interrupted menu does not jump.
    visual.Properties().InsertVector3(L"Translation", {static_cast<float>(from.X - menu_.anchor.X),
                                                      static_cast<float>(from.Y - menu_.anchor.Y), 0});
    auto compositor = visual.Compositor();
    auto move = glass_motion::vector3(compositor, opening ? glass_motion::open : glass_motion::close, L"Translation",
                                      {static_cast<float>(to.X - menu_.anchor.X),
                                       static_cast<float>(to.Y - menu_.anchor.Y), 0});
    if (move) visual.StartAnimation(L"Translation", move);
    menu_.panel.Width(to.Width);
    menu_.panel.Height(to.Height);
}

void grow() { animate(true); }

}  // namespace

void attach(FrameworkElement const& root) {
    root_ = root;
    popup_ = Popup();
    popup_.ShouldConstrainToRootBounds(false);
    if (auto grid = root.try_as<Grid>()) {
        grid.Children().Append(popup_);
    }
    root.ActualThemeChanged([](auto&&, auto&&) {
        if (open_) close();
    });
}

Controls::Button make_button(std::wstring title, std::function<std::vector<Entry>()> entries, bool hides) {
    Controls::Button button;
    Automation::AutomationProperties::SetName(button, title);
    button.Click([title = std::move(title), entries = std::move(entries), hides,
                  weak = winrt::make_weak(button)](auto&&, auto&&) {
        if (auto self = weak.get()) show_entries(self, title, hides, entries);
    });
    return button;
}

FrameworkElement make_picker(std::wstring title, std::vector<std::wstring> options, int selected, double minimum,
                             bool on_glass, std::function<void(int)> changed) {
    auto value = TextBlock();
    value.FontSize(on_glass ? 12 : 13);
    value.FontWeight(winrt::Windows::UI::Text::FontWeights::Medium());
    value.VerticalAlignment(VerticalAlignment::Center);
    FontIcon chevron;
    chevron.Glyph(glyph(L"\uE70D"));  // ScrollChevronUpDown
    chevron.FontSize(9);
    chevron.Foreground(brush(L"MuSecondaryBrush"));
    chevron.VerticalAlignment(VerticalAlignment::Center);
    StackPanel content;
    content.Orientation(Orientation::Horizontal);
    content.Spacing(6);
    content.Children().Append(value);
    content.Children().Append(chevron);

    auto button = make_button(title, [options, changed, weak = winrt::make_weak(value)] {
        std::vector<Entry> entries;
        const auto current = weak.get() ? std::wstring(weak.get().Text()) : std::wstring{};
        for (int i = 0; i < static_cast<int>(options.size()); ++i) {
            Entry entry;
            entry.text = options[i];
            entry.checked = current == options[i];
            entry.action = [changed, i] { if (changed) changed(i); };
            entries.push_back(std::move(entry));
        }
        return entries;
    });
    button.MinWidth(minimum);
    button.Padding(Thickness{12, on_glass ? 6.0 : 8.0, 12, on_glass ? 6.0 : 8.0});
    button.Content(content);
    if (on_glass) {
        button.Background(brush(L"MuSurfaceBrush"));
        button.BorderBrush(brush(L"MuSeparatorBrush"));
        button.BorderThickness(ThicknessHelper::FromUniformLength(1));
    } else {
        button.Background(brush(L"MuSurfaceBrush"));
        button.BorderThickness(ThicknessHelper::FromUniformLength(0));
    }
    button.CornerRadius(CornerRadiusHelper::FromUniformRadius(14));
    std::vector<winrt::hstring> labels;
    for (const auto& option : options) labels.emplace_back(option);
    button.Tag(winrt::single_threaded_vector<winrt::hstring>(std::move(labels)));
    select(button, selected);
    return button;
}

void select(FrameworkElement const& picker, int index) {
    auto button = picker.try_as<Controls::Button>();
    auto content = button ? button.Content().try_as<StackPanel>() : nullptr;
    auto value = content && content.Children().Size() ? content.Children().GetAt(0).try_as<TextBlock>() : nullptr;
    if (!value || index < 0) return;
    // The button rebuilds its entries from this text, so the text is the selection.
    const auto options = button.Tag().try_as<winrt::Windows::Foundation::Collections::IVector<winrt::hstring>>();
    if (!options || index >= static_cast<int>(options.Size())) return;
    value.Text(options.GetAt(index));
}

void show_entries(FrameworkElement const& button, std::wstring title, bool hides,
                  std::function<std::vector<Entry>()> entries) {
    Automation::AutomationProperties::SetName(button, title);
    if (open_ && menu_.source.button.get() == button) {
        close();
        return;
    }
    show(Source{winrt::make_weak(button), hides}, entries());
}

void close() {
    if (!open_) return;
    open_ = false;
    auto button = menu_.source.button.get();
    if (!motion::enabled()) {
        if (button) button.Opacity(1);
        popup_.IsOpen(false);
        popup_.Child(nullptr);
        menu_ = {};
        return;
    }
    animate(false);
    // The panel and the button cross-fade where they meet, starting 0.12 s into the shrink.
    auto timer = menu_.panel.DispatcherQueue().CreateTimer();
    timer.Interval(std::chrono::milliseconds(static_cast<int>(glass_motion::handoff_delay * 1000)));
    timer.Tick([button, timer](auto&&, auto&&) {
        timer.Stop();
        if (button) button.Opacity(1);
        auto visual = Hosting::ElementCompositionPreview::GetElementVisual(menu_.panel);
        auto fade = visual.Compositor().CreateScalarKeyFrameAnimation();
        fade.InsertKeyFrame(0, 1);
        fade.InsertKeyFrame(1, 0);
        fade.Duration(std::chrono::milliseconds(static_cast<int>(glass_motion::handoff_duration * 1000)));
        visual.StartAnimation(L"Opacity", fade);
        auto done = menu_.panel.DispatcherQueue().CreateTimer();
        done.Interval(fade.Duration());
        done.Tick([done](auto&&, auto&&) {
            done.Stop();
            popup_.IsOpen(false);
            popup_.Child(nullptr);
            menu_ = {};
        });
        done.Start();
    });
    timer.Start();
}

}  // namespace glass_menu
