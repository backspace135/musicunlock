#pragma once

#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>

namespace winrt::MusicUnlock::implementation {

namespace peers = Microsoft::UI::Xaml::Automation::Peers;
namespace providers = Microsoft::UI::Xaml::Automation::Provider;

// Provider 必须由 implements 生成 ABI；直接继承投影接口不会实现 QueryInterface。
struct GlassSwitchAutomationPeer : peers::FrameworkElementAutomationPeerT<GlassSwitchAutomationPeer,
                                                                        providers::ISelectionProvider> {
    explicit GlassSwitchAutomationPeer(Microsoft::UI::Xaml::FrameworkElement const& owner)
        : FrameworkElementAutomationPeerT(owner) {}

    hstring GetClassNameCore() const { return L"GlassSwitch"; }
    peers::AutomationControlType GetAutomationControlTypeCore() const { return peers::AutomationControlType::List; }
    peers::AutomationOrientation GetOrientationCore() const { return peers::AutomationOrientation::Horizontal; }
    Windows::Foundation::IInspectable GetPatternCore(peers::PatternInterface const& pattern) const;
    Windows::Foundation::Collections::IVector<peers::AutomationPeer> GetChildrenCore();
    com_array<providers::IRawElementProviderSimple> GetSelection();
    bool CanSelectMultiple() const { return false; }
    bool IsSelectionRequired() const { return true; }
};

struct GlassSegmentAutomationPeer : peers::FrameworkElementAutomationPeerT<GlassSegmentAutomationPeer,
                                                                         providers::ISelectionItemProvider> {
    GlassSegmentAutomationPeer(Microsoft::UI::Xaml::FrameworkElement const& owner,
                               MusicUnlock::GlassSwitch const& control, int32_t index)
        : FrameworkElementAutomationPeerT(owner), control_(make_weak(control)), index_(index) {}

    hstring GetClassNameCore() const { return L"GlassSegment"; }
    hstring GetNameCore() const;
    peers::AutomationControlType GetAutomationControlTypeCore() const { return peers::AutomationControlType::ListItem; }
    bool IsKeyboardFocusableCore() const { return false; }
    int32_t GetPositionInSetCore() const { return index_ + 1; }
    int32_t GetSizeOfSetCore() const;
    Windows::Foundation::IInspectable GetPatternCore(peers::PatternInterface const& pattern) const;
    Windows::Foundation::Collections::IVector<peers::AutomationPeer> GetChildrenCore() const { return nullptr; }
    void Select();
    void AddToSelection() { Select(); }
    void RemoveFromSelection() {}
    bool IsSelected() const;
    providers::IRawElementProviderSimple SelectionContainer();

private:
    weak_ref<MusicUnlock::GlassSwitch> control_;
    int32_t index_;
};

}  // namespace winrt::MusicUnlock::implementation
