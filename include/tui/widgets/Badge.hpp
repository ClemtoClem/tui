/**
 * @file Badge.hpp
 * @brief Petite étiquette colorée inline (ex: lettre de statut de fichier, compteur).
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/Widget.hpp"

namespace tui {

class Badge : public Widget {
public:
    explicit Badge(std::string text = "") : text_(std::move(text)) {}

    void set_text(std::string text) { text_ = std::move(text); need_repaint(); }
    void set_colors(Color fg, Color bg) { fg_ = fg; bg_ = bg; need_repaint(); }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        auto decoded = TextHelper::decode_utf8(text_);
        return c.clamp({TextHelper::display_width(decoded) + 2, 1});
    }

    void paint(Buffer& buffer) const override {
        if (bounds_.height <= 0) return;
        auto decoded = TextHelper::decode_utf8(" " + text_ + " ");
        auto truncated = TextHelper::truncate_to_width(decoded, bounds_.width);
        int x = bounds_.x;
        for (char32_t ch : truncated) {
            buffer.set(x, bounds_.y, Cell{ch, 1, fg_, bg_, TextStyle::Bold});
            ++x;
        }
    }

private:
    std::string text_;
    Color fg_ = Color::Black();
    Color bg_ = Color::Cyan();
};

} // namespace tui
