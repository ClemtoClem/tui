/* @file ui/DiffView.cpp */

#include "ui/DiffView.hpp"

#include <tui/core/Text.hpp>

namespace ui {

void DiffView::set_colors(tui::Color add, tui::Color del, tui::Color header) {
    add_color_ = add;
    del_color_ = del;
    header_color_ = header;
    // Recalcule la coloration des lignes avec les nouvelles couleurs.
    set_text(editor_->text());
}

void DiffView::set_text(std::string text) {
    editor_->set_text(std::move(text));

    line_colors_.clear();
    line_bold_.clear();
    size_t n = editor_->line_count();
    line_colors_.reserve(n);
    line_bold_.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const std::u32string& line = editor_->line(i);
        tui::Color c = tui::Color::Default();
        bool bold = false;
        if (!line.empty()) {
            char32_t first = line[0];
            if (first == U'+' && !line.starts_with(U"+++")) {
                c = add_color_;
            } else if (first == U'-' && !line.starts_with(U"---")) {
                c = del_color_;
            } else if (line.starts_with(U"@@") || line.starts_with(U"diff ") || line.starts_with(U"index ") ||
                       line.starts_with(U"---") || line.starts_with(U"+++") || line.starts_with(U"commit ") ||
                       line.starts_with(U"Author:") || line.starts_with(U"Date:") || line.starts_with(U"Merge:") ||
                       line.starts_with(U"new file") || line.starts_with(U"deleted file") || line.starts_with(U"rename ")) {
                c = header_color_;
                bold = true;
            }
        }
        line_colors_.push_back(c);
        line_bold_.push_back(bold);
    }

    editor_->set_style_hook([this](int line, int /*col*/) -> tui::CharStyle {
        if (line < 0 || static_cast<size_t>(line) >= line_colors_.size()) return {};
        tui::CharStyle cs;
        cs.fg = line_colors_[static_cast<size_t>(line)];
        if (line_bold_[static_cast<size_t>(line)]) cs.style = tui::TextStyle::Bold;
        return cs;
    });
    need_repaint();
}

} // namespace ui
