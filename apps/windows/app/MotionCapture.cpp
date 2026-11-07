#include "pch.h"

#include "MotionCapture.h"

#include "Motion.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>

#include <winrt/Microsoft.UI.Xaml.Automation.Peers.h>
#include <winrt/Microsoft.UI.Xaml.Automation.Provider.h>
#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace motion_capture {

using namespace winrt::Microsoft::UI::Xaml;
using namespace winrt::Windows::Foundation::Numerics;

namespace {

struct Scenario {
    std::wstring name;
    std::function<void(Driver&)> run;
};

std::map<std::wstring, Scenario>& scenarios() {
    static std::map<std::wstring, Scenario> registered;
    return registered;
}

std::function<void(winrt::Windows::System::VirtualKey, winrt::Windows::System::VirtualKeyModifiers)>& key_handler() {
    static std::function<void(winrt::Windows::System::VirtualKey, winrt::Windows::System::VirtualKeyModifiers)> handler;
    return handler;
}

/// The control's own click handler: the same one a pointer release calls.
void invoke(UIElement const& element) {
    auto peer = Automation::Peers::FrameworkElementAutomationPeer::FromElement(element.try_as<FrameworkElement>());
    if (!peer) peer = Automation::Peers::FrameworkElementAutomationPeer::CreatePeerForElement(element);
    if (peer) {
        if (auto provider = peer.GetPattern(Automation::Peers::PatternInterface::Invoke)
                                .try_as<Automation::Provider::IInvokeProvider>()) {
            provider.Invoke();
        }
    }
}

std::wstring key_name(winrt::Windows::System::VirtualKey key) {
    switch (key) {
        case winrt::Windows::System::VirtualKey::Number1: return L"1";
        case winrt::Windows::System::VirtualKey::Number2: return L"2";
        case winrt::Windows::System::VirtualKey::Enter: return L"Enter";
        case winrt::Windows::System::VirtualKey::Escape: return L"Esc";
        default: return std::to_wstring(static_cast<int>(key));
    }
}

}  // namespace

struct Driver::State {
    std::filesystem::path log;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    std::function<void(winrt::Windows::System::VirtualKey, winrt::Windows::System::VirtualKeyModifiers)> on_key;
};

void set_key_handler(
    std::function<void(winrt::Windows::System::VirtualKey, winrt::Windows::System::VirtualKeyModifiers)> handler) {
    key_handler() = std::move(handler);
}

void add(std::wstring name, std::function<void(Driver&)> scenario) {
    scenarios().insert_or_assign(name, Scenario{name, std::move(scenario)});
}

void Driver::note(const std::wstring& text) const {
    const auto at = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - state_->started);
    std::wofstream out(state_->log, std::ios::app);
    out << L"t=" << at.count() << L"ms  " << text << L"\n";
}

void Driver::click(UIElement const& element) {
    press(element);
    release(element);
}

void Driver::press(UIElement const& element) {
    note(L"press");
    if (auto control = element.try_as<Controls::Control>()) {
        control.Focus(FocusState::Programmatic);
        VisualStateManager::GoToState(control, L"Pressed", false);
    }
}

void Driver::release(UIElement const& element) {
    note(L"release");
    if (auto control = element.try_as<Controls::Control>()) VisualStateManager::GoToState(control, L"Normal", false);
    invoke(element);
}

void Driver::drag(UIElement const&, float2 from, float2 to, int ms) {
    note(L"drag (" + std::to_wstring(from.x) + L"," + std::to_wstring(from.y) + L") -> (" + std::to_wstring(to.x) + L"," +
         std::to_wstring(to.y) + L") over " + std::to_wstring(ms) + L" ms");
}

void Driver::key(winrt::Windows::System::VirtualKey key, winrt::Windows::System::VirtualKeyModifiers modifiers) {
    std::wstring text = L"key ";
    if ((static_cast<unsigned>(modifiers) & static_cast<unsigned>(winrt::Windows::System::VirtualKeyModifiers::Control)) != 0)
        text += L"Ctrl+";
    if ((static_cast<unsigned>(modifiers) & static_cast<unsigned>(winrt::Windows::System::VirtualKeyModifiers::Shift)) != 0)
        text += L"Shift+";
    note(text + key_name(key));
    if (state_->on_key) state_->on_key(key, modifiers);
}

void Driver::wait(int ms) {
    note(L"wait " + std::to_wstring(ms) + L" ms");
    Sleep(static_cast<DWORD>(ms));
}

void Driver::capture(int ms) { note(L"capture " + std::to_wstring(ms) + L" ms"); }

void Driver::name(std::wstring element) { motion::describe(std::move(element)); }

void run(FrameworkElement const& root, std::wstring requested, std::wstring directory) {
    std::vector<std::wstring> names;
    if (requested == L"all") {
        for (const auto& [name, scenario] : scenarios()) names.push_back(name);
    } else {
        for (size_t start = 0; start <= requested.size();) {
            const auto end = requested.find(L',', start);
            auto name = requested.substr(start, end == std::wstring::npos ? std::wstring::npos : end - start);
            if (!name.empty()) names.push_back(std::move(name));
            if (end == std::wstring::npos) break;
            start = end + 1;
        }
    }
    const std::filesystem::path folder = std::filesystem::path(directory) / L"motion";
    std::filesystem::create_directories(folder);
    for (const auto& name : names) {
        const auto found = scenarios().find(name);
        if (found == scenarios().end()) continue;
        for (const auto [theme_name, theme] : {std::pair{L"light", ElementTheme::Light}, std::pair{L"dark", ElementTheme::Dark}}) {
            auto state = std::make_shared<Driver::State>();
            state->log = folder / (name + L"-" + theme_name + L".log");
            state->on_key = key_handler();
            std::filesystem::remove(state->log);
            SetEnvironmentVariableW(L"MUSICUNLOCK_MOTION_LOG", state->log.c_str());
            root.RequestedTheme(theme);
            root.UpdateLayout();
            Driver driver(state);
            driver.note(std::wstring(L"theme ") + theme_name);
            found->second.run(driver);
        }
    }
    SetEnvironmentVariableW(L"MUSICUNLOCK_MOTION_LOG", nullptr);
    Application::Current().Exit();
}

}  // namespace motion_capture
