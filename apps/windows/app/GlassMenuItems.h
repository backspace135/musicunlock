#pragma once

#include "GlassMenuItems.g.h"
#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>

namespace winrt::MusicUnlock::implementation {
struct GlassMenuItems : GlassMenuItemsT<GlassMenuItems> {
    GlassMenuItems() = default;
    Microsoft::UI::Xaml::Automation::Peers::AutomationPeer OnCreateAutomationPeer();
};
}

namespace winrt::MusicUnlock::factory_implementation {
struct GlassMenuItems : GlassMenuItemsT<GlassMenuItems, implementation::GlassMenuItems> {};
}
