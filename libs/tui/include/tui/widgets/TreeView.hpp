/**
 * @file TreeView.hpp
 * @brief Arborescence navigable : expansion paresseuse via lazy_expand, navigation clavier.
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/Scrollbar.hpp"
#include "../widget/Widget.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace tui {

struct TreeNode {
    std::string label;
    /// Identifiant opaque pour l'application hôte (ex: chemin de fichier
    /// complet quand `label` n'est que le nom affiché) ; jamais lu ni
    /// écrit par TreeView lui-même.
    std::string id;
    std::vector<std::shared_ptr<TreeNode>> children;
    bool expanded = false;
    /// Appelé une seule fois, à la première expansion, si `children`
    /// est vide - permet de peupler à la demande (ex: lister un
    /// répertoire seulement quand l'utilisateur l'ouvre).
    std::function<void(TreeNode&)> lazy_expand;
    bool lazy_loaded = false;
};

class TreeView : public Widget {
public:
    void set_root(std::shared_ptr<TreeNode> root) {
        root_ = std::move(root);
        rebuild_visible();
        need_repaint();
    }

    void set_on_activate(std::function<void(TreeNode&)> cb) { on_activate_ = std::move(cb); }

    /// Réserve la dernière colonne pour une piste de défilement cliquable,
    /// comme ListView (la politique est gérée par Widget::set_scrollbar_policy).
    void set_accent_color(Color color) { accent_color_ = color; set_scrollbar_color(color); need_repaint(); }

    [[nodiscard]] Size measure(const Constraints& c) const override {
        return c.clamp({c.max_width, c.max_height});
    }

    [[nodiscard]] Size content_size() const override { return {row_max_width(), static_cast<int>(visible_.size())}; }
    [[nodiscard]] Point scroll_offset() const override { return {scroll_col_, scroll_offset_}; }
    void set_scroll_offset(int x, int y) override {
        scroll_offset_ = y;
        scroll_col_ = x;
        clamp_scroll();
        need_repaint();
    }

    void arrange(Rect final_rect) override {
        bounds_ = final_rect;
        // Borne le défilement sans forcer la sélection visible (pour pouvoir
        // défiler au-delà de l'élément sélectionné).
        clamp_scroll();
    }

    void paint(Buffer& buffer) const override {
        ClipGuard clip(buffer, bounds_);
        for (int row = 0; row < bounds_.height; ++row) {
            size_t idx = static_cast<size_t>(scroll_offset_ + row);
            if (idx >= visible_.size()) continue;
            paint_row(buffer, visible_[idx], idx, bounds_.y + row);
        }
    }

    [[nodiscard]] bool focusable() const override { return !visible_.empty(); }
    void on_focus() override { focused_ = true; need_repaint(); }
    void on_blur() override { focused_ = false; need_repaint(); }

    bool on_key(const KeyEvent& e) override {
        if (visible_.empty()) return false;
        if (e.key == Key::Up) { move_selection(-1); return true; }
        if (e.key == Key::Down) { move_selection(1); return true; }
        if (e.key == Key::Right) { expand_or_move_in(selected_); return true; }
        if (e.key == Key::Left) { collapse_or_move_out(selected_); return true; }
        if (e.key == Key::Enter) {
            auto node = visible_[selected_].node;
            if (!node->children.empty() || node->lazy_expand) toggle_expand(selected_);
            if (on_activate_) on_activate_(*node);
            return true;
        }
        return false;
    }

    bool on_mouse(const MouseEvent& e) override {
        if (visible_.empty()) return false;
        if (handle_h_scroll_wheel(e)) return true;
        if (e.button == MouseEvent::Button::WheelUp) { scroll_by(-3); return true; }
        if (e.button == MouseEvent::Button::WheelDown) { scroll_by(3); return true; }
        if (handle_scrollbar_mouse(e)) return true;

        if (e.button != MouseEvent::Button::Left || e.action != MouseEvent::Action::Press) return false;
        size_t idx = static_cast<size_t>(scroll_offset_ + (e.y - bounds_.y));
        if (idx >= visible_.size()) return false;
        set_selected_index(idx);

        const auto& entry = visible_[idx];
        bool expandable = !entry.node->children.empty() || entry.node->lazy_expand;
        int marker_x = bounds_.x + entry.depth * 2;
        if (expandable && e.x >= marker_x && e.x < marker_x + 2) {
            toggle_expand(idx);
        } else if (e.clicks >= 2) {
            // Double-clic sur le libellé : déplie / replie, puis active (comme Entrée).
            std::shared_ptr<TreeNode> node = entry.node;   // visible_ est reconstruit par toggle_expand
            if (expandable) toggle_expand(idx);
            if (on_activate_) on_activate_(*node);
        } else if (!expandable && on_activate_) {
            on_activate_(*entry.node);
        }
        return true;
    }

    [[nodiscard]] size_t selected_index() const { return selected_; }
    [[nodiscard]] std::shared_ptr<TreeNode> selected_node() const {
        return visible_.empty() ? nullptr : visible_[selected_].node;
    }

private:
    struct VisibleEntry {
        std::shared_ptr<TreeNode> node;
        int depth;
    };

    void rebuild_visible() {
        visible_.clear();
        if (root_) flatten(root_, 0);
        if (selected_ >= visible_.size()) selected_ = visible_.empty() ? 0 : visible_.size() - 1;
    }

    void flatten(const std::shared_ptr<TreeNode>& node, int depth) {
        visible_.push_back({node, depth});
        if (node->expanded) {
            for (auto& child : node->children) flatten(child, depth + 1);
        }
    }

    void toggle_expand(size_t idx) {
        auto node = visible_[idx].node;
        if (!node->expanded && !node->lazy_loaded && node->lazy_expand) {
            node->lazy_expand(*node);
            node->lazy_loaded = true;
        }
        node->expanded = !node->expanded;
        rebuild_visible();
        need_repaint();
    }

    void expand_or_move_in(size_t idx) {
        auto node = visible_[idx].node;
        bool expandable = !node->children.empty() || node->lazy_expand;
        if (expandable && !node->expanded) {
            toggle_expand(idx);
        } else {
            move_selection(1);
        }
    }

    void collapse_or_move_out(size_t idx) {
        auto node = visible_[idx].node;
        if (node->expanded) {
            node->expanded = false;
            rebuild_visible();
            need_repaint();
            return;
        }
        int depth = visible_[idx].depth;
        for (long i = static_cast<long>(idx) - 1; i >= 0; --i) {
            if (visible_[static_cast<size_t>(i)].depth < depth) {
                set_selected_index(static_cast<size_t>(i));
                return;
            }
        }
    }

    void move_selection(int delta) {
        long next = std::clamp(static_cast<long>(selected_) + delta, 0L, static_cast<long>(visible_.size()) - 1);
        set_selected_index(static_cast<size_t>(next));
    }

    void set_selected_index(size_t i) {
        selected_ = i;
        ensure_visible();
        need_repaint();
    }

    void ensure_visible() {
        if (bounds_.height <= 0) return;
        if (static_cast<int>(selected_) < scroll_offset_) scroll_offset_ = static_cast<int>(selected_);
        if (static_cast<int>(selected_) >= scroll_offset_ + bounds_.height) {
            scroll_offset_ = static_cast<int>(selected_) - bounds_.height + 1;
        }
        clamp_scroll();
    }

    void scroll_by(int delta) {
        scroll_offset_ += delta;
        clamp_scroll();
        need_repaint();
    }

    void clamp_scroll() {
        int max_scroll = std::max(static_cast<int>(visible_.size()) - std::max(bounds_.height, 0), 0);
        scroll_offset_ = std::clamp(scroll_offset_, 0, max_scroll);
        scroll_col_ = std::clamp(scroll_col_, 0, max_scroll_col());
    }

    [[nodiscard]] int content_width() const { return Widget::content_width(); }

    [[nodiscard]] int row_max_width() const {
        int w = 0;
        for (const auto& e : visible_) {
            bool expandable = !e.node->children.empty() || e.node->lazy_expand;
            std::string marker = !expandable ? "  " : (e.node->expanded ? "▾ " : "▸ ");
            std::string indent(static_cast<size_t>(e.depth) * 2, ' ');
            w = std::max(w, TextHelper::display_width(TextHelper::decode_utf8(indent + marker + e.node->label)));
        }
        return w;
    }
    [[nodiscard]] int max_scroll_col() const { return std::max(row_max_width() - content_width(), 0); }

    void paint_row(Buffer& buffer, const VisibleEntry& entry, size_t idx, int y) const {
        bool expandable = !entry.node->children.empty() || entry.node->lazy_expand;
        std::string marker = !expandable ? "  " : (entry.node->expanded ? "▾ " : "▸ ");
        std::string indent(static_cast<size_t>(entry.depth) * 2, ' ');
        std::string display = indent + marker + entry.node->label;
        auto decoded = TextHelper::decode_utf8(display);
        RowCursor cur = row_cursor(focused_);
        bool sel = cur.active && idx == selected_;
        TextStyle style = sel ? cur.style : TextStyle::None;
        Color fg = sel ? cur.fg : Color::Default(), bg = sel ? cur.bg : Color::Default();
        int left = bounds_.x;
        int right = left + content_width();
        if (sel && cur.full_row)
            for (int fx = left; fx < right; ++fx) buffer.set(fx, y, Cell{U' ', 1, fg, bg, style});
        int x = left - scroll_col_;
        for (char32_t ch : decoded) {
            if (x >= right) break;
            if (x >= left) buffer.set(x, y, Cell{ch, 1, fg, bg, style});
            ++x;
        }
    }

    Color accent_color_ = Color::Default();
    std::shared_ptr<TreeNode> root_;
    std::vector<VisibleEntry> visible_;
    size_t selected_ = 0;
    int scroll_offset_ = 0;
    int scroll_col_ = 0;
    bool focused_ = false;
    std::function<void(TreeNode&)> on_activate_;
};

} // namespace tui
