/**
 * @file Collapsible.hpp
 * @brief Section repliable (Textual « Collapsible ») : un titre cliquable
 * « ▶ Titre » / « ▼ Titre » suivi du contenu quand elle est dépliée.
 * Entrée, Espace ou un clic sur le titre basculent l'état.
 * CSS : propriété « title-color » ; « Collapsible:focus » pour le titre.
 */

#pragma once

#include "../widget/Container.hpp"
#include "../widget/Paint.hpp"

#include <functional>
#include <string>

namespace tui {

class Collapsible : public Container {
public:
    explicit Collapsible(std::string title = {}, bool collapsed = true)
        : title_(std::move(title)), collapsed_(collapsed) {}

    void set_content(std::shared_ptr<Widget> w) {
        children_.clear();
        if (w) add_child(std::move(w));
    }
    void set_title(std::string t) { title_ = std::move(t); need_repaint(); }
    [[nodiscard]] const std::string& title() const { return title_; }
    void set_collapsed(bool c) {
        if (c == collapsed_) return;
        collapsed_ = c;
        if (on_toggle_) on_toggle_(collapsed_);
        need_repaint();
    }
    [[nodiscard]] bool collapsed() const { return collapsed_; }
    void toggle() { set_collapsed(!collapsed_); }
    void set_on_toggle(std::function<void(bool collapsed)> cb) { on_toggle_ = std::move(cb); }
    void set_symbols(std::string collapsed, std::string expanded) {
        collapsed_symbol_ = std::move(collapsed);
        expanded_symbol_ = std::move(expanded);
        need_repaint();
    }

    /// Replié : le contenu n'est ni focusable, ni cliquable.
    [[nodiscard]] std::vector<std::shared_ptr<Widget>> children() const override {
        if (collapsed_) return {};
        return Container::children();
    }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        int w = paint::width(collapsed_symbol_ + " " + title_) + 1;
        int h = 1;
        if (!collapsed_ && child_count() > 0) {
            Size s = child_at(0).widget->measure_outer(Constraints{0, c.max_width, 0, std::max(0, c.max_height - 1)});
            w = std::max(w, s.width);
            h += s.height;
        }
        return c.clamp({std::max(w, c.max_width < (1 << 20) ? c.max_width : w), h});
    }

    void arrange(Rect r) override {
        bounds_ = r;
        if (child_count() > 0)
            child_at(0).widget->layout(collapsed_ ? Rect{r.x, r.y + 1, 0, 0}
                                                  : Rect{r.x, r.y + 1, r.width, std::max(0, r.height - 1)});
    }

    void paint(Buffer& b) const override {
        if (bounds_.empty()) return;
        ClipGuard clip(b, bounds_);
        Color tc = style().extra_color("title-color").value_or(Color::Default());
        TextStyle st = has_focus() ? (styled() ? TextStyle::Bold : TextStyle::Bold | TextStyle::Reverse) : TextStyle::Bold;
        paint::text(b, bounds_.x, bounds_.y, bounds_.width,
                    (collapsed_ ? collapsed_symbol_ : expanded_symbol_) + " " + title_, tc, Color::Default(), st);
        if (!collapsed_ && child_count() > 0 && child_at(0).widget->visible()) child_at(0).widget->render(b);
    }

    [[nodiscard]] bool focusable() const override { return true; }
    void on_focus() override { need_repaint(); }
    void on_blur() override { need_repaint(); }

    bool on_key(const KeyEvent& e) override {
        if (e.key == Key::Enter || (e.key == Key::Char && e.codepoint == U' ')) {
            toggle();
            return true;
        }
        if (e.key == Key::Left && !collapsed_) { set_collapsed(true); return true; }
        if (e.key == Key::Right && collapsed_) { set_collapsed(false); return true; }
        return false;
    }

    bool on_mouse(const MouseEvent& e) override {
        if (e.button == MouseEvent::Button::Left && e.action == MouseEvent::Action::Press && e.y == bounds_.y) {
            toggle();
            return true;
        }
        return false;
    }

private:
    std::string title_;
    bool collapsed_;
    std::string collapsed_symbol_ = "▶", expanded_symbol_ = "▼";
    std::function<void(bool)> on_toggle_;
};

} // namespace tui
