/* @file model/Workspace.hpp
 *
 * Projet chargé en mémoire : project.toml + layers + modules + applications,
 * références de dépendances résolues et configuration validée.
 *
 * Références de modules (dependencies.modules, sélection en ligne de
 * commande) :
 *   « core.log »   identifiant complet <layer>.<module>
 *   « log »        nom court, s'il est unique (ou présent dans le même layer)
 *   « core.* »     tous les modules du layer core
 *   « core »       idem, si aucun module ne s'appelle « core »
 */
#pragma once

#include "model/Model.hpp"

#include <optional>
#include <string>
#include <vector>

namespace model {

struct LoadOptions {
    bool resolve_git = true;     ///< importe les dépendances git (réseau/cache)
    bool update_git = false;     ///< ignore cpp-maker.lock
    bool validate = true;        ///< lève une erreur si la configuration est invalide
};

class Workspace {
public:
    fs::path root;
    ProjectConfig project;
    std::vector<LayerConfig> layers;     ///< dans l'ordre des couches
    std::vector<TargetConfig> targets;   ///< modules (par layer) puis applications
    std::map<std::string, std::string> git_lock;   ///< url → commit
    std::vector<std::string> warnings;   ///< clés inconnues, etc.

    /// Remonte depuis `from` jusqu'au dossier contenant project.toml.
    static std::optional<fs::path> find_root(const fs::path& from);
    /// Charge le projet contenant `from` (erreur s'il n'y en a pas).
    static Workspace load(const fs::path& from, const LoadOptions& o = {});
    static Workspace load_root(const fs::path& root, const LoadOptions& o = {});

    // --- Recherche
    [[nodiscard]] const TargetConfig* find(const std::string& id) const;
    TargetConfig* find(const std::string& id);
    [[nodiscard]] const LayerConfig* find_layer(const std::string& name) const;
    [[nodiscard]] std::vector<const TargetConfig*> modules_of(const std::string& layer) const;
    [[nodiscard]] std::vector<const TargetConfig*> applications() const;
    [[nodiscard]] std::vector<const TargetConfig*> modules() const;
    [[nodiscard]] std::vector<std::string> ids() const;

    /// Résout une référence de module (voir en-tête). `context_layer` :
    /// layer de la cible qui déclare la dépendance (priorité aux voisins).
    [[nodiscard]] std::vector<std::string> resolve_module_ref(const std::string& ref,
                                                              const std::string& context_layer = {}) const;
    [[nodiscard]] std::vector<std::string> resolve_module_ref_noexcept(const std::string& ref,
                                                                       const std::string& context_layer = {}) const;
    /// Résout un sélecteur de ligne de commande (applications comprises).
    [[nodiscard]] std::vector<std::string> resolve_selector(const std::string& sel) const;
    /// Idem sans exception (vide si le sélecteur ne désigne rien).
    [[nodiscard]] std::vector<std::string> resolve_selector_noexcept(const std::string& sel) const;
    /// Cibles sélectionnées par une liste de sélecteurs ; vide = cible du
    /// dossier courant, sinon tout le projet.
    [[nodiscard]] std::vector<std::string> select(const std::vector<std::string>& selectors,
                                                  const fs::path& cwd) const;
    /// Cible (ou layer) contenant `cwd`.
    [[nodiscard]] std::vector<std::string> targets_at(const fs::path& cwd) const;

    /// Ordre topologique (dépendances d'abord) de `roots` et de leur fermeture.
    [[nodiscard]] std::vector<std::string> topo_order(const std::vector<std::string>& roots) const;
    /// Fermeture transitive des dépendances (sans `id`), dépendances d'abord.
    [[nodiscard]] std::vector<std::string> closure(const std::string& id) const;
    /// Cibles qui dépendent directement de `id`.
    [[nodiscard]] std::vector<std::string> dependents(const std::string& id) const;
    /// Rang d'un layer dans [layers].order (-1 si non ordonné).
    [[nodiscard]] int layer_rank(const std::string& layer) const;

    /// Liste des erreurs de configuration (vide si valide).
    [[nodiscard]] std::vector<std::string> validate() const;

    // --- Chemins
    /// Premier dossier de layers (création de nouveaux layers).
    [[nodiscard]] fs::path layers_path() const { return root / project.layers_dirs.front(); }
    [[nodiscard]] std::vector<fs::path> layers_paths() const {
        std::vector<fs::path> r;
        for (auto& d : project.layers_dirs) r.push_back(root / d);
        return r;
    }
    /// Premier dossier d'applications (création de nouvelles applications).
    [[nodiscard]] fs::path apps_path() const { return root / project.apps_dirs.front(); }
    [[nodiscard]] std::vector<fs::path> apps_paths() const {
        std::vector<fs::path> r;
        for (auto& d : project.apps_dirs) r.push_back(root / d);
        return r;
    }
    [[nodiscard]] fs::path build_path() const { return root / project.build_dir; }
    [[nodiscard]] fs::path dist_path() const { return root / project.dist_dir; }
    [[nodiscard]] fs::path abs(const TargetConfig& t) const { return root / t.dir; }

private:
    std::vector<std::string> unresolved_;   ///< références de dépendances non résolues
    bool git_imported_ = false;

    void load_project_file();
    void discover();
    void resolve_dependencies();
    void import_git(bool update);
};

} // namespace model
