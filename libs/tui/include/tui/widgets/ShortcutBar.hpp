/**
 * @file ShortcutBar.hpp
 * @brief Barre d'indices de raccourcis clavier ("F1 Aide  Ctrl+Q Quitter").
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/Widget.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace tui {

struct ShortcutHint {
    std::string key;
    std::string label;
};

/// Horizontal (défaut) : bandeau d'une ligne, touche en vidéo inverse -
/// rendu historique, inchangé.
/// Vertical : une entrée par ligne ("touche  libellé"), sans vidéo
/// inverse, pensé pour un aide-mémoire discret en bas d'un panneau
/// (façon barre latérale planor : "↑/k up", "↓/j down", ...).
enum class ShortcutBarOrientation {
    Horizontal,
    Vertical,
};

class ShortcutBar : public Widget {
public:
    void set_hints(std::vector<ShortcutHint> hints) { hints_ = std::move(hints); need_repaint(); }

    void set_orientation(ShortcutBarOrientation orientation) { orientation_ = orientation; need_repaint(); }

    /// Couleur utilisée en orientation Vertical (touche et libellé) ;
    /// sans effet en Horizontal (qui reste en vidéo inverse par défaut).
    void set_color(Color color) { color_ = color; need_repaint(); }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        if (orientation_ == ShortcutBarOrientation::Vertical) {
            return c.clamp({c.max_width, static_cast<int>(hints_.size())});
        }
        return c.clamp({c.max_width, 1});
    }

    void paint(Buffer& buffer) const override {
        if (orientation_ == ShortcutBarOrientation::Vertical) {
            paint_vertical(buffer);
        } else {
            paint_horizontal(buffer);
        }
    }

private:
    void paint_horizontal(Buffer& buffer) const {
        int x = bounds_.x;
        for (const auto& h : hints_) {
            std::string display = h.key + " " + h.label + "  ";
            auto decoded = TextHelper::decode_utf8(display);
            for (size_t i = 0; i < decoded.size() && x < bounds_.x + bounds_.width; ++i) {
                TextStyle style = (i < h.key.size()) ? TextStyle::Reverse : TextStyle::None;
                buffer.set(x, bounds_.y, Cell{decoded[i], 1, Color::Default(), Color::Default(), style});
                ++x;
            }
        }
    }

    void paint_vertical(Buffer& buffer) const {
        size_t key_width = 0;
        for (const auto& h : hints_) key_width = std::max(key_width, h.key.size());
        key_width += 2;

        for (size_t row = 0; row < hints_.size(); ++row) {
            int y = bounds_.y + static_cast<int>(row);
            if (y >= bounds_.y + bounds_.height) break;
            const auto& h = hints_[row];
            std::string padded_key = h.key;
            padded_key.resize(std::max(padded_key.size(), key_width), ' ');
            std::string display = padded_key + h.label;
            auto decoded = TextHelper::decode_utf8(display);
            auto truncated = TextHelper::truncate_to_width(decoded, bounds_.width);
            int x = bounds_.x;
            for (char32_t ch : truncated) {
                buffer.set(x, y, Cell{ch, 1, color_, Color::Default(), TextStyle::None});
                ++x;
            }
        }
    }

    std::vector<ShortcutHint> hints_;
    ShortcutBarOrientation orientation_ = ShortcutBarOrientation::Horizontal;
    Color color_ = Color::Default();
};

} // namespace tui
