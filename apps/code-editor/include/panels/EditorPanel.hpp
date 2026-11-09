#pragma once

#include <tui/widget/layout/Border.hpp>
#include <tui/widgets/CodeEditor.hpp>

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace ide {

/// Enveloppe autour de tui::CodeEditor : lecture/écriture disque et
/// fourniture du blame (le widget générique ne sait rien de git ni du
/// système de fichiers, voir sa doc).
class EditorPanel {
public:
    EditorPanel();

    [[nodiscard]] std::shared_ptr<tui::Widget> root() const { return panel_; }
    [[nodiscard]] std::shared_ptr<tui::CodeEditor> editor() const { return editor_; }

    /// Ouvre `path` dans un nouvel onglet (ou bascule sur l'onglet déjà
    /// ouvert pour ce fichier s'il existe). Erreurs de lecture ignorées
    /// silencieusement au niveau du fichier (rien à éditer dans ce cas).
    void open_file(const std::string& path);

    /// Écrit le contenu de l'onglet actif sur disque. Retourne faux si
    /// aucun onglet actif ou si l'écriture échoue.
    bool save_active();

    void set_on_status(std::function<void(const std::string&)> cb) { on_status_ = std::move(cb); }

    void set_repo_dir(std::string repo_dir) { repo_dir_ = std::move(repo_dir); }
    void set_show_blame(bool show);
    void set_confirm_before_close(bool confirm) { confirm_before_close_ = confirm; }

private:
    std::string blame_for(const std::string& path, int line);
    const std::vector<std::string>& blame_lines_for(const std::string& path);

    std::shared_ptr<tui::CodeEditor> editor_;
    std::shared_ptr<tui::Border> panel_;
    std::string repo_dir_;
    bool confirm_before_close_ = true;
    std::unordered_map<std::string, std::vector<std::string>> blame_cache_;
    std::function<void(const std::string&)> on_status_;
};

} // namespace ide
