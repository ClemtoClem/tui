#pragma once

#include <tui/widget/layout/Border.hpp>
#include <tui/widgets/TreeView.hpp>

#include <functional>
#include <memory>
#include <string>

namespace ide {

/// Navigateur de fichiers : TreeView sur le vrai système de fichiers,
/// expansion paresseuse par répertoire (TreeNode::lazy_expand), racine
/// remplaçable via open_project() ("ouvrir un projet").
class FileBrowser {
public:
    explicit FileBrowser(std::string root_path);

    [[nodiscard]] std::shared_ptr<tui::Widget> root() const { return panel_; }
    [[nodiscard]] std::shared_ptr<tui::TreeView> tree() const { return tree_; }
    [[nodiscard]] const std::string& root_path() const { return root_path_; }

    /// Appelé quand l'utilisateur active (Entrée/clic) un fichier
    /// (pas un répertoire) - avec son chemin absolu.
    void set_on_open_file(std::function<void(const std::string&)> cb) { on_open_ = std::move(cb); }

    /// 'q' quitte l'application quand l'arbre de fichiers a le focus -
    /// même convention que le reste des exemples (DataTable, SpreadsheetGrid).
    void set_on_quit(std::function<void()> cb);

    void open_project(const std::string& path);

private:
    void rebuild();

    std::string root_path_;
    std::shared_ptr<tui::TreeView> tree_;
    std::shared_ptr<tui::Border> panel_;
    std::function<void(const std::string&)> on_open_;
};

} // namespace ide
