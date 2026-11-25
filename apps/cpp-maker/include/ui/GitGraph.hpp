/* @file ui/GitGraph.hpp
 *
 * Graphe interactif de l'historique git : nœuds (commits) reliés par des
 * lignes de branches, couleurs distinctes par branche, merges identifiables.
 *
 * Le layout (colonnes/lignes de branches) est calculé UNE fois par
 * set_history() ; paint() ne dessine que les lignes visibles, ce qui reste
 * efficace sur des dépôts de plusieurs milliers de commits.
 */

#pragma once

#include "vcs/History.hpp"

#include <tui/Buffer.hpp>
#include <tui/core/Color.hpp>
#include <tui/widget/Widget.hpp>

#include <functional>
#include <map>
#include <set>
#include <unordered_map>
#include <vector>

namespace ui {

class GitGraph : public tui::Widget {
public:
    GitGraph() = default;

    void set_history(vcs::History h);
    void set_on_select(std::function<void(const vcs::CommitRef&)> cb) { on_select_ = std::move(cb); }
    void set_on_activate(std::function<void(const vcs::CommitRef&)> cb) { on_activate_ = std::move(cb); }
    /// Clic droit sur un commit (ouvre un menu contextuel).
    void set_on_context(std::function<void(const vcs::CommitRef&)> cb) { on_context_ = std::move(cb); }
    void set_accent(tui::Color c) { accent_ = c; need_repaint(); }

    /// Commit sélectionné (nullptr si historique vide).
    [[nodiscard]] const vcs::CommitRef* selected() const;
    void select_by_hash(const std::string& short_hash);

    // --- Widget ---
    [[nodiscard]] tui::Size measure(const tui::Constraints& c) const override;
    void arrange(tui::Rect r) override;
    void paint(tui::Buffer& b) const override;
    [[nodiscard]] tui::Size content_size() const override;
    [[nodiscard]] tui::Point scroll_offset() const override;
    void set_scroll_offset(int x, int y) override;

    [[nodiscard]] bool focusable() const override { return !commits_.empty(); }
    void on_focus() override { focused_ = true; need_repaint(); }
    void on_blur() override { focused_ = false; need_repaint(); }
    bool on_key(const tui::KeyEvent& e) override;
    bool on_mouse(const tui::MouseEvent& e) override;

private:
    void build_layout();
    void move_to(long index);
    void ensure_visible();
    void clamp_scroll();
    [[nodiscard]] int graph_width() const { return max_lane_ + 1; }
    [[nodiscard]] int max_scroll_col() const;
    [[nodiscard]] tui::Color lane_color(int lane) const;

    std::vector<vcs::CommitRef> commits_;
    std::vector<int> lanes_;           ///< colonne de chaque commit
    int max_lane_ = 0;
    std::vector<std::set<int>> pass_;  ///< pass_[r] = colonnes avec « │ »
    std::vector<std::map<int, char32_t>> diag_; ///< diag_[r][col] = U'╲' / U'╱'
    std::unordered_map<std::string, size_t> row_of_;      ///< hash -> index
    std::unordered_map<std::string, size_t> first_child_; ///< hash -> premier enfant
    int content_width_ = 0;

    size_t selected_ = 0;
    int scroll_row_ = 0;
    int scroll_col_ = 0;
    bool focused_ = false;
    tui::Color accent_ = tui::Color::Default();

    std::function<void(const vcs::CommitRef&)> on_select_;
    std::function<void(const vcs::CommitRef&)> on_activate_;
    std::function<void(const vcs::CommitRef&)> on_context_;
};

} // namespace ui
