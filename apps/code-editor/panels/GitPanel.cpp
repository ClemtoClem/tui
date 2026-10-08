#include "GitPanel.hpp"

#include "../Theme.hpp"
#include "../util/Shell.hpp"

#include <tui/widget/layout/Horizontal.hpp>
#include <tui/widget/layout/Vertical.hpp>
#include <tui/widgets/Badge.hpp>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <unistd.h>

using tui::Border;
using tui::BorderStyle;
using tui::Horizontal;
using tui::LayoutParams;
using tui::ListItem;
using tui::Vertical;

namespace ide {

namespace {

std::vector<std::string> split_soh(const std::string& s) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (true) {
        size_t p = s.find('\x01', start);
        if (p == std::string::npos) { parts.push_back(s.substr(start)); break; }
        parts.push_back(s.substr(start, p - start));
        start = p + 1;
    }
    return parts;
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

} // namespace

GitPanel::GitPanel(std::string repo_dir) : repo_dir_(std::move(repo_dir)) {
    build_ui();
    refresh();
}

std::string GitPanel::run_git(const std::vector<std::string>& args, int* exit_code) {
    std::vector<std::string> tokens = {"git"};
    tokens.insert(tokens.end(), args.begin(), args.end());
    ProcessResult r = run_command(build_command(tokens), repo_dir_);
    if (exit_code) *exit_code = r.exit_code;
    return r.output;
}

void GitPanel::set_status_message(const std::string& text, bool is_error) {
    status_message_->set_text(text);
    status_message_->set_foreground(is_error ? kError : kSuccess);
}

// --- Construction de l'interface -------------------------------------

void GitPanel::build_ui() {
    // --- Colonne gauche : branche, staging, message de commit ---------
    branch_label_ = std::make_shared<tui::Label>("");
    branch_label_->set_foreground(kFg);
    branch_label_->set_style(tui::TextStyle::Bold);

    auto staged_badge = std::make_shared<tui::Badge>("Staged");
    staged_badge->set_colors(tui::Color::White(), kBadgeBg);
    staged_list_ = std::make_shared<ShortcutListView>();
    staged_list_->set_colors(kAccent, kFg, kMuted);
    staged_list_->set_show_scrollbar(true);
    staged_list_->bind(U'u', [this] { unstage_selected(); });
    staged_list_->set_on_select([this](size_t i) {
        if (i < staged_list_->items().size()) show_diff_for_change(staged_list_->items()[i].text, true);
    });

    auto changes_badge = std::make_shared<tui::Badge>("Changes");
    changes_badge->set_colors(tui::Color::White(), kBadgeBg);
    changes_list_ = std::make_shared<ShortcutListView>();
    changes_list_->set_colors(kAccent, kFg, kMuted);
    changes_list_->set_show_scrollbar(true);
    changes_list_->bind(U'a', [this] { stage_selected(); });
    changes_list_->bind(U'd', [this] { discard_selected(); });
    changes_list_->bind(U'r', [this] { refresh(); });
    changes_list_->bind(U'p', [this] { push(); });
    changes_list_->bind(U'P', [this] { pull(); });
    changes_list_->bind(U'f', [this] { fetch(); });
    changes_list_->set_on_select([this](size_t i) {
        if (i < changes_list_->items().size()) show_diff_for_change(changes_list_->items()[i].text, false);
    });

    commit_message_ = std::make_shared<tui::Input>();
    commit_message_->set_placeholder("Message de commit...");
    commit_message_->set_on_submit([this](const std::string&) { commit(); });

    auto commit_button = std::make_shared<tui::Button>("Commit");
    commit_button->set_on_click([this] { commit(); });

    auto hints = std::make_shared<tui::ShortcutBar>();
    hints->set_orientation(tui::ShortcutBarOrientation::Vertical);
    hints->set_color(kMuted);
    hints->set_hints({
        {"a", "stage"}, {"u", "unstage"}, {"d", "discard"}, {"Enter", "commit msg"},
        {"p/P/f", "push/pull/fetch"}, {"r", "refresh"},
    });

    status_message_ = std::make_shared<tui::Label>("");

    auto left = std::make_shared<Vertical>();
    left->add_child(branch_label_, LayoutParams::fixed(1));
    left->add_child(staged_badge, LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    left->add_child(staged_list_, LayoutParams::stretch(1));
    left->add_child(changes_badge, LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    left->add_child(changes_list_, LayoutParams::stretch(1));
    left->add_child(commit_message_, LayoutParams::fixed(1));
    left->add_child(commit_button, LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    left->add_child(hints, LayoutParams::fixed(6));

    auto left_panel = std::make_shared<Border>();
    left_panel->set_border_style(BorderStyle::Rounded);
    left_panel->set_color(kBorder);
    left_panel->set_title("Git");
    left_panel->set_child(left);

    // --- Colonne droite : historique / diff / stash / rebase / tags ---
    log_list_ = std::make_shared<ShortcutListView>();
    log_list_->set_style(tui::ListViewStyle::Detailed);
    log_list_->set_colors(kAccent, kFg, kMuted);
    log_list_->set_show_scrollbar(true);
    log_list_->set_on_select([this](size_t i) { select_log_entry(i); });

    commit_meta_ = std::make_shared<tui::Label>("");
    commit_meta_->set_wrap(true);
    commit_meta_->set_foreground(kMuted);

    auto log_page = std::make_shared<Vertical>();
    log_page->add_child(log_list_, LayoutParams::stretch(3));
    log_page->add_child(commit_meta_, LayoutParams::stretch(1));

    diff_view_ = std::make_shared<tui::TextArea>();
    diff_view_->set_read_only(true);
    diff_view_->set_show_scrollbar(true);
    diff_view_->set_accent_color(kAccent);

    stash_list_ = std::make_shared<ShortcutListView>();
    stash_list_->set_colors(kAccent, kFg, kMuted);
    stash_list_->set_show_scrollbar(true);
    stash_list_->bind(U'p', [this] { stash_apply(true); });
    stash_list_->set_on_activate([this](size_t) { stash_apply(false); });
    stash_list_->bind(U'd', [this] { stash_drop(); });
    auto stash_hints = std::make_shared<tui::ShortcutBar>();
    stash_hints->set_orientation(tui::ShortcutBarOrientation::Vertical);
    stash_hints->set_color(kMuted);
    stash_hints->set_hints({{"Enter", "apply"}, {"p", "pop"}, {"d", "drop"}});
    auto stash_page = std::make_shared<Vertical>();
    stash_page->add_child(stash_list_, LayoutParams::stretch(1));
    stash_page->add_child(stash_hints, LayoutParams::fixed(3));

    rebase_list_ = std::make_shared<ShortcutListView>();
    rebase_list_->set_colors(kAccent, kFg, kMuted);
    rebase_list_->set_show_scrollbar(true);
    rebase_list_->bind(U' ', [this] { cycle_rebase_action(rebase_list_->selected_index()); });
    rebase_list_->bind(U'K', [this] { move_rebase_entry(rebase_list_->selected_index(), -1); });
    rebase_list_->bind(U'J', [this] { move_rebase_entry(rebase_list_->selected_index(), 1); });
    rebase_list_->set_on_activate([this](size_t) { run_rebase(); });
    auto rebase_hints = std::make_shared<tui::ShortcutBar>();
    rebase_hints->set_orientation(tui::ShortcutBarOrientation::Vertical);
    rebase_hints->set_color(kMuted);
    rebase_hints->set_hints({
        {"espace", "cycler pick/squash/drop"}, {"K/J", "deplacer haut/bas"}, {"Enter", "lancer le rebase"},
    });
    auto rebase_page = std::make_shared<Vertical>();
    rebase_page->add_child(rebase_list_, LayoutParams::stretch(1));
    rebase_page->add_child(rebase_hints, LayoutParams::fixed(4));

    tag_name_ = std::make_shared<tui::Input>();
    tag_name_->set_placeholder("nom du tag...");
    tag_message_ = std::make_shared<tui::Input>();
    tag_message_->set_placeholder("message (optionnel)...");
    tag_message_->set_on_submit([this](const std::string&) { create_tag(); });
    auto create_tag_button = std::make_shared<tui::Button>("Creer le tag");
    create_tag_button->set_on_click([this] { create_tag(); });
    tags_list_ = std::make_shared<ShortcutListView>();
    tags_list_->set_colors(kAccent, kFg, kMuted);
    auto tags_page = std::make_shared<Vertical>();
    tags_page->add_child(tag_name_, LayoutParams::fixed(1));
    tags_page->add_child(tag_message_, LayoutParams::fixed(1));
    tags_page->add_child(create_tag_button, LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    tags_page->add_child(tags_list_, LayoutParams::stretch(1));

    right_tabs_ = std::make_shared<tui::Tabs>();
    right_tabs_->set_colors(kBorder, kFg, kMuted);
    right_tabs_->add_tab("Historique", log_page);
    right_tabs_->add_tab("Diff", diff_view_);
    right_tabs_->add_tab("Stash", stash_page);
    right_tabs_->add_tab("Rebase", rebase_page);
    right_tabs_->add_tab("Tags", tags_page);

    auto right_panel = std::make_shared<Border>();
    right_panel->set_border_style(BorderStyle::Rounded);
    right_panel->set_color(kBorder);
    right_panel->set_child(right_tabs_);

    auto columns = std::make_shared<Horizontal>();
    columns->add_child(left_panel, LayoutParams::fixed(38));
    columns->add_child(right_panel, LayoutParams::stretch(1));

    auto outer = std::make_shared<Vertical>();
    outer->add_child(columns, LayoutParams::stretch(1));
    outer->add_child(status_message_, LayoutParams::fixed(1));
    root_ = outer;
}

// --- Rafraîchissement --------------------------------------------------

void GitPanel::set_on_quit(std::function<void()> cb) {
    staged_list_->bind(U'q', cb);
    changes_list_->bind(U'q', cb);
}

void GitPanel::refresh() {
    refresh_status();
    refresh_log();
    refresh_stash();
    refresh_tags();
    refresh_rebase_source();
}

void GitPanel::refresh_status() {
    std::string out = run_git({"status", "--porcelain=v1", "-b"});
    auto lines = split_lines(out);

    std::string branch = "?";
    std::vector<ListItem> staged, changes;
    std::vector<std::string> staged_paths, changes_paths; // stockés dans ListItem.text directement

    for (const auto& line : lines) {
        if (line.rfind("## ", 0) == 0) {
            std::string rest = line.substr(3);
            size_t cut = rest.find("...");
            if (cut == std::string::npos) cut = rest.find(' ');
            branch = cut == std::string::npos ? rest : rest.substr(0, cut);
            continue;
        }
        if (line.size() < 4) continue;
        char x = line[0], y = line[1];
        std::string path = line.substr(3);
        size_t arrow = path.find(" -> ");
        if (arrow != std::string::npos) path = path.substr(arrow + 4);

        if (x == '?' && y == '?') {
            changes.push_back(ListItem{path, "", true, "?"});
            continue;
        }
        if (x != ' ') staged.push_back(ListItem{path, "", true, std::string(1, x)});
        if (y != ' ') changes.push_back(ListItem{path, "", true, std::string(1, y)});
    }

    branch_label_->set_text("Branche: " + branch);
    staged_list_->set_items(std::move(staged));
    changes_list_->set_items(std::move(changes));
}

void GitPanel::refresh_log() {
    std::string out = run_git({"log", "--graph", "--format=%x01%H%x01%an%x01%ar%x01%s", "-n", "200"});
    auto lines = split_lines(out);

    std::vector<ListItem> items;
    log_hashes_.clear();
    for (const auto& line : lines) {
        size_t marker = line.find('\x01');
        if (marker == std::string::npos) {
            items.push_back(ListItem{line, "", false, ""});
            log_hashes_.push_back("");
            continue;
        }
        std::string prefix = line.substr(0, marker);
        auto fields = split_soh(line.substr(marker + 1));
        if (fields.size() < 4) continue;
        const std::string& hash = fields[0];
        const std::string& author = fields[1];
        const std::string& date = fields[2];
        const std::string& subject = fields[3];
        items.push_back(ListItem{prefix + " " + subject, author + " - " + date, true, ""});
        log_hashes_.push_back(hash);
    }
    log_list_->set_items(std::move(items));
    commit_meta_->set_text("");
}

void GitPanel::refresh_stash() {
    std::string out = run_git({"stash", "list"});
    std::vector<ListItem> items;
    for (const auto& line : split_lines(out)) items.push_back(ListItem{line, "", true, ""});
    stash_list_->set_items(std::move(items));
}

void GitPanel::refresh_tags() {
    std::string out = run_git({"tag", "-l", "--sort=-creatordate"});
    std::vector<ListItem> items;
    for (const auto& line : split_lines(out)) {
        if (!line.empty()) items.push_back(ListItem{line, "", true, ""});
    }
    tags_list_->set_items(std::move(items));
}

void GitPanel::refresh_rebase_source() {
    std::string out = run_git({"log", "--no-merges", "--format=%h%x01%s", "-n", "20"});
    auto lines = split_lines(out);
    rebase_entries_.clear();
    for (auto it = lines.rbegin(); it != lines.rend(); ++it) { // le plus ancien d'abord (ordre attendu par le todo de rebase)
        auto fields = split_soh(*it);
        if (fields.size() < 2) continue;
        rebase_entries_.push_back({fields[0], fields[1], "pick"});
    }

    std::vector<ListItem> items;
    for (const auto& e : rebase_entries_) items.push_back(ListItem{e.hash + "  " + e.subject, "", true, e.action});
    rebase_list_->set_items(std::move(items));
}

// --- Staging -------------------------------------------------------------

void GitPanel::stage_selected() {
    if (changes_list_->items().empty()) return;
    std::string path = changes_list_->items()[changes_list_->selected_index()].text;
    int code = 0;
    run_git({"add", "--", path}, &code);
    set_status_message(code == 0 ? ("Ajoute a l'index : " + path) : ("Echec git add : " + path), code != 0);
    refresh_status();
}

void GitPanel::unstage_selected() {
    if (staged_list_->items().empty()) return;
    std::string path = staged_list_->items()[staged_list_->selected_index()].text;
    int code = 0;
    run_git({"restore", "--staged", "--", path}, &code);
    set_status_message(code == 0 ? ("Retire de l'index : " + path) : ("Echec unstage : " + path), code != 0);
    refresh_status();
}

void GitPanel::discard_selected() {
    if (changes_list_->items().empty()) return;
    std::string path = changes_list_->items()[changes_list_->selected_index()].text;
    int code = 0;
    run_git({"checkout", "--", path}, &code);
    set_status_message(code == 0 ? ("Modifications annulees : " + path) : ("Echec discard : " + path), code != 0);
    refresh_status();
}

void GitPanel::commit() {
    std::string msg = trim(commit_message_->text());
    if (msg.empty()) { set_status_message("Message de commit vide.", true); return; }
    int code = 0;
    std::string out = run_git({"commit", "-m", msg}, &code);
    set_status_message(code == 0 ? "Commit cree." : ("Echec du commit : " + trim(out)), code != 0);
    if (code == 0) commit_message_->set_text("");
    refresh();
}

void GitPanel::push() {
    int code = 0;
    std::string out = run_git({"push"}, &code);
    set_status_message(code == 0 ? "Push effectue." : ("Echec push : " + trim(out)), code != 0);
}

void GitPanel::pull() {
    int code = 0;
    std::string out = run_git({"pull"}, &code);
    set_status_message(code == 0 ? "Pull effectue." : ("Echec pull : " + trim(out)), code != 0);
    refresh();
}

void GitPanel::fetch() {
    int code = 0;
    std::string out = run_git({"fetch"}, &code);
    set_status_message(code == 0 ? "Fetch effectue." : ("Echec fetch : " + trim(out)), code != 0);
}

// --- Historique / diff ---------------------------------------------------

void GitPanel::select_log_entry(size_t index) {
    if (index >= log_hashes_.size() || log_hashes_[index].empty()) {
        commit_meta_->set_text("");
        return;
    }
    const std::string& hash = log_hashes_[index];
    std::string meta = run_git({"show", "--no-patch", "--format=Hash: %H%nAuteur: %an <%ae>%nDate: %ad%n%n%s%n%n%b", hash});
    commit_meta_->set_text(trim(meta));
    std::string diff = run_git({"show", hash});
    set_diff_text(diff);
}

void GitPanel::show_diff_for_change(const std::string& path, bool staged) {
    std::string diff = staged ? run_git({"diff", "--cached", "--", path}) : run_git({"diff", "--", path});
    if (trim(diff).empty() && !staged) {
        // fichier untracked : pas de diff au sens git, on affiche le contenu tel quel
        diff = run_git({"diff", "--no-index", "--", "/dev/null", path});
    }
    set_diff_text(diff);
    right_tabs_->set_active_index(1); // bascule sur l'onglet Diff
}

void GitPanel::set_diff_text(const std::string& diff_text) {
    diff_view_->set_text(diff_text);
    auto colors = std::make_shared<std::vector<tui::Color>>();
    colors->reserve(diff_view_->line_count());
    for (size_t i = 0; i < diff_view_->line_count(); ++i) {
        std::string line = tui::TextHelper::encode_utf8(diff_view_->line(i));
        tui::Color c = kFg;
        if (line.rfind("+++", 0) == 0 || line.rfind("---", 0) == 0) c = kMuted;
        else if (line.rfind("@@", 0) == 0) c = kAccent;
        else if (line.rfind("diff ", 0) == 0 || line.rfind("index ", 0) == 0) c = kMuted;
        else if (!line.empty() && line[0] == '+') c = kSuccess;
        else if (!line.empty() && line[0] == '-') c = kError;
        colors->push_back(c);
    }
    diff_view_->set_style_hook([colors](int line, int) -> tui::CharStyle {
        if (line < 0 || static_cast<size_t>(line) >= colors->size()) return {};
        return tui::CharStyle{tui::TextStyle::None, (*colors)[static_cast<size_t>(line)]};
    });
}

// --- Stash -----------------------------------------------------------------

void GitPanel::stash_apply(bool pop) {
    if (stash_list_->items().empty()) return;
    std::string ref = "stash@{" + std::to_string(stash_list_->selected_index()) + "}";
    int code = 0;
    run_git({pop ? "stash" : "stash", pop ? "pop" : "apply", ref}, &code);
    set_status_message(code == 0 ? (pop ? "Stash applique et retire." : "Stash applique.") : "Echec de l'application du stash.",
                        code != 0);
    refresh();
}

void GitPanel::stash_drop() {
    if (stash_list_->items().empty()) return;
    std::string ref = "stash@{" + std::to_string(stash_list_->selected_index()) + "}";
    int code = 0;
    run_git({"stash", "drop", ref}, &code);
    set_status_message(code == 0 ? "Stash supprime." : "Echec de la suppression du stash.", code != 0);
    refresh_stash();
}

// --- Rebase interactif simplifie -------------------------------------------

void GitPanel::cycle_rebase_action(size_t index) {
    if (index >= rebase_entries_.size()) return;
    auto& action = rebase_entries_[index].action;
    action = action == "pick" ? "squash" : action == "squash" ? "drop" : "pick";
    std::vector<ListItem> items;
    for (const auto& e : rebase_entries_) items.push_back(ListItem{e.hash + "  " + e.subject, "", true, e.action});
    size_t sel = rebase_list_->selected_index();
    rebase_list_->set_items(std::move(items));
    rebase_list_->set_selected_index(sel);
}

void GitPanel::move_rebase_entry(size_t index, int delta) {
    long target = static_cast<long>(index) + delta;
    if (target < 0 || target >= static_cast<long>(rebase_entries_.size())) return;
    std::swap(rebase_entries_[index], rebase_entries_[static_cast<size_t>(target)]);
    std::vector<ListItem> items;
    for (const auto& e : rebase_entries_) items.push_back(ListItem{e.hash + "  " + e.subject, "", true, e.action});
    rebase_list_->set_items(std::move(items));
    rebase_list_->set_selected_index(static_cast<size_t>(target));
}

void GitPanel::run_rebase() {
    if (rebase_entries_.empty()) return;

    std::string todo;
    for (const auto& e : rebase_entries_) todo += e.action + " " + e.hash + " " + e.subject + "\n";

    std::string tmpfile = "/tmp/.ide_rebase_todo_" + std::to_string(getpid());
    { std::ofstream f(tmpfile); f << todo; }

    std::string base = rebase_entries_.front().hash + "^";
    std::string command = "GIT_SEQUENCE_EDITOR=" + shell_quote("cp " + tmpfile) + " git rebase -i " + shell_quote(base);
    ProcessResult r = run_command(command, repo_dir_);
    std::remove(tmpfile.c_str());

    set_status_message(r.exit_code == 0 ? "Rebase termine."
                                         : "Rebase interrompu (conflit ?) - terminez depuis le Terminal (git status).",
                        r.exit_code != 0);
    refresh();
}

// --- Tags --------------------------------------------------------------

void GitPanel::create_tag() {
    std::string name = trim(tag_name_->text());
    if (name.empty()) { set_status_message("Nom de tag vide.", true); return; }
    std::string msg = trim(tag_message_->text());
    std::vector<std::string> args = {"tag"};
    if (!msg.empty()) { args.push_back("-a"); args.push_back(name); args.push_back("-m"); args.push_back(msg); }
    else { args.push_back(name); }
    int code = 0;
    run_git(args, &code);
    set_status_message(code == 0 ? ("Tag cree : " + name) : "Echec de creation du tag.", code != 0);
    if (code == 0) { tag_name_->set_text(""); tag_message_->set_text(""); }
    refresh_tags();
}

} // namespace ide
