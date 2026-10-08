/**
 * @file Grid.hpp
 * @brief Grille CSS-lite : pistes de lignes/colonnes fixes ou pondérées (fr), placement explicite.
 *
 * N'hérite pas de Container : le modèle enfant+LayoutParams ne colle
 * pas au placement (row,col,span) d'une grille, donc Grid gère sa
 * propre liste enfants+placement plutôt que de laisser le champ
 * children_ de Container inutilisé.
 */

#pragma once

#include "../../Buffer.hpp"
#include "../Widget.hpp"

#include <algorithm>

namespace tui {

struct GridTrack {
    enum class Mode { Fixed, Fraction } mode = Mode::Fraction;
    int value = 1;

    [[nodiscard]] static constexpr GridTrack fixed(int size) { return {Mode::Fixed, size}; }
    [[nodiscard]] static constexpr GridTrack fr(int weight = 1) { return {Mode::Fraction, weight}; }
};

struct GridPlacement {
    int row = 0;
    int col = 0;
    int row_span = 1;
    int col_span = 1;
};

class Grid : public Widget {
public:
    void set_rows(std::vector<GridTrack> rows) { rows_ = std::move(rows); need_repaint(); }
    void set_columns(std::vector<GridTrack> cols) { cols_ = std::move(cols); need_repaint(); }

    void add_child(std::shared_ptr<Widget> child, GridPlacement placement) {
        child->set_parent(this);
        entries_.push_back({std::move(child), placement});
        need_repaint();
    }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        return c.clamp({c.max_width, c.max_height}); // remplit toujours l'espace donné
    }

    void arrange(Rect final_rect) override {
        bounds_ = final_rect;
        auto col_offsets = track_offsets(cols_, final_rect.width);
        auto row_offsets = track_offsets(rows_, final_rect.height);
        if (col_offsets.size() < 2 || row_offsets.size() < 2) return; // pas de pistes définies

        int max_col = static_cast<int>(col_offsets.size()) - 2;
        int max_row = static_cast<int>(row_offsets.size()) - 2;

        for (auto& e : entries_) {
            int col = std::clamp(e.placement.col, 0, max_col);
            int row = std::clamp(e.placement.row, 0, max_row);
            int col_end = std::clamp(col + e.placement.col_span, col + 1, static_cast<int>(col_offsets.size()) - 1);
            int row_end = std::clamp(row + e.placement.row_span, row + 1, static_cast<int>(row_offsets.size()) - 1);

            int x = final_rect.x + col_offsets[static_cast<size_t>(col)];
            int y = final_rect.y + row_offsets[static_cast<size_t>(row)];
            int w = col_offsets[static_cast<size_t>(col_end)] - col_offsets[static_cast<size_t>(col)];
            int h = row_offsets[static_cast<size_t>(row_end)] - row_offsets[static_cast<size_t>(row)];
            e.widget->arrange(Rect{x, y, std::max(w, 0), std::max(h, 0)});
        }
    }

    void paint(Buffer& buffer) const override {
        ClipGuard clip(buffer, bounds_);
        for (auto& e : entries_) {
            if (e.widget->visible()) e.widget->paint(buffer);
        }
    }

    [[nodiscard]] std::vector<std::shared_ptr<Widget>> children() const override {
        std::vector<std::shared_ptr<Widget>> result;
        result.reserve(entries_.size());
        for (auto& e : entries_) result.push_back(e.widget);
        return result;
    }

private:
    struct Entry {
        std::shared_ptr<Widget> widget;
        GridPlacement placement;
    };

    /// Retourne N+1 offsets cumulés (offsets[0]==0, offsets[N]==total),
    /// un par frontière de piste ; vide si `tracks` est vide.
    [[nodiscard]] static std::vector<int> track_offsets(const std::vector<GridTrack>& tracks, int total) {
        if (tracks.empty()) return {};

        int fixed_sum = 0;
        int fr_sum = 0;
        for (const auto& t : tracks) {
            if (t.mode == GridTrack::Mode::Fixed) fixed_sum += std::max(t.value, 0);
            else fr_sum += std::max(t.value, 0);
        }
        int remaining = std::max(total - fixed_sum, 0);

        std::vector<int> sizes(tracks.size(), 0);
        int distributed = 0;
        int last_fr_index = -1;
        for (size_t i = 0; i < tracks.size(); ++i) {
            if (tracks[i].mode == GridTrack::Mode::Fixed) {
                sizes[i] = std::max(tracks[i].value, 0);
            } else {
                int weight = std::max(tracks[i].value, 0);
                int size = fr_sum > 0 ? (remaining * weight) / fr_sum : 0;
                sizes[i] = size;
                distributed += size;
                last_fr_index = static_cast<int>(i);
            }
        }
        if (last_fr_index >= 0) sizes[static_cast<size_t>(last_fr_index)] += remaining - distributed;

        std::vector<int> offsets;
        offsets.reserve(tracks.size() + 1);
        offsets.push_back(0);
        int cursor = 0;
        for (int s : sizes) { cursor += s; offsets.push_back(cursor); }
        return offsets;
    }

    std::vector<GridTrack> rows_;
    std::vector<GridTrack> cols_;
    std::vector<Entry> entries_;
};

} // namespace tui
