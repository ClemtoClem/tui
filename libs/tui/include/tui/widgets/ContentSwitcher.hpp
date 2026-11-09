/**
 * @file ContentSwitcher.hpp
 * @brief N'affiche qu'un enfant à la fois, choisi par son id CSS (Textual
 * « ContentSwitcher »). Les autres enfants ne sont ni peints, ni focusables.
 */

#pragma once

#include "../widget/Container.hpp"

#include <string>

namespace tui {

class ContentSwitcher : public Container {
public:
    /// Ajoute une page ; `id` remplace l'id CSS de l'enfant s'il est donné.
    void add_page(std::shared_ptr<Widget> w, std::string id = {}) {
        if (!id.empty()) w->set_id(std::move(id));
        if (current_.empty()) current_ = w->id();
        add_child(std::move(w));
    }
    /// Page affichée (id) ; false si aucun enfant ne porte cet id.
    bool set_current(const std::string& id) {
        for (auto& e : children_)
            if (e.widget->id() == id) {
                current_ = id;
                need_repaint();
                return true;
            }
        return false;
    }
    [[nodiscard]] const std::string& current() const { return current_; }
    [[nodiscard]] std::shared_ptr<Widget> current_widget() const {
        for (auto& e : children_)
            if (e.widget->id() == current_) return e.widget;
        return nullptr;
    }

    [[nodiscard]] std::vector<std::shared_ptr<Widget>> children() const override {
        if (auto w = current_widget()) return {w};
        return {};
    }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        if (auto w = current_widget()) return c.clamp(w->measure_outer(c));
        return c.clamp({0, 0});
    }
    void arrange(Rect r) override {
        bounds_ = r;
        if (auto w = current_widget()) w->layout(r);
    }
    void paint(Buffer& b) const override {
        ClipGuard clip(b, bounds_);
        if (auto w = current_widget(); w && w->visible()) w->render(b);
    }

private:
    std::string current_;
};

} // namespace tui
