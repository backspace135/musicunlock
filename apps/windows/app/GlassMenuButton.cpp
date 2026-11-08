#include "pch.h"
#include <UIAutomation.h>

#include "GlassMenuButton.h"
#include "GlassMenuItems.h"
#include "GlassMenuButton.g.cpp"
#include "GlassMenuItems.g.cpp"

#include <winrt/Microsoft.UI.Xaml.Automation.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>

namespace winrt::MusicUnlock::implementation {
namespace {

using namespace Microsoft::UI::Xaml;
using namespace Automation;
using namespace Automation::Peers;
using namespace Automation::Provider;

struct MenuButtonPeer : ButtonAutomationPeerT<MenuButtonPeer, IExpandCollapseProvider, IToggleProvider> {
    explicit MenuButtonPeer(MusicUnlock::GlassMenuButton const& owner) : ButtonAutomationPeerT(owner) {}

    GlassMenuButton* State() { return get_self<GlassMenuButton>(Owner().as<MusicUnlock::GlassMenuButton>()); }
    AutomationControlType GetAutomationControlTypeCore() {
        return State()->menu_item ? AutomationControlType::MenuItem : AutomationControlType::Button;
    }
    hstring GetClassNameCore() { return State()->menu_item ? L"GlassMenuItem" : L"GlassMenuButton"; }
    Windows::Foundation::Collections::IVector<AutomationPeer> GetChildrenCore() {
        // 行内容已由菜单项的名称和模式完整表达，避免重复朗读文字和字体图标。
        if (State()->menu_item) return single_threaded_vector<AutomationPeer>();
        return ButtonAutomationPeerT::GetChildrenCore();
    }
    IInspectable GetPatternCore(PatternInterface pattern) {
        if (pattern == PatternInterface::ExpandCollapse && !State()->menu_item) return *this;
        if (pattern == PatternInterface::Toggle && State()->checkable) return *this;
        return ButtonAutomationPeerT::GetPatternCore(pattern);
    }
    Automation::ExpandCollapseState ExpandCollapseState() {
        return State()->expanded ? Automation::ExpandCollapseState::Expanded : Automation::ExpandCollapseState::Collapsed;
    }
    void Expand() {
        if (!IsEnabled()) throw hresult_error(UIA_E_ELEMENTNOTENABLED);
        if (!State()->expanded && State()->expand) State()->expand();
    }
    void Collapse() {
        if (State()->expanded && State()->collapse) State()->collapse();
    }
    Automation::ToggleState ToggleState() { return State()->selected ? Automation::ToggleState::On : Automation::ToggleState::Off; }
    void Toggle() {
        // 菜单勾选由实际点击路径更新，避免无障碍状态与业务选择分离。
        GetPattern(PatternInterface::Invoke).as<IInvokeProvider>().Invoke();
    }
};

struct MenuItemsPeer : FrameworkElementAutomationPeerT<MenuItemsPeer> {
    explicit MenuItemsPeer(FrameworkElement const& owner) : FrameworkElementAutomationPeerT(owner) {}
    AutomationControlType GetAutomationControlTypeCore() { return AutomationControlType::Menu; }
    hstring GetClassNameCore() { return L"GlassMenu"; }
    bool IsControlElementCore() { return true; }
    bool IsContentElementCore() { return true; }
};

}

Microsoft::UI::Xaml::Automation::Peers::AutomationPeer GlassMenuButton::OnCreateAutomationPeer() {
    return make<MenuButtonPeer>(*this);
}

void GlassMenuButton::Expanded(bool value) {
    if (expanded == value) return;
    expanded = value;
    if (auto peer = FrameworkElementAutomationPeer::FromElement(*this)) {
        peer.RaisePropertyChangedEvent(ExpandCollapsePatternIdentifiers::ExpandCollapseStateProperty(),
            box_value(value ? ExpandCollapseState::Collapsed : ExpandCollapseState::Expanded),
            box_value(value ? ExpandCollapseState::Expanded : ExpandCollapseState::Collapsed));
    }
}

Microsoft::UI::Xaml::Automation::Peers::AutomationPeer GlassMenuItems::OnCreateAutomationPeer() {
    return make<MenuItemsPeer>(*this);
}

}
