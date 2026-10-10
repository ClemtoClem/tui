/**
 * @file Tabs.hpp
 * @brief Barre d'onglets + N pages ; seule la page active est arrangée/peinte.
 */

#pragma once

#include "../../Buffer.hpp"
#include "../../core/Text.hpp"
#include "../BoxFrame.hpp"
#include "../Widget.hpp"

namespace tui {

/// Line (défaut) : rendu historique, un bandeau d'une ligne en vidéo
/// inverse pour l'onglet actif - inchangé pour ne rien casser.
/// Boxed : chaque onglet dans son propre cadre (façon lipgloss/planor),
/// posé sur une règle pleine largeur ; utilise 3 lignes au lieu d'une.
enum class TabsBarStyle {
    Line,
    Boxed,
};

class Tabs : public Widget {
public:
    void add_tab(std::string label, std::shared_ptr<Widget> page) {
        page->set_parent(this);
        tabs_.push_back({std::move(label), std::move(page)});
        need_repaint();
    }

    void set_active_index(size_t index) {
        if (index < tabs_.size() && index != active_index_) {
            active_index_ = index;
            need_repaint();
        }
    }
    [[nodiscard]] size_t active_index() const { return active_index_; }
    [[nodiscard]] size_t tab_count() const { return tabs_.size(); }

    void set_bar_style(TabsBarStyle style) { bar_style_ = style; need_repaint(); }

    /// Couleurs utilisées par le style Boxed ; Color::Default() (valeur
    /// par défaut) laisse le terminal choisir. Sans effet en style Line.
    void set_colors(Color border, Color active_text, Color inactive_text) {
        border_color_ = border;
        active_text_color_ = active_text;
        inactive_text_color_ = inactive_text;
        need_repaint();
    }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        return c.clamp({c.max_width, c.max_height});
    }

    void arrange(Rect final_rect) override {
        bounds_ = final_rect;
        int bar_height = tab_bar_height();
        tab_bar_rect_ = Rect{final_rect.x, final_rect.y, final_rect.width, bar_height};
        Rect page_rect{final_rect.x, final_rect.y + bar_height, final_rect.width,
                        std::max(final_rect.height - bar_height, 0)};
        // Seule la page active est arrangée : les pages inactives n'ont
        // aucun coût de mise en page tant qu'elles ne sont pas visibles.
        if (!tabs_.empty()) tabs_[active_index_].page->arrange(page_rect);
    }

    void paint(Buffer& buffer) const override {
        ClipGuard clip(buffer, bounds_);
        if (bar_style_ == TabsBarStyle::Boxed) {
            draw_boxed_tab_bar(buffer);
        } else {
            draw_line_tab_bar(buffer);
        }
        if (!tabs_.empty() && tabs_[active_index_].page->visible()) {
            tabs_[active_index_].page->paint(buffer);
        }
    }

    bool on_key(const KeyEvent& e) override {
        if (tabs_.empty()) return false;
        if (e.key == Key::Left && e.ctrl) {
            set_active_index((active_index_ + tabs_.size() - 1) % tabs_.size());
            return true;
        }
        if (e.key == Key::Right && e.ctrl) {
            set_active_index((active_index_ + 1) % tabs_.size());
            return true;
        }
        return false;
    }

    [[nodiscard]] bool focusable() const override { return !tabs_.empty(); }

    [[nodiscard]] std::vector<std::shared_ptr<Widget>> children() const override {
        std::vector<std::shared_ptr<Widget>> result;
        if (!tabs_.empty()) result.push_back(tabs_[active_index_].page);
        return result;
    }

private:
    [[nodiscard]] int tab_bar_height() const { return bar_style_ == TabsBarStyle::Boxed ? 3 : 1; }

    void draw_line_tab_bar(Buffer& buffer) const {
        int x = tab_bar_rect_.x;
        for (size_t i = 0; i < tabs_.size(); ++i) {
            TextStyle style = (i == active_index_) ? (TextStyle::Bold | TextStyle::Reverse) : TextStyle::None;
            std::u32string label = TextHelper::decode_utf8(" " + tabs_[i].label + " ");
            for (char32_t ch : label) {
                if (x >= tab_bar_rect_.right()) break;
                buffer.set(x, tab_bar_rect_.y, Cell{ch, 1, Color::Default(), Color::Default(), style});
                ++x;
            }
        }
    }

    void draw_boxed_tab_bar(Buffer& buffer) const {
        BoxChars chars = box_chars(BorderStyle::Rounded);
        int y0 = tab_bar_rect_.y;
        int y1 = y0 + 1;
        int y2 = y0 + 2;
        int right = tab_bar_rect_.right();

        auto put = [&](int px, int py, char32_t ch, Color color, TextStyle style = TextStyle::None) {
            buffer.set(px, py, Cell{ch, 1, color, Color::Default(), style});
        };

        // Règle pleine largeur en fond : chaque onglet y superpose son
        // propre coin bas ensuite, donnant l'impression d'une ligne
        // continue "portant" les cadres d'onglets.
        for (int x = tab_bar_rect_.x; x < right; ++x) put(x, y2, chars.horizontal, border_color_);

        int x = tab_bar_rect_.x;
        for (size_t i = 0; i < tabs_.size(); ++i) {
            bool active = (i == active_index_);
            std::u32string label = TextHelper::decode_utf8(" " + tabs_[i].label + " ");
            int inner_width = TextHelper::display_width(label);
            int box_width = inner_width + 2;
            if (x + box_width > right) break;

            put(x, y0, chars.top_left, border_color_);
            put(x + box_width - 1, y0, chars.top_right, border_color_);
            for (int fx = x + 1; fx < x + box_width - 1; ++fx) put(fx, y0, chars.horizontal, border_color_);

            put(x, y1, chars.vertical, border_color_);
            put(x + box_width - 1, y1, chars.vertical, border_color_);
            TextStyle label_style = active ? TextStyle::Bold : TextStyle::None;
            Color label_color = active ? active_text_color_ : inactive_text_color_;
            int lx = x + 1;
            for (char32_t ch : label) { put(lx, y1, ch, label_color, label_style); ++lx; }

            put(x, y2, chars.bottom_left, border_color_);
            put(x + box_width - 1, y2, chars.bottom_right, border_color_);

            x += box_width;
        }
    }

    struct TabEntry {
        std::string label;
        std::shared_ptr<Widget> page;
    };

    std::vector<TabEntry> tabs_;
    size_t active_index_ = 0;
    Rect tab_bar_rect_;
    TabsBarStyle bar_style_ = TabsBarStyle::Line;
    Color border_color_ = Color::Default();
    Color active_text_color_ = Color::Default();
    Color inactive_text_color_ = Color::Default();
};

} // namespace tui
