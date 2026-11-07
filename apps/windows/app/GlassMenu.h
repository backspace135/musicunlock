#pragma once
// The window's one glass menu (issue #63), matching apps/macos GlassMenu.swift.
//
// A menu is a window-level Popup, so it floats over every panel and the title bar instead of being clipped by
// them. Only one is open. It grows out of the button that opened it and shrinks back into that button.

#include <functional>
#include <string>
#include <vector>

#include <winrt/Microsoft.UI.Xaml.h>

namespace glass_menu {

namespace xaml = winrt::Microsoft::UI::Xaml;

/// One row. `action` runs after the menu has started closing; notes and dividers have none.
struct Entry {
    enum class Kind { item, note, divider } kind = Kind::item;
    std::wstring text;
    bool checked = false;
    bool enabled = true;
    std::function<void()> action;
};

/// Attaches the shared menu to the window's root. Call once, after the content is loaded.
void attach(xaml::FrameworkElement const& root);

/// The button a menu grows out of. `title` is its accessible name ("音乐来源：网易云音乐"), `entries` is read
/// each time it opens. While its menu is open the button is hidden, unless `hides` is false (the title bar
/// draws the button, so the menu grows from behind it).
xaml::Controls::Button make_button(std::wstring title, std::function<std::vector<Entry>()> entries, bool hides = true);

/// A capsule showing the current value of a list, with the up/down chevron. `on_glass` paints an opaque
/// surface so it does not stack glass on a glass card. `minimum` keeps neighbours still as the value changes
/// length. `changed` runs with the chosen index.
xaml::FrameworkElement make_picker(std::wstring title, std::vector<std::wstring> options, int selected, double minimum,
                                   bool on_glass, std::function<void(int)> changed);

/// Points an existing picker at a new selection without notifying `changed` (the model updated the control).
void select(xaml::FrameworkElement const& picker, int index);

/// Opens `button` as a menu. The title bar account button already exists, so it is not made here; `hides` is
/// false because the title bar draws that button and the menu grows from behind it.
void show_entries(xaml::FrameworkElement const& button, std::wstring title, bool hides,
                  std::function<std::vector<Entry>()> entries);

/// Closes whatever is open. A window losing activation and a preview render both need this.
void close();

}  // namespace glass_menu
