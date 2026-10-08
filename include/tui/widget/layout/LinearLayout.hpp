/**
 * @file LinearLayout.hpp
 * @brief Algorithme de mise en page flex-lite partagé par Vertical et Horizontal.
 *
 * Détail d'implémentation interne (pas dans le catalogue public du
 * framework) : Vertical et Horizontal sont deux sous-classes fines
 * qui ne diffèrent que par l'axe. Évite de dupliquer l'algorithme.
 */

#pragma once

#include "../Container.hpp"

namespace tui {

class LinearLayout : public Container {
protected:
    explicit LinearLayout(bool vertical) : vertical_(vertical) {}

public:
    [[nodiscard]] Size measure(const Constraints& constraints) const override {
        int primary_max = primary_of(constraints.max_width, constraints.max_height);
        int cross_max = cross_of(constraints.max_width, constraints.max_height);

        int reserved = 0;
        int cross_needed = 0;

        for (size_t i = 0; i < child_count(); ++i) {
            const auto& entry = child_at(i);
            if (entry.params.mode == LayoutMode::Fixed) {
                reserved += std::max(entry.params.value, 0);
                Size s = entry.widget->measure(loose_constraint(entry.params.value, cross_max));
                cross_needed = std::max(cross_needed, cross_of(s.width, s.height));
            } else if (entry.params.mode == LayoutMode::Auto) {
                Size s = entry.widget->measure(loose_constraint(primary_max, cross_max));
                reserved += primary_of(s.width, s.height);
                cross_needed = std::max(cross_needed, cross_of(s.width, s.height));
            } else { // Stretch : ne réserve rien en measure(), juste sa préférence cross
                Size s = entry.widget->measure(loose_constraint(0, cross_max));
                cross_needed = std::max(cross_needed, cross_of(s.width, s.height));
            }
        }

        int primary = std::min(reserved, primary_max);
        int cross = std::min(cross_needed, cross_max);
        return vertical_ ? constraints.clamp({cross, primary}) : constraints.clamp({primary, cross});
    }

    void arrange(Rect final_rect) override {
        bounds_ = final_rect;
        int primary_total = primary_of(final_rect.width, final_rect.height);
        int cross_total = cross_of(final_rect.width, final_rect.height);

        std::vector<int> primary_sizes(child_count(), 0);
        int reserved = 0;
        int weight_sum = 0;

        for (size_t i = 0; i < child_count(); ++i) {
            const auto& entry = child_at(i);
            if (entry.params.mode == LayoutMode::Fixed) {
                primary_sizes[i] = std::max(entry.params.value, 0);
                reserved += primary_sizes[i];
            } else if (entry.params.mode == LayoutMode::Auto) {
                Size s = entry.widget->measure(loose_constraint(primary_total, cross_total));
                primary_sizes[i] = primary_of(s.width, s.height);
                reserved += primary_sizes[i];
            } else {
                weight_sum += std::max(entry.params.value, 0);
            }
        }

        int remaining = std::max(primary_total - reserved, 0);
        if (weight_sum > 0) {
            int distributed = 0;
            int last_stretch_index = -1;
            for (size_t i = 0; i < child_count(); ++i) {
                if (child_at(i).params.mode != LayoutMode::Stretch) continue;
                int weight = std::max(child_at(i).params.value, 0);
                int size = (weight > 0) ? (remaining * weight) / weight_sum : 0;
                primary_sizes[i] = size;
                distributed += size;
                last_stretch_index = static_cast<int>(i);
            }
            if (last_stretch_index >= 0) {
                primary_sizes[static_cast<size_t>(last_stretch_index)] += remaining - distributed;
            }
        }

        // taille totale du contenu, puis clamp du décalage.
        content_primary_size_ = 0;
        for (int s : primary_sizes) content_primary_size_ += s;
        int max_scroll = std::max(content_primary_size_ - primary_total, 0);
        scroll_offset_ = std::clamp(scroll_offset_, 0, max_scroll);

        // le premier enfant démarre à -scroll_offset_.
        int offset = -scroll_offset_;
        for (size_t i = 0; i < child_count(); ++i) {
            auto& entry = child_at(i);
            int primary_size = primary_sizes[i];

            int cross_size = cross_total;
            int cross_offset = 0;
            if (entry.params.cross_align != CrossAlign::Stretch) {
                Size natural = entry.widget->measure(loose_constraint(primary_size, cross_total));
                cross_size = std::min(cross_of(natural.width, natural.height), cross_total);
                switch (entry.params.cross_align) {
                    case CrossAlign::Start: cross_offset = 0; break;
                    case CrossAlign::Center: cross_offset = (cross_total - cross_size) / 2; break;
                    case CrossAlign::End: cross_offset = cross_total - cross_size; break;
                    case CrossAlign::Stretch: break; // inatteignable ici
                }
            }

            entry.widget->arrange(make_rect(final_rect, offset, primary_size, cross_offset, cross_size));
            offset += primary_size;
        }
    }

private:
    [[nodiscard]] int primary_of(int width, int height) const { return vertical_ ? height : width; }
    [[nodiscard]] int cross_of(int width, int height) const { return vertical_ ? width : height; }

    [[nodiscard]] Constraints loose_constraint(int primary_max, int cross_max) const {
        return vertical_ ? Constraints{0, cross_max, 0, primary_max} : Constraints{0, primary_max, 0, cross_max};
    }

    [[nodiscard]] Rect make_rect(Rect container, int primary_offset, int primary_size, int cross_offset, int cross_size) const {
        if (vertical_) {
            return Rect{container.x + cross_offset, container.y + primary_offset, cross_size, primary_size};
        }
        return Rect{container.x + primary_offset, container.y + cross_offset, primary_size, cross_size};
    }

    bool vertical_;
    
    /// Décalage de défilement sur l'axe principal (0 = début, en haut
    /// ou à gauche). Borné à [0, contenu - vue]. Ignoré si le contenu
    /// tient dans la vue.
    int scroll_offset_ = 0;

    /// Taille totale du contenu sur l'axe principal, calculée à chaque
    /// arrange(). Sert à borner scroll_offset_ et à décider si un
    /// défilement est nécessaire.
    int content_primary_size_ = 0;
};

} // namespace tui
