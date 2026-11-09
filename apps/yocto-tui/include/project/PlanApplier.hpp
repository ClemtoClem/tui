/* apps/yocto-tui/project/PlanApplier.hpp */
#pragma once

#include "LayerGenerator.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace yocto {

enum class EntryStatus {
    New,        // le fichier n'existe pas encore
    Modified,   // contenu ou mode différent
    Unchanged,  // identique : rien à faire
    Removed,    // généré précédemment, plus produit : supprimé
    Missing,    // fichier de configuration du build absent (build non initialisé)
};

[[nodiscard]] const char* status_icon(EntryStatus s);
[[nodiscard]] const char* status_text(EntryStatus s);

/// Une ligne de l'aperçu : ce qui sera fait sur un fichier.
struct PreviewEntry {
    std::filesystem::path path;
    std::string display;          // chemin relatif lisible
    EntryStatus status = EntryStatus::New;
    std::string old_content;
    std::string new_content;
    unsigned mode = 0644;
    std::string origin;
    bool binary = false;          // copie d'un fichier binaire : pas d'aperçu
    std::filesystem::path copy_from;
    bool in_layer = true;         // false : local.conf / bblayers.conf
};

struct ApplyReport {
    int written = 0;
    int unchanged = 0;
    int removed = 0;
    std::vector<std::string> errors;
    [[nodiscard]] bool ok() const { return errors.empty(); }
};

using LogFn = std::function<void(const std::string&)>;

inline constexpr const char* kManifestName = ".yocto-tui-manifest";

/// Calcule l'aperçu des fichiers du layer : fichiers générés, copies
/// des sources hôte, et suppression des fichiers générés lors d'une
/// application précédente qui ne le sont plus (d'après le manifeste).
[[nodiscard]] std::vector<PreviewEntry> preview_layer(const GenerationPlan& plan);

/// Aperçu des modifications de local.conf et bblayers.conf.
[[nodiscard]] std::vector<PreviewEntry> preview_build_conf(const GenerationPlan& plan);

/// Écrit les fichiers du layer et met à jour le manifeste. En mode
/// `dry_run`, ne fait que journaliser ce qui serait fait.
ApplyReport apply_layer(const GenerationPlan& plan, bool dry_run, const LogFn& log);

/// Met à jour les blocs gérés de local.conf et bblayers.conf.
ApplyReport apply_build_conf(const GenerationPlan& plan, bool dry_run, const LogFn& log);

/// Lit le manifeste du layer (chemins relatifs des fichiers générés).
[[nodiscard]] std::vector<std::string> read_manifest(const std::filesystem::path& layer_dir);

// --- Diff ligne à ligne ----------------------------------------------------

struct DiffLine {
    char op;            // ' ' identique, '-' supprimée, '+' ajoutée
    std::string text;
};

/// Diff par plus longue sous-séquence commune, après retrait des
/// préfixe/suffixe communs. Au-delà de `max_lines` lignes différentes
/// de chaque côté, renvoie un diff grossier (tout supprimé / tout
/// ajouté) pour borner le coût.
[[nodiscard]] std::vector<DiffLine> line_diff(const std::string& a, const std::string& b,
                                              size_t max_lines = 2000);

/// Rendu texte d'un diff, avec `context` lignes de contexte autour des
/// changements (les autres sont résumées par « ... »).
[[nodiscard]] std::string format_diff(const std::vector<DiffLine>& diff, size_t context = 3);

/// Lit un fichier entier ; "" s'il n'existe pas.
[[nodiscard]] std::string read_file(const std::filesystem::path& p, bool* exists = nullptr);

} // namespace yocto
