/* @file src/Theme.cpp */

#include "Theme.hpp"

#include <algorithm>
#include <cctype>
#include <string>

namespace yocto {

tui::Theme make_planor_theme() {
    tui::Theme t;
    t.set(tui::ColorRole::Background,     kBackground);
    t.set(tui::ColorRole::Foreground,     kFg);
    t.set(tui::ColorRole::Border,         kBorder);
    t.set(tui::ColorRole::BorderFocused,  kAccent);
    t.set(tui::ColorRole::Selection,      kAccent);
    t.set(tui::ColorRole::SelectionText,  tui::Color::White());
    t.set(tui::ColorRole::Accent,         kBadgeBg);
    t.set(tui::ColorRole::Error,          kError);
    t.set(tui::ColorRole::Warning,        kWarning);
    t.set(tui::ColorRole::Success,        kSuccess);
    t.set(tui::ColorRole::Info,           kInfo);
    t.set(tui::ColorRole::Disabled,       tui::Color{100, 100, 110});
    t.set(tui::ColorRole::Muted,          kMuted);
    t.set_focus_style(tui::TextStyle::Bold);
    return t;
}

tui::Color status_color(int exit_code) {
    return exit_code == 0 ? kSuccess : kError;
}

tui::Color log_level_color(const std::string& level) {
    std::string upper = level;
    std::transform(upper.begin(), upper.end(), upper.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });

    if (upper == "ERROR" || upper == "FATAL") return kError;
    if (upper == "WARNING")                  return kWarning;
    if (upper == "NOTE")                     return kInfo;
    if (upper == "DEBUG")                    return kMuted;
    return kMuted;
}

} // namespace yocto