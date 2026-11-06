#pragma once
// Development renders (MUSICUNLOCK_RENDER_DIR, scripts/screenshot.py): a stand-in for the system text size,
// which a render must not change on the machine, and a dump of what UI Automation (Narrator) sees.
// MainWindow::SavePreview captures the PNGs; SimulateHighContrast there stands in for high contrast.

#include <filesystem>

namespace preview {

namespace xaml = winrt::Microsoft::UI::Xaml;

/// Text size (Settings > Accessibility > Text size): multiplies the font size of every TextBlock in the tree.
void scale_text(xaml::DependencyObject const& root, double factor);

/// One line per UI Automation element under `root`: control type, name, keyboard focus, live setting.
/// List rows appear as the list reports them to Narrator, named by the item (JobItem::ToString).
void write_automation_tree(xaml::UIElement const& root, std::filesystem::path const& file);

}  // namespace preview
