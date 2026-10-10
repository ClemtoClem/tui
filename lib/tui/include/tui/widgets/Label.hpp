/**
 * @file Label.hpp
 * @brief Texte statique : couvre Static/Label (une ligne) et Paragraph (multi-ligne, wrap).
 *
 * Une seule implémentation pour les trois, plutôt que trois classes
 * dupliquant la logique de largeur/troncature/wrap : Label avec
 * wrap()==false se comporte comme un Label/Static classique (tronqué,
 * jamais plus d'une ligne) ; avec wrap()==true, comme un Paragraph.
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/Layout.hpp"
#include "../widget/Widget.hpp"

namespace tui {

class Label : public Widget {
public:
    explicit Label(std::string text = "") : text_(std::move(text)) {}

    void set_text(std::string text) { text_ = std::move(text); need_repaint(); }
    [[nodiscard]] const std::string& text() const { return text_; }

    /// false (défaut) : une seule ligne, tronquée si trop longue.
    /// true : multi-ligne avec retour à la ligne glouton (Paragraph).
    void set_wrap(bool wrap) { wrap_ = wrap; need_repaint(); }
    [[nodiscard]] bool wrap() const { return wrap_; }

    void set_align(HAlign align) { align_ = align; need_repaint(); }
    void set_foreground(Color fg) { fg_ = fg; need_repaint(); }
    void set_background(Color bg) { bg_ = bg; need_repaint(); }
    void set_style(TextStyle style) { style_ = style; need_repaint(); }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        auto decoded = TextHelper::decode_utf8(text_);
        if (!wrap_) {
            int w = TextHelper::display_width(decoded);
            return c.clamp({w, 1});
        }
        int width_for_wrap = c.max_width > 0 ? c.max_width : static_cast<int>(decoded.size());
        auto lines = TextHelper::wrap_to_width(decoded, width_for_wrap);
        int max_w = 0;
        for (const auto& l : lines) max_w = std::max(max_w, TextHelper::display_width(l));
        return c.clamp({max_w, static_cast<int>(lines.size())});
    }

    void paint(Buffer& buffer) const override {
        auto decoded = TextHelper::decode_utf8(text_);
        std::vector<std::u32string> lines;
        if (wrap_) {
            lines = TextHelper::wrap_to_width(decoded, bounds_.width);
        } else {
            lines.push_back(TextHelper::truncate_to_width(decoded, bounds_.width, U"…"));
        }

        for (int i = 0; i < static_cast<int>(lines.size()) && i < bounds_.height; ++i) {
            paint_line(buffer, lines[static_cast<size_t>(i)], bounds_.y + i);
        }
    }

private:
    void paint_line(Buffer& buffer, const std::u32string& line, int y) const {
        int line_width = TextHelper::display_width(line);
        int x_offset = 0;
        switch (align_) {
            case HAlign::Start: case HAlign::Stretch: x_offset = 0; break;
            case HAlign::Center: x_offset = std::max((bounds_.width - line_width) / 2, 0); break;
            case HAlign::End: x_offset = std::max(bounds_.width - line_width, 0); break;
        }
        int x = bounds_.x + x_offset;
        for (char32_t ch : line) {
            int w = TextHelper::codepoint_width(ch);
            buffer.set(x, y, Cell{ch, static_cast<uint8_t>(w), fg_, bg_, style_});
            x += std::max(w, 1);
        }
    }

    std::string text_;
    bool wrap_ = false;
    HAlign align_ = HAlign::Start;
    Color fg_ = Color::Default();
    Color bg_ = Color::Default();
    TextStyle style_ = TextStyle::None;
};

} // namespace tui
