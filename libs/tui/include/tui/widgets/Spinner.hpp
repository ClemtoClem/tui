/**
 * @file Spinner.hpp
 * @brief Indicateur d'activité animé ; l'appelant pilote l'animation via advance()
 * (typiquement depuis un App::add_timer, qui prouve le chemin
 * TimerManager -> need_repaint() -> repaint pour un widget qui bouge tout
 * seul, sans que Spinner ait besoin de connaître App).
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/Widget.hpp"

#include <array>
#include <cstddef>

namespace tui {

class Spinner : public Widget {
public:
    void advance() { frame_ = (frame_ + 1) % kFrames.size(); need_repaint(); }
    void set_label(std::string label) { label_ = std::move(label); need_repaint(); }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        int w = 1 + (label_.empty() ? 0 : 1 + static_cast<int>(TextHelper::display_width(TextHelper::decode_utf8(label_))));
        return c.clamp({w, 1});
    }

    void paint(Buffer& buffer) const override {
        if (bounds_.width <= 0 || bounds_.height <= 0) return;
        buffer.set(bounds_.x, bounds_.y, Cell{kFrames[frame_], 1, Color::Default(), Color::Default(), TextStyle::None});

        if (!label_.empty()) {
            auto decoded = TextHelper::decode_utf8(" " + label_);
            auto truncated = TextHelper::truncate_to_width(decoded, std::max(bounds_.width - 1, 0));
            int x = bounds_.x + 1;
            for (char32_t ch : truncated) {
                buffer.set(x, bounds_.y, Cell{ch, 1, Color::Default(), Color::Default(), TextStyle::None});
                ++x;
            }
        }
    }

    [[nodiscard]] size_t frame() const { return frame_; }

private:
    static constexpr std::array<char32_t, 10> kFrames{
        U'⠋', U'⠙', U'⠹', U'⠸', U'⠼', U'⠴', U'⠦', U'⠧', U'⠇', U'⠏',
    };

    size_t frame_ = 0;
    std::string label_;
};

} // namespace tui
