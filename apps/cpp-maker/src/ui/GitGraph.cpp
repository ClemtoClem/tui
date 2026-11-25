/* @file ui/GitGraph.cpp */

#include "ui/GitGraph.hpp"

#include "core/Util.hpp"

#include <tui/core/Text.hpp>
#include <tui/widget/Scrollbar.hpp>

#include <algorithm>
#include <unordered_map>

namespace ui {

namespace {

/// Palette de couleurs distinctes pour les branches.
constexpr tui::Color kBranchColors[] = {
    {0x6c, 0x9a, 0xff}, // bleu
    {0xff, 0x7a, 0x7a}, // rouge
    {0x6c, 0xd0, 0x7a}, // vert
    {0xff, 0xc9, 0x66}, // jaune
    {0xc9, 0x8a, 0xff}, // violet
    {0x66, 0xd9, 0xcc}, // cyan
    {0xff, 0x8a, 0xc9}, // magenta
    {0xa8, 0xd9, 0x66}, // lime
};

constexpr size_t kBranchColorCount = sizeof(kBranchColors) / sizeof(kBranchColors[0]);

std::string build_info(const vcs::CommitRef& c) {
    std::string info = c.short_hash + "  " + c.subject + "  (" + c.author + ", " + c.date + ")";
    std::string refs;
    if (c.head) refs += "* ";
    for (const auto& b : c.branches) refs += b + " ";
    for (const auto& t : c.tags) refs += "tag:" + t + " ";
    if (!refs.empty()) info += "  [" + util::trim(refs) + "]";
    return info;
}

} // namespace

void GitGraph::set_history(vcs::History h) {
    commits_ = std::move(h.commits);
    selected_ = 0;
    scroll_row_ = 0;
    scroll_col_ = 0;
    build_layout();
    // Largeur du contenu pour le défilement horizontal.
    content_width_ = graph_width() + 2;
    for (const auto& c : commits_)
        content_width_ = std::max(content_width_,
                                  graph_width() + 2 + static_cast<int>(tui::TextHelper::display_width(tui::TextHelper::decode_utf8(build_info(c)))));
    need_repaint();
}

const vcs::CommitRef* GitGraph::selected() const {
    return selected_ < commits_.size() ? &commits_[selected_] : nullptr;
}

void GitGraph::select_by_hash(const std::string& short_hash) {
    for (size_t i = 0; i < commits_.size(); ++i) {
        if (commits_[i].short_hash == short_hash || commits_[i].hash == short_hash) {
            move_to(static_cast<long>(i));
            return;
        }
    }
}

void GitGraph::build_layout() {
    size_t n = commits_.size();
    lanes_.assign(n, 0);
    pass_.assign(n, {});
    diag_.assign(n, {});
    max_lane_ = 0;
    row_of_.clear();
    first_child_.clear();
    if (n == 0) return;

    for (size_t i = 0; i < n; ++i) row_of_[commits_[i].hash] = i;

    // 1) Colonne de chaque commit (lignes de branches).
    std::unordered_map<std::string, int> reserved; // parent -> colonne
    int next = 0;
    for (size_t i = 0; i < n; ++i) {
        auto it = reserved.find(commits_[i].hash);
        if (it != reserved.end()) {
            lanes_[i] = it->second;
            reserved.erase(it);
        } else {
            lanes_[i] = next++;
        }
        for (size_t j = 0; j < commits_[i].parents.size(); ++j) {
            const std::string& p = commits_[i].parents[j];
            if (reserved.count(p)) continue; // déjà réservé par un autre enfant (fork)
            reserved[p] = (j == 0) ? lanes_[i] : next++;
        }
        max_lane_ = std::max(max_lane_, lanes_[i]);
    }

    // 2) Premier enfant (pour la navigation parent/enfant).
    for (size_t i = 0; i < n; ++i)
        for (const auto& p : commits_[i].parents)
            if (!first_child_.count(p)) first_child_[p] = i;

    // 3) Lignes passantes et diagonales de merge/fork.
    std::set<int> open;
    for (size_t i = 0; i < n; ++i) {
        for (int c : open)
            if (c != lanes_[i]) pass_[i].insert(c);
        open.erase(lanes_[i]);

        for (size_t j = 0; j < commits_[i].parents.size(); ++j) {
            auto it = row_of_.find(commits_[i].parents[j]);
            if (it == row_of_.end()) continue; // parent hors fenêtre (tronqué)
            int lp = lanes_[it->second];
            open.insert(lp);
            // Diagonale pour une branche latérale (merge ou fork), seulement
            // s'il y a un écart (pas entre deux commits adjacents).
            if (lp != lanes_[i] && it->second > i + 1) {
                diag_[i + 1][lp] = (lp > lanes_[i]) ? U'╲' : U'╱';
            }
        }
    }
}

tui::Size GitGraph::measure(const tui::Constraints& c) const {
    return c.clamp({c.max_width, c.max_height});
}

void GitGraph::arrange(tui::Rect r) {
    bounds_ = r;
    clamp_scroll();
}

tui::Size GitGraph::content_size() const {
    return {content_width_, static_cast<int>(commits_.size())};
}

tui::Point GitGraph::scroll_offset() const { return {scroll_col_, scroll_row_}; }

void GitGraph::set_scroll_offset(int x, int y) {
    scroll_col_ = x;
    scroll_row_ = y;
    clamp_scroll();
    need_repaint();
}

int GitGraph::max_scroll_col() const { return std::max(content_width_ - content_width(), 0); }

tui::Color GitGraph::lane_color(int lane) const {
    return kBranchColors[static_cast<size_t>(lane) % kBranchColorCount];
}

void GitGraph::paint(tui::Buffer& b) const {
    tui::ClipGuard clip(b, bounds_);
    int w = content_width();
    int gw = graph_width();
    for (int row = 0; row < bounds_.height; ++row) {
        size_t idx = static_cast<size_t>(scroll_row_ + row);
        if (idx >= commits_.size()) break;
        int y = bounds_.y + row;
        bool sel = focused_ && idx == selected_;
        const auto& commit = commits_[idx];

        // Préfixe graphe : lignes + nœud.
        for (int c = 0; c < gw; ++c) {
            char32_t ch = U' ';
            tui::Color color = tui::Color::Default();
            if (c == lanes_[idx]) {
                ch = commit.is_merge() ? U'◆' : U'●';
                color = lane_color(c);
            } else if (auto it = diag_[idx].find(c); it != diag_[idx].end()) {
                ch = it->second;
                color = lane_color(c);
            } else if (pass_[idx].count(c)) {
                ch = U'│';
                color = lane_color(c);
            } else {
                continue;
            }
            int x = bounds_.x + c - scroll_col_;
            if (x < bounds_.x || x >= bounds_.x + w) continue;
            tui::TextStyle st = (sel && c == lanes_[idx]) ? tui::TextStyle::Reverse : tui::TextStyle::None;
            b.set(x, y, tui::Cell{ch, 1, color, tui::Color::Default(), st});
        }

        // Texte du commit.
        auto decoded = tui::TextHelper::decode_utf8(build_info(commit));
        int x = bounds_.x + gw + 1 - scroll_col_;
        tui::Color fg = sel ? accent_ : tui::Color::Default();
        tui::TextStyle st = sel ? tui::TextStyle::Bold : tui::TextStyle::None;
        for (char32_t ch : decoded) {
            if (x >= bounds_.x + w) break;
            if (x >= bounds_.x) b.set(x, y, tui::Cell{ch, 1, fg, tui::Color::Default(), st});
            ++x;
        }
    }
}

void GitGraph::move_to(long index) {
    if (commits_.empty()) return;
    index = std::clamp<long>(index, 0, static_cast<long>(commits_.size()) - 1);
    if (static_cast<size_t>(index) == selected_) return;
    selected_ = static_cast<size_t>(index);
    ensure_visible();
    need_repaint();
    if (on_select_ && selected_ < commits_.size()) on_select_(commits_[selected_]);
}

void GitGraph::ensure_visible() {
    int h = std::max(bounds_.height, 1);
    int sel = static_cast<int>(selected_);
    if (sel < scroll_row_) scroll_row_ = sel;
    if (sel >= scroll_row_ + h) scroll_row_ = sel - h + 1;
    clamp_scroll();
}

void GitGraph::clamp_scroll() {
    int max_row = std::max(static_cast<int>(commits_.size()) - std::max(bounds_.height, 1), 0);
    scroll_row_ = std::clamp(scroll_row_, 0, max_row);
    scroll_col_ = std::clamp(scroll_col_, 0, max_scroll_col());
}

bool GitGraph::on_key(const tui::KeyEvent& e) {
    if (commits_.empty()) return false;
    long s = static_cast<long>(selected_);
    int page = std::max(bounds_.height - 1, 1);
    switch (e.key) {
        case tui::Key::Up: move_to(s - 1); return true;
        case tui::Key::Down: move_to(s + 1); return true;
        case tui::Key::PageUp: move_to(s - page); return true;
        case tui::Key::PageDown: move_to(s + page); return true;
        case tui::Key::Home: move_to(0); return true;
        case tui::Key::End: move_to(static_cast<long>(commits_.size()) - 1); return true;
        case tui::Key::Left: {
            const auto& c = commits_[selected_];
            if (!c.parents.empty()) {
                auto it = row_of_.find(c.parents.front());
                if (it != row_of_.end()) move_to(static_cast<long>(it->second));
            }
            return true;
        }
        case tui::Key::Right: {
            auto it = first_child_.find(commits_[selected_].hash);
            if (it != first_child_.end()) move_to(static_cast<long>(it->second));
            return true;
        }
        case tui::Key::Enter:
            if (on_activate_ && selected_ < commits_.size()) on_activate_(commits_[selected_]);
            return true;
        case tui::Key::Char:
            if (e.codepoint == U'c' && !e.ctrl && on_context_ && selected_ < commits_.size()) {
                on_context_(commits_[selected_]);
                return true;
            }
            return false;
        default: return false;
    }
}

bool GitGraph::on_mouse(const tui::MouseEvent& e) {
    if (handle_h_scroll_wheel(e)) return true;
    if (e.button == tui::MouseEvent::Button::WheelUp) { scroll_row_ -= 3; clamp_scroll(); need_repaint(); return true; }
    if (e.button == tui::MouseEvent::Button::WheelDown) { scroll_row_ += 3; clamp_scroll(); need_repaint(); return true; }
    if (handle_scrollbar_mouse(e)) return true;
    // Clic droit : menu contextuel du commit survolé.
    if (e.button == tui::MouseEvent::Button::Right && e.action == tui::MouseEvent::Action::Press) {
        long idx = static_cast<long>(scroll_row_) + (e.y - bounds_.y);
        if (idx >= 0 && static_cast<size_t>(idx) < commits_.size()) {
            move_to(idx);
            if (on_context_) on_context_(commits_[static_cast<size_t>(idx)]);
        }
        return true;
    }
    if (e.button != tui::MouseEvent::Button::Left || e.action != tui::MouseEvent::Action::Press) return false;
    long idx = static_cast<long>(scroll_row_) + (e.y - bounds_.y);
    if (idx >= 0 && static_cast<size_t>(idx) < commits_.size()) {
        move_to(idx);
        if (e.clicks >= 2 && on_activate_) on_activate_(commits_[static_cast<size_t>(idx)]);
    }
    return true;
}

} // namespace ui
