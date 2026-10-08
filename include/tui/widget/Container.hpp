/**
 * @file Container.hpp
 * @brief Widget composite générique portant une liste d'enfants + leurs LayoutParams.
 *
 * Container n'implémente PAS measure()/arrange() (toujours différés
 * aux sous-classes concrètes comme Vertical/Grid/Border...) : chaque
 * algorithme de mise en page est propre à son conteneur. Ce que
 * Container fournit est commun à tous : gestion de la liste d'enfants
 * et peinture clippée aux bornes du conteneur.
 */

#pragma once

#include "../Buffer.hpp"
#include "Layout.hpp"
#include "Widget.hpp"

#include <algorithm>

namespace tui {

class Container : public Widget {
public:
    void add_child(std::shared_ptr<Widget> child, LayoutParams params = {}) {
        child->set_parent(this);
        children_.push_back({std::move(child), params});
        need_repaint();
    }

    void remove_child(const std::shared_ptr<Widget>& child) {
        auto it = std::remove_if(children_.begin(), children_.end(),
            [&](const Entry& e) { return e.widget == child; });
        if (it == children_.end()) return;
        for (auto e = it; e != children_.end(); ++e) e->widget->set_parent(nullptr);
        children_.erase(it, children_.end());
        need_repaint();
    }

    [[nodiscard]] std::vector<std::shared_ptr<Widget>> children() const override {
        std::vector<std::shared_ptr<Widget>> result;
        result.reserve(children_.size());
        for (const auto& e : children_) result.push_back(e.widget);
        return result;
    }

    /// Peint chaque enfant visible, clippé aux bornes de ce conteneur.
    /// Les conteneurs qui ont besoin d'un ordre/dessin spécial (ex:
    /// Tabs ne peint que la page active) surchargent paint() entièrement.
    void paint(Buffer& buffer) const override {
        ClipGuard clip(buffer, bounds_);
        for (const auto& e : children_) {
            if (e.widget->visible()) e.widget->paint(buffer);
        }
    }

protected:
    struct Entry {
        std::shared_ptr<Widget> widget;
        LayoutParams params;
    };

    [[nodiscard]] size_t child_count() const { return children_.size(); }
    [[nodiscard]] Entry& child_at(size_t i) { return children_[i]; }
    [[nodiscard]] const Entry& child_at(size_t i) const { return children_[i]; }

    std::vector<Entry> children_;
};

} // namespace tui
