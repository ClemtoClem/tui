/**
 * @file Link.hpp
 * @brief Lien cliquable (Textual « Link ») : texte souligné, activable au
 * clic, par Entrée ou Espace ; « :hover » et « :focus » stylables en CSS.
 */

#pragma once

#include "../widget/Paint.hpp"
#include "../widget/Widget.hpp"

#include <functional>
#include <string>

namespace tui {

class Link : public Widget {
public:
    explicit Link(std::string text = {}, std::string url = {}) : text_(std::move(text)), url_(std::move(url)) {}

    void set_text(std::string t) { text_ = std::move(t); need_repaint(); }
    [[nodiscard]] const std::string& text() const { return text_; }
    void set_url(std::string u) { url_ = std::move(u); }
    [[nodiscard]] const std::string& url() const { return url_; }
    /// Appelé avec l'URL à l'activation.
    void set_on_click(std::function<void(const std::string&)> cb) { on_click_ = std::move(cb); }

    [[nodiscard]] Size measure(const Constraints& c) const override { return c.clamp({paint::width(text_), 1}); }

    void paint(Buffer& b) const override {
        if (bounds_.empty()) return;
        TextStyle st = styled() ? TextStyle::None : TextStyle::Underline;
        if (!styled() && (has_focus() || hovered())) st = st | TextStyle::Bold;
        paint::text(b, bounds_.x, bounds_.y, bounds_.width, text_, Color::Default(), Color::Default(), st);
    }

    [[nodiscard]] bool focusable() const override { return true; }
    void on_focus() override { need_repaint(); }
    void on_blur() override { need_repaint(); }

    bool on_key(const KeyEvent& e) override {
        if (e.key == Key::Enter || (e.key == Key::Char && e.codepoint == U' ')) {
            activate();
            return true;
        }
        return false;
    }
    bool on_mouse(const MouseEvent& e) override {
        if (e.button == MouseEvent::Button::Left && e.action == MouseEvent::Action::Press) {
            activate();
            return true;
        }
        return false;
    }
    void activate() {
        if (on_click_) on_click_(url_);
    }

private:
    std::string text_, url_;
    std::function<void(const std::string&)> on_click_;
};

} // namespace tui
