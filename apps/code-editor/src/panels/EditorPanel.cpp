#include "panels/EditorPanel.hpp"

#include "Theme.hpp"
#include "utils/Shell.hpp"

#include <tui/core/SyntaxHighlight.hpp>

#include <fstream>
#include <sstream>

namespace ide {

EditorPanel::EditorPanel() {
    editor_ = std::make_shared<tui::CodeEditor>();
    editor_->set_colors(kAccent, kFg, kMuted);
    editor_->syntax_theme().set_color(tui::TokenRole::Keyword, kAccent);
    editor_->syntax_theme().set_color(tui::TokenRole::String, kSuccess);
    editor_->syntax_theme().set_color(tui::TokenRole::Comment, kMuted);
    editor_->set_blame_provider([this](const std::string& path, int line) { return blame_for(path, line); });
    editor_->set_on_tab_close_requested([this](size_t i) {
        if (confirm_before_close_ && editor_->is_dirty(i)) {
            if (on_status_) {
                on_status_("Fichier modifie - Ctrl+S pour enregistrer avant de fermer (ou desactivez la "
                            "confirmation dans Parametres).");
            }
            return;
        }
        editor_->close_tab(i);
    });

    panel_ = std::make_shared<tui::Border>();
    panel_->set_border_style(tui::BorderStyle::Rounded);
    panel_->set_color(kBorder);
    panel_->set_child(editor_);
}

void EditorPanel::open_file(const std::string& path) {
    for (size_t i = 0; i < editor_->tab_count(); ++i) {
        if (editor_->path_at(i) == path) { editor_->set_active_index(i); return; }
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        if (on_status_) on_status_("Impossible d'ouvrir : " + path);
        return;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    size_t index = editor_->add_tab(path, tui::detect_language(path), ss.str());
    editor_->editor_at(index)->set_key_intercept([this](const tui::KeyEvent& e) {
        if (e.key == tui::Key::Char && e.ctrl && (e.codepoint == U's' || e.codepoint == U'S')) { save_active(); return true; }
        return false;
    });
    if (on_status_) on_status_("Ouvert : " + path);
}

bool EditorPanel::save_active() {
    if (editor_->tab_count() == 0) return false;
    size_t idx = editor_->active_index();
    auto ta = editor_->editor_at(idx);
    std::ofstream out(editor_->path_at(idx), std::ios::binary | std::ios::trunc);
    if (!out) {
        if (on_status_) on_status_("Echec de l'ecriture : " + editor_->path_at(idx));
        return false;
    }
    out << ta->text();
    editor_->mark_saved(idx);
    if (on_status_) on_status_("Enregistre : " + editor_->path_at(idx));
    return true;
}

void EditorPanel::set_show_blame(bool show) {
    editor_->set_show_blame(show);
}

const std::vector<std::string>& EditorPanel::blame_lines_for(const std::string& path) {
    auto it = blame_cache_.find(path);
    if (it != blame_cache_.end()) return it->second;

    std::vector<std::string> lines;
    if (!repo_dir_.empty()) {
        ProcessResult r = run_command(build_command({"git", "blame", "--line-porcelain", "--", path}), repo_dir_);
        std::string current_hash, current_author;
        for (const auto& line : split_lines(r.output)) {
            if (!line.empty() && line[0] == '\t') {
                lines.push_back(current_hash.substr(0, std::min<size_t>(7, current_hash.size())) + " " + current_author);
                continue;
            }
            if (line.rfind("author ", 0) == 0) { current_author = line.substr(7); continue; }
            if (line.size() > 40 && line[40] == ' ') current_hash = line.substr(0, 40);
        }
    }
    return blame_cache_.emplace(path, std::move(lines)).first->second;
}

std::string EditorPanel::blame_for(const std::string& path, int line) {
    const auto& lines = blame_lines_for(path);
    if (line < 0 || static_cast<size_t>(line) >= lines.size()) return "";
    return lines[static_cast<size_t>(line)];
}

} // namespace ide
