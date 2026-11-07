#pragma once

#include "GlassMenuButton.g.h"
#include <functional>
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>

namespace winrt::MusicUnlock::implementation {

struct GlassMenuButton : GlassMenuButtonT<GlassMenuButton> {
    GlassMenuButton() = default;
    Microsoft::UI::Xaml::Automation::Peers::AutomationPeer OnCreateAutomationPeer();
    void Expanded(bool value);

    bool menu_item = false;
    bool checkable = false;
    bool selected = false;
    bool expanded = false;
    std::function<void()> expand;
    std::function<void()> collapse;
};

}

namespace winrt::MusicUnlock::factory_implementation {
struct GlassMenuButton : GlassMenuButtonT<GlassMenuButton, implementation::GlassMenuButton> {};
}
