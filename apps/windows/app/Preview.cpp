#include "pch.h"

#include "Preview.h"

#include <fstream>
#include <string>

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>

namespace preview {

namespace {

using namespace winrt;
using namespace winrt::Microsoft::UI::Xaml;
using Media::VisualTreeHelper;

/// The control view Narrator walks: peers that are control elements, nested through GetChildren.
void describe_peer(Automation::Peers::AutomationPeer const& peer, int depth, std::wstring& out) {
    int children_depth = depth;
    if (peer.IsControlElement()) {
        std::wstring line = std::wstring(peer.GetLocalizedControlType()) + L"「" + std::wstring(peer.GetName()) + L"」";
        if (peer.IsKeyboardFocusable()) line += L" [Tab]";
        if (!peer.IsEnabled()) line += L" [不可用]";
        if (peer.GetLiveSetting() == Automation::Peers::AutomationLiveSetting::Polite) line += L" [live: polite]";
        if (peer.GetLiveSetting() == Automation::Peers::AutomationLiveSetting::Assertive) line += L" [live: assertive]";
        out += std::wstring(depth * 2, L' ') + line + L"\n";
        children_depth = depth + 1;
    }
    if (auto children = peer.GetChildren()) {
        for (const auto& child : children) describe_peer(child, children_depth, out);
    }
}

/// Panels have no peers; walk down to the first elements that do.
void describe(DependencyObject const& node, std::wstring& out) {
    if (auto element = node.try_as<UIElement>()) {
        if (element.Visibility() == Visibility::Collapsed) return;
        if (auto peer = Automation::Peers::FrameworkElementAutomationPeer::CreatePeerForElement(element)) {
            return describe_peer(peer, 0, out);
        }
    }
    const int32_t count = VisualTreeHelper::GetChildrenCount(node);
    for (int32_t i = 0; i < count; ++i) describe(VisualTreeHelper::GetChild(node, i), out);
}

}  // namespace

void scale_text(DependencyObject const& root, double factor) {
    if (auto text = root.try_as<Controls::TextBlock>()) text.FontSize(text.FontSize() * factor);
    const int32_t count = VisualTreeHelper::GetChildrenCount(root);
    for (int32_t i = 0; i < count; ++i) scale_text(VisualTreeHelper::GetChild(root, i), factor);
}

void write_automation_tree(UIElement const& root, std::filesystem::path const& file) {
    std::wstring text;
    describe(root, text);
    std::ofstream(file, std::ios::binary) << to_string(text);
}

}  // namespace preview
