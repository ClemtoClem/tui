#include <tui/core/Theme.hpp>

namespace tui {

const Theme& Theme::dark() {
    static const Theme theme = [] {
        Theme t;
        t.set(ColorRole::Background, Color{0, 0, 0});
        t.set(ColorRole::Foreground, Color{220, 220, 220});
        t.set(ColorRole::Border, Color{90, 90, 90});
        t.set(ColorRole::BorderFocused, Color::Cyan());
        t.set(ColorRole::Selection, Color{38, 79, 120});
        t.set(ColorRole::SelectionText, Color::White());
        t.set(ColorRole::Accent, Color::Blue());
        t.set(ColorRole::Error, Color::Red());
        t.set(ColorRole::Warning, Color::Yellow());
        t.set(ColorRole::Success, Color::Green());
        t.set(ColorRole::Info, Color::Cyan());
        t.set(ColorRole::Disabled, Color::Gray());
        t.set(ColorRole::Muted, Color{130, 130, 140});
        t.set_focus_style(TextStyle::Bold);
        return t;
    }();
    return theme;
}

const Theme& Theme::light() {
    static const Theme theme = [] {
        Theme t;
        t.set(ColorRole::Background, Color::White());
        t.set(ColorRole::Foreground, Color{30, 30, 30});
        t.set(ColorRole::Border, Color{160, 160, 160});
        t.set(ColorRole::BorderFocused, Color::Blue());
        t.set(ColorRole::Selection, Color{200, 220, 245});
        t.set(ColorRole::SelectionText, Color::Black());
        t.set(ColorRole::Accent, Color::Blue());
        t.set(ColorRole::Error, Color::Red());
        t.set(ColorRole::Warning, Color{150, 110, 0});
        t.set(ColorRole::Success, Color{0, 120, 0});
        t.set(ColorRole::Info, Color{0, 110, 130});
        t.set(ColorRole::Disabled, Color::Gray());
        t.set(ColorRole::Muted, Color{110, 110, 110});
        t.set_focus_style(TextStyle::Bold);
        return t;
    }();
    return theme;
}

const Theme& Theme::planor() {
    static const Theme theme = [] {
        Theme t;
        t.set(ColorRole::Background, Color{13, 12, 18});
        t.set(ColorRole::Foreground, Color{225, 225, 232});
        t.set(ColorRole::Border, Color{124, 111, 240});
        t.set(ColorRole::BorderFocused, Color{124, 111, 240});
        t.set(ColorRole::Selection, Color{217, 87, 190});
        t.set(ColorRole::SelectionText, Color{217, 87, 190});
        t.set(ColorRole::Accent, Color{92, 84, 224});
        t.set(ColorRole::Error, Color{224, 90, 90});
        t.set(ColorRole::Warning, Color{224, 180, 90});
        t.set(ColorRole::Success, Color{110, 200, 140});
        t.set(ColorRole::Info, Color{124, 111, 240});
        t.set(ColorRole::Disabled, Color{100, 100, 110});
        t.set(ColorRole::Muted, Color{128, 128, 140});
        t.set_focus_style(TextStyle::Bold);
        return t;
    }();
    return theme;
}

} // namespace tui
