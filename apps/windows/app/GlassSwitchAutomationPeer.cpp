#include "pch.h"
#include "GlassSwitch.h"
#include "GlassSwitchAutomationPeer.h"

namespace winrt::MusicUnlock::implementation {

Windows::Foundation::IInspectable GlassSwitchAutomationPeer::GetPatternCore(peers::PatternInterface const& pattern) const {
    if (pattern == peers::PatternInterface::Selection) return static_cast<providers::ISelectionProvider>(*this);
    return FrameworkElementAutomationPeerT::GetPatternCore(pattern);
}

Windows::Foundation::Collections::IVector<peers::AutomationPeer> GlassSwitchAutomationPeer::GetChildrenCore() {
    return get_self<GlassSwitch>(Owner().as<MusicUnlock::GlassSwitch>())->SegmentPeers();
}

com_array<providers::IRawElementProviderSimple> GlassSwitchAutomationPeer::GetSelection() {
    const auto control = Owner().as<MusicUnlock::GlassSwitch>();
    const auto children = GetChildrenCore();
    const auto index = control.SelectedIndex();
    if (index < 0 || index >= static_cast<int32_t>(children.Size())) return {};
    return {ProviderFromPeer(children.GetAt(index))};
}

Windows::Foundation::IInspectable GlassSegmentAutomationPeer::GetPatternCore(peers::PatternInterface const& pattern) const {
    if (pattern == peers::PatternInterface::SelectionItem) return static_cast<providers::ISelectionItemProvider>(*this);
    return FrameworkElementAutomationPeerT::GetPatternCore(pattern);
}

hstring GlassSegmentAutomationPeer::GetNameCore() const {
    if (auto control = control_.get()) return get_self<GlassSwitch>(control)->SegmentName(index_);
    return {};
}

int32_t GlassSegmentAutomationPeer::GetSizeOfSetCore() const {
    if (auto control = control_.get()) return get_self<GlassSwitch>(control)->SegmentCount();
    return 0;
}

void GlassSegmentAutomationPeer::Select() {
    if (auto control = control_.get(); control && control.IsEnabled())
        get_self<GlassSwitch>(control)->SelectFromPeer(index_);
}

bool GlassSegmentAutomationPeer::IsSelected() const {
    if (auto control = control_.get()) return control.SelectedIndex() == index_;
    return false;
}

providers::IRawElementProviderSimple GlassSegmentAutomationPeer::SelectionContainer() {
    if (auto control = control_.get())
        return ProviderFromPeer(peers::FrameworkElementAutomationPeer::CreatePeerForElement(control));
    return nullptr;
}

}  // namespace winrt::MusicUnlock::implementation
