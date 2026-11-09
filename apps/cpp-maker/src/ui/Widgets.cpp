/* @file ui/Widgets.cpp */

#include "ui/Widgets.hpp"

#include "Theme.hpp"

#include <tui/core/Cell.hpp>
#include <tui/core/Event.hpp>
#include <tui/core/Text.hpp>
#include <tui/core/TextStyle.hpp>

#include <algorithm>

namespace ui {

using tui::Cell;
using tui::Color;
using tui::Key;
using tui::TextHelper;
using tui::TextStyle;

namespace {

/// Écrit `text` à partir de (x, y), tronqué à `max_w` colonnes ; renvoie la largeur écrite.
int put_text(tui::Buffer& b, int x, int y, int max_w, const std::string& text, Color fg, Color bg, TextStyle st) {
    if (max_w <= 0) return 0;
    auto decoded = TextHelper::truncate_to_width(TextHelper::decode_utf8(text), max_w);
    int w = 0;
    for (char32_t ch : decoded) {
        int cw = TextHelper::display_width(std::u32string(1, ch));
        if (cw <= 0) cw = 1;
        b.set(x + w, y, Cell{ch, static_cast<uint8_t>(cw), fg, bg, st});
        w += cw;
    }
    return w;
}

} // namespace

// ============================================================================
// Tons
// ============================================================================

const char* tone_class(Tone t) {
    switch (t) {
    case Tone::Accent: return "-accent";
    case Tone::Info: return "-info";
    case Tone::Muted: return "-muted";
    case Tone::Success: return "-success";
    case Tone::Warning: return "-warning";
    case Tone::Error: return "-error";
    case Tone::Normal: break;
    }
    return "";
}

void set_tone(tui::Widget& w, Tone t) {
    w.remove_class("-accent -info -muted -success -warning -error");
    if (t != Tone::Normal) w.add_class(tone_class(t));
}

tui::Color tone_color(const tui::Widget& w, Tone t) {
    using namespace cpp_maker;
    const char* prop = nullptr;
    Color fallback = Color::Default();
    switch (t) {
    case Tone::Normal: return Color::Default();
    case Tone::Accent: prop = "accent-color"; fallback = kAccent; break;
    case Tone::Info: prop = "info-color"; fallback = kInfo; break;
    case Tone::Muted: prop = "muted-color"; fallback = kMuted; break;
    case Tone::Success: prop = "success-color"; fallback = kSuccess; break;
    case Tone::Warning: prop = "warning-color"; fallback = kWarning; break;
    case Tone::Error: prop = "error-color"; fallback = kError; break;
    }
    if (!w.styled()) return fallback;
    return w.style().extra_color(prop).value_or(fallback);
}

// ============================================================================
// NavTree
// ============================================================================

void NavTree::set_items(std::vector<NavItem> items) {
    std::string keep = selected_id();
    items_ = std::move(items);
    selected_ = 0;
    select_id(keep);
    need_repaint();
}

void NavTree::select_id(const std::string& id) {
    for (size_t i = 0; i < items_.size(); ++i)
        if (items_[i].id == id) {
            selected_ = i;
            ensure_selected_visible();
            need_repaint();
            return;
        }
    if (selected_ >= items_.size()) selected_ = 0;
}

std::string NavTree::selected_id() const { return selected_ < items_.size() ? items_[selected_].id : std::string(); }

tui::Size NavTree::measure(const tui::Constraints& c) const { return c.clamp({c.max_width, c.max_height}); }

void NavTree::arrange(tui::Rect r) {
    bounds_ = r;
    ensure_selected_visible();
}

void NavTree::ensure_selected_visible() {
    int h = std::max(bounds_.height, 1);
    int sel = static_cast<int>(selected_);
    if (sel < scroll_) scroll_ = sel;
    if (sel >= scroll_ + h) scroll_ = sel - h + 1;
    scroll_ = std::clamp(scroll_, 0, std::max(0, static_cast<int>(items_.size()) - h));
}

void NavTree::paint(tui::Buffer& b) const {
    tui::ClipGuard clip(b, bounds_);
    // Ligne courante : selon le CSS (cursor-background…) ou palette historique.
    RowCursor cur = row_cursor(focused_);
    bool css = styled() && style().extra_color("cursor-background").has_value();
    Color hover_bg = style().extra_color("hover-background").value_or(Color::Default());
    Color hint_fg = tone_color(*this, Tone::Muted);
    int hover = hovered() ? hover_ : -1;
    for (int row = 0; row < bounds_.height; ++row) {
        size_t idx = static_cast<size_t>(scroll_ + row);
        int y = bounds_.y + row;
        if (idx >= items_.size()) break;
        const auto& it = items_[idx];
        bool sel = idx == selected_;
        Color fg = tone_color(*this, it.tone), bg = Color::Default();
        TextStyle st = TextStyle::None;
        if (sel && css) {
            if (cur.active) {
                fg = cur.fg;
                bg = cur.bg;
                st = cur.style;
            }
        } else if (sel) {
            fg = focused_ ? Color::White() : fg;
            bg = focused_ ? (accent_.is_default ? cpp_maker::kBadgeBg : accent_) : Color{50, 48, 70};
            st = TextStyle::Bold;
        } else if (static_cast<int>(idx) == hover) {
            bg = hover_bg;
        }
        if (!bg.is_default || st != TextStyle::None)
            for (int x = 0; x < bounds_.width; ++x) b.set(bounds_.x + x, y, Cell{U' ', 1, fg, bg, st});
        // Marqueur de sélection à gauche (barre d'accent), comme les listes Textual.
        int x = bounds_.x + it.depth * 2;
        if (sel && css && focused_)
            b.set(bounds_.x, y, Cell{U'▌', 1, style().extra_color("marker-color").value_or(tone_color(*this, Tone::Accent)), bg,
                                     TextStyle::None});
        int w = put_text(b, x + (css ? 1 : 0), y, bounds_.right() - x - (css ? 1 : 0), it.label, fg, bg, st);
        if (!it.hint.empty()) {
            int hx = x + (css ? 1 : 0) + w + 1;
            Color hc = sel && cur.active && css ? cur.fg : sel && focused_ ? Color::White() : hint_fg;
            put_text(b, hx, y, bounds_.right() - hx, it.hint, hc, bg, css ? TextStyle::Italic : TextStyle::Dim);
        }
    }
}

void NavTree::move_to(long index) {
    if (items_.empty()) return;
    index = std::clamp<long>(index, 0, static_cast<long>(items_.size()) - 1);
    if (static_cast<size_t>(index) == selected_) return;
    selected_ = static_cast<size_t>(index);
    ensure_selected_visible();
    need_repaint();
    if (on_select_) on_select_(items_[selected_].id);
}

bool NavTree::on_key(const tui::KeyEvent& e) {
    long s = static_cast<long>(selected_);
    long page = std::max(1, bounds_.height - 1);
    switch (e.key) {
    case Key::Up: move_to(s - 1); return true;
    case Key::Down: move_to(s + 1); return true;
    case Key::PageUp: move_to(s - page); return true;
    case Key::PageDown: move_to(s + page); return true;
    case Key::Home: move_to(0); return true;
    case Key::End: move_to(static_cast<long>(items_.size()) - 1); return true;
    case Key::Enter:
        if (on_activate_ && selected_ < items_.size()) on_activate_(items_[selected_].id);
        return true;
    default: return false;
    }
}

bool NavTree::on_mouse(const tui::MouseEvent& e) {
    using MB = tui::MouseEvent::Button;
    if (e.button == MB::WheelUp) {
        scroll_ = std::max(0, scroll_ - 3);
        need_repaint();
        return true;
    }
    if (e.button == MB::WheelDown) {
        scroll_ = std::min(std::max(0, static_cast<int>(items_.size()) - bounds_.height), scroll_ + 3);
        need_repaint();
        return true;
    }
    long idx = scroll_ + (e.y - bounds_.y);
    bool valid = idx >= 0 && idx < static_cast<long>(items_.size());
    if (e.action == tui::MouseEvent::Action::Move) {
        int h = valid ? static_cast<int>(idx) : -1;
        if (h == hover_) return false;
        hover_ = h;
        need_repaint();
        return true;
    }
    if (e.button == MB::Left && e.action == tui::MouseEvent::Action::Press) {
        if (!valid) return true;
        move_to(idx);
        // Double-clic : comme Entrée (aller au formulaire, indexer…).
        if (e.clicks >= 2 && on_activate_) on_activate_(items_[static_cast<size_t>(idx)].id);
        return true;
    }
    return false;
}

// ============================================================================
// TextBlock
// ============================================================================

tui::Size TextBlock::measure(const tui::Constraints& c) const {
    int lines = 0;
    for (auto& para : [&] {
             std::vector<std::string> v;
             size_t start = 0;
             while (true) {
                 size_t e = text_.find('\n', start);
                 v.push_back(text_.substr(start, e == std::string::npos ? std::string::npos : e - start));
                 if (e == std::string::npos) break;
                 start = e + 1;
             }
             return v;
         }())
        lines += std::max<int>(1, static_cast<int>(TextHelper::wrap_to_width(TextHelper::decode_utf8(para), std::max(c.max_width, 1)).size()));
    return c.clamp({c.max_width, lines});
}

void TextBlock::paint(tui::Buffer& b) const {
    tui::ClipGuard clip(b, bounds_);
    int y = bounds_.y;
    size_t start = 0;
    while (y < bounds_.bottom()) {
        size_t e = text_.find('\n', start);
        std::string para = text_.substr(start, e == std::string::npos ? std::string::npos : e - start);
        auto lines = TextHelper::wrap_to_width(TextHelper::decode_utf8(para), std::max(bounds_.width, 1));
        if (lines.empty()) lines.emplace_back();
        for (auto& l : lines) {
            if (y >= bounds_.bottom()) break;
            int x = bounds_.x;
            for (char32_t ch : l) b.set(x++, y, Cell{ch, 1, Color::Default(), Color::Default(), TextStyle::None});
            ++y;
        }
        if (e == std::string::npos) break;
        start = e + 1;
    }
}

// ============================================================================
// ChoiceField
// ============================================================================

void ChoiceField::set_choices(std::vector<std::string> values, std::vector<std::string> labels) {
    std::string keep = value();
    values_ = std::move(values);
    labels_ = std::move(labels);
    if (labels_.size() != values_.size()) labels_ = values_;
    index_ = 0;
    set_value(keep);
}

void ChoiceField::set_value(const std::string& v) {
    auto it = std::find(values_.begin(), values_.end(), v);
    if (it == values_.end() && !v.empty()) {
        // Valeur hors liste (fichier édité à la main) : conservée telle quelle.
        values_.push_back(v);
        labels_.push_back(v + " (?)");
        it = values_.end() - 1;
    }
    index_ = it == values_.end() ? 0 : static_cast<size_t>(it - values_.begin());
    need_repaint();
}

std::string ChoiceField::value() const { return index_ < values_.size() ? values_[index_] : std::string(); }

tui::Size ChoiceField::measure(const tui::Constraints& c) const {
    int w = 4;
    for (auto& l : labels_) w = std::max(w, TextHelper::display_width(TextHelper::decode_utf8(l)) + 4);
    return c.clamp({w, 1});
}

void ChoiceField::paint(tui::Buffer& b) const {
    if (bounds_.height <= 0) return;
    std::string label = index_ < labels_.size() ? labels_[index_] : "";
    // Stylé : fond et texte viennent du CSS (ChoiceField, :focus, :hover) ;
    // les chevrons prennent « arrow-color ».
    Color fg = Color::Default(), bg = Color::Default(), arrow = Color::Default();
    if (styled()) {
        arrow = style().extra_color("arrow-color").value_or(Color::Default());
    } else {
        fg = focused_ ? Color::White() : Color::Default();
        bg = focused_ ? (accent_.is_default ? cpp_maker::kBadgeBg : accent_) : Color{40, 38, 56};
        arrow = fg;
    }
    for (int x = 0; x < bounds_.width; ++x) b.set(bounds_.x + x, bounds_.y, Cell{U' ', 1, fg, bg, TextStyle::None});
    int w = put_text(b, bounds_.x, bounds_.y, bounds_.width, "‹ ", arrow, bg, TextStyle::Bold);
    w += put_text(b, bounds_.x + w, bounds_.y, bounds_.width - w - 2, label, fg, bg, focused_ ? TextStyle::Bold : TextStyle::None);
    put_text(b, std::max(bounds_.x + w, bounds_.right() - 2), bounds_.y, 2, " ›", arrow, bg, TextStyle::Bold);
}

void ChoiceField::on_focus() {
    focused_ = true;
    need_repaint();
    if (on_focus_cb_) on_focus_cb_();
}

void ChoiceField::step(int delta) {
    if (values_.empty()) return;
    long n = static_cast<long>(values_.size());
    index_ = static_cast<size_t>(((static_cast<long>(index_) + delta) % n + n) % n);
    need_repaint();
    if (on_change_) on_change_(value());
}

bool ChoiceField::on_key(const tui::KeyEvent& e) {
    if (e.key == Key::Left) { step(-1); return true; }
    if (e.key == Key::Right || e.key == Key::Enter || (e.key == Key::Char && e.codepoint == U' ')) {
        step(1);
        return true;
    }
    return false;
}

bool ChoiceField::on_mouse(const tui::MouseEvent& e) {
    if (e.button == tui::MouseEvent::Button::Left && e.action == tui::MouseEvent::Action::Press) {
        step(e.x < bounds_.x + bounds_.width / 2 ? -1 : 1);
        return true;
    }
    return false;
}

} // namespace ui
