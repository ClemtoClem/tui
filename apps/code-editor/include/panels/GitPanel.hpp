#pragma once

#include <tui/widget/layout/Border.hpp>
#include <tui/widget/layout/Tabs.hpp>
#include <tui/widgets/Button.hpp>
#include <tui/widgets/Input.hpp>
#include <tui/widgets/Label.hpp>
#include <tui/widgets/ListView.hpp>
#include <tui/widgets/ShortcutBar.hpp>
#include <tui/widgets/TextArea.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ide {

/// ListView générique avec liaisons touche -> action, pour les raccourcis
/// spécifiques à chaque liste du panneau Git (stage/unstage/push...) sans
/// dupliquer une sous-classe par liste (voir QuitListView de ci-dashboard
/// pour le schéma d'origine : ici généralisé à plusieurs touches).
class ShortcutListView : public tui::ListView {
public:
    void bind(char32_t key, std::function<void()> action) { bindings_[key] = std::move(action); }

    bool on_key(const tui::KeyEvent& e) override {
        if (e.key == tui::Key::Char) {
            auto it = bindings_.find(e.codepoint);
            if (it != bindings_.end()) { it->second(); return true; }
        }
        return tui::ListView::on_key(e);
    }

private:
    std::unordered_map<char32_t, std::function<void()>> bindings_;
};

/// Panneau "Gestionnaire Git" : staging (index), historique/graphe de
/// commits, diff, stash, rebase interactif simplifié, tags. Toutes les
/// commandes git passent par util/Shell.hpp (popen, arguments
/// systématiquement shell_quote()-és).
///
/// Portée assumée (documenté, pas oublié) :
/// - Diff/staging au niveau fichier entier, pas par hunk.
/// - Le rebase interactif suppose un historique récent SANS merge dans la
///   plage sélectionnée (--no-merges) ; un conflit laisse le dépôt en
///   rebase en cours, à terminer depuis le panneau Terminal
///   (git status / git rebase --continue|--abort).
/// - Pas de résolution de conflits visuelle : les conflits redirigent
///   vers le Terminal.
class GitPanel {
public:
    explicit GitPanel(std::string repo_dir);

    [[nodiscard]] std::shared_ptr<tui::Widget> root() const { return root_; }
    [[nodiscard]] std::shared_ptr<tui::Widget> initial_focus() const { return changes_list_; }

    /// Relit intégralement l'état (status, log, stash, tags) depuis git.
    void refresh();

    /// 'q' quitte l'application depuis les listes staged/changes (même
    /// convention que le reste des exemples).
    void set_on_quit(std::function<void()> cb);

private:
    void build_ui();
    /// Exécute `git <args>` dans repo_dir_ (bloquant, court - voir Shell.hpp).
    std::string run_git(const std::vector<std::string>& args, int* exit_code = nullptr);
    void set_status_message(const std::string& text, bool is_error);

    void refresh_status();
    void refresh_log();
    void refresh_stash();
    void refresh_tags();
    void refresh_rebase_source();

    void stage_selected();
    void unstage_selected();
    void discard_selected();
    void commit();
    void push();
    void pull();
    void fetch();

    void select_log_entry(size_t index);
    void show_diff_for_change(const std::string& path, bool staged);
    void set_diff_text(const std::string& diff_text);

    void stash_apply(bool pop);
    void stash_drop();

    void cycle_rebase_action(size_t index);
    void move_rebase_entry(size_t index, int delta);
    void run_rebase();

    void create_tag();

    std::string repo_dir_;
    std::shared_ptr<tui::Widget> root_;

    std::shared_ptr<tui::Label> branch_label_;
    std::shared_ptr<ShortcutListView> staged_list_;
    std::shared_ptr<ShortcutListView> changes_list_;
    std::shared_ptr<tui::Input> commit_message_;
    std::shared_ptr<tui::Label> status_message_;

    std::shared_ptr<tui::Tabs> right_tabs_;
    std::shared_ptr<ShortcutListView> log_list_;
    std::vector<std::string> log_hashes_;
    std::shared_ptr<tui::Label> commit_meta_;
    std::shared_ptr<tui::TextArea> diff_view_;

    std::shared_ptr<ShortcutListView> stash_list_;

    std::shared_ptr<ShortcutListView> rebase_list_;
    struct RebaseEntry {
        std::string hash;
        std::string subject;
        std::string action; // "pick" | "squash" | "drop"
    };
    std::vector<RebaseEntry> rebase_entries_;

    std::shared_ptr<tui::Input> tag_name_;
    std::shared_ptr<tui::Input> tag_message_;
    std::shared_ptr<ShortcutListView> tags_list_;
};

} // namespace ide
