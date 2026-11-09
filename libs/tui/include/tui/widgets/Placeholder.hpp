/**
 * @file Placeholder.hpp
 * @brief Bloc coloré portant un libellé, pour maquetter une mise en page
 * (Textual « Placeholder »). Un clic fait défiler les variantes : libellé,
 * taille, texte de remplissage.
 */

#pragma once

#include "../widget/Paint.hpp"
#include "../widget/Widget.hpp"

#include <array>
#include <string>

namespace tui {

class Placeholder : public Widget {
public:
    enum class Variant { Default, Size, Text };

    explicit Placeholder(std::string label = {}) : label_(std::move(label)) {
        static int counter = 0;
        color_index_ = counter++;
    }

    void set_label(std::string l) { label_ = std::move(l); need_repaint(); }
    void set_variant(Variant v) { variant_ = v; need_repaint(); }
    [[nodiscard]] Variant variant() const { return variant_; }
    void cycle_variant() {
        variant_ = variant_ == Variant::Default ? Variant::Size : variant_ == Variant::Size ? Variant::Text : Variant::Default;
        need_repaint();
    }

    [[nodiscard]] Size measure(const Constraints& c) const override { return c.clamp({c.max_width, c.max_height}); }

    void paint(Buffer& b) const override {
        static constexpr std::array<Color, 8> palette{
            Color{136, 23, 152}, Color{0, 103, 155}, Color{0, 133, 102}, Color{195, 112, 0},
            Color{170, 30, 60},  Color{60, 90, 200}, Color{100, 130, 20}, Color{120, 70, 160},
        };
        Color bg = style().background ? Color::Default() : palette[static_cast<size_t>(color_index_) % palette.size()];
        Color fg = style().color ? Color::Default() : Color{240, 240, 240};
        paint::fill(b, bounds_, U' ', fg, bg);
        std::string text = label_.empty() ? (id().empty() ? "Placeholder" : "#" + id()) : label_;
        if (variant_ == Variant::Size) text = std::to_string(bounds_.width) + " x " + std::to_string(bounds_.height);
        if (variant_ == Variant::Text) {
            static const std::string lorem =
                "Lorem ipsum dolor sit amet, consectetur adipiscing elit, sed do eiusmod tempor incididunt ut labore "
                "et dolore magna aliqua. Ut enim ad minim veniam, quis nostrud exercitation ullamco laboris.";
            auto lines = TextHelper::wrap_to_width(TextHelper::decode_utf8(lorem), std::max(1, bounds_.width - 2));
            for (size_t i = 0; i < lines.size() && static_cast<int>(i) < bounds_.height; ++i)
                paint::text(b, bounds_.x + 1, bounds_.y + static_cast<int>(i), bounds_.width - 2,
                            TextHelper::encode_utf8(lines[i]), fg, bg);
            return;
        }
        int w = paint::width(text);
        paint::text(b, bounds_.x + std::max(0, (bounds_.width - w) / 2), bounds_.y + (bounds_.height - 1) / 2,
                    bounds_.width, text, fg, bg, TextStyle::Bold);
    }

    bool on_mouse(const MouseEvent& e) override {
        if (e.button == MouseEvent::Button::Left && e.action == MouseEvent::Action::Press) {
            cycle_variant();
            return true;
        }
        return false;
    }

private:
    std::string label_;
    Variant variant_ = Variant::Default;
    int color_index_ = 0;
};

} // namespace tui
