/* @file ui/DiffView.hpp
 *
 * Visualiseur de diff en lecture seule : coloration des ajouts/suppressions,
 * en-têtes en gras, numéros de ligne, sélection/copie. Aucune édition.
 */

#pragma once

#include <tui/Buffer.hpp>
#include <tui/core/Color.hpp>
#include <tui/widget/Widget.hpp>
#include <tui/widgets/TextArea.hpp>

#include <memory>
#include <string>
#include <vector>

namespace ui {

class DiffView : public tui::Widget {
public:
    explicit DiffView(std::string text = {}) {
        editor_->set_parent(this);
        editor_->set_read_only(true);
        editor_->set_show_scrollbar(true);
        set_text(std::move(text));
    }

    void set_text(std::string text);
    void set_colors(tui::Color add, tui::Color del, tui::Color header);

    [[nodiscard]] tui::Size measure(const tui::Constraints& c) const override { return editor_->measure(c); }
    void arrange(tui::Rect r) override {
        bounds_ = r;
        editor_->layout(r);
    }
    void paint(tui::Buffer& b) const override {
        tui::ClipGuard clip(b, bounds_);
        editor_->render(b);
    }

    [[nodiscard]] bool focusable() const override { return true; }
    void on_focus() override { editor_->on_focus(); }
    void on_blur() override { editor_->on_blur(); }
    bool on_key(const tui::KeyEvent& e) override { return editor_->on_key(e); }
    bool on_mouse(const tui::MouseEvent& e) override { return editor_->on_mouse(e); }
    [[nodiscard]] std::vector<std::shared_ptr<tui::Widget>> children() const override { return {editor_}; }

private:
    std::shared_ptr<tui::TextArea> editor_ = std::make_shared<tui::TextArea>();
    std::vector<tui::Color> line_colors_;
    std::vector<bool> line_bold_;
    tui::Color add_color_{0x6c, 0xd0, 0x7a};
    tui::Color del_color_{0xff, 0x7a, 0x7a};
    tui::Color header_color_{0x6c, 0x9a, 0xff};
};

} // namespace ui
