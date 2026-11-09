/* apps/yocto-tui/project/LayerGenerator.hpp */
#pragma once

#include "../config/YoctoConfig.hpp"
#include "ProjectSpec.hpp"

#include <filesystem>
#include <functional>
#include <string>
#include <vector>

namespace yocto {

/// Fichier du layer dont le contenu est entièrement produit par le
/// générateur.
struct PlannedFile {
    std::filesystem::path path;   // absolu
    std::string content;
    unsigned mode = 0644;
    std::string origin;           // ex : "recette formation-apps"
};

/// Copie d'un répertoire de sources de l'hôte dans le layer
/// (applications SourceKind::HostDir).
struct PlannedCopy {
    std::filesystem::path from_dir;
    std::filesystem::path to_dir;
    std::string origin;
};

/// Bloc géré dans un fichier de configuration existant du build
/// (local.conf, bblayers.conf), délimité par deux marqueurs : le reste
/// du fichier n'est jamais touché, ce qui laisse l'utilisateur libre de
/// l'éditer à la main.
struct ManagedEdit {
    std::filesystem::path path;
    std::string block_body;                 // local.conf : lignes du bloc
    std::vector<std::string> layer_paths;   // bblayers.conf : couches à ajouter
    bool is_bblayers = false;
    std::string origin;
};

struct GenerationPlan {
    std::filesystem::path layer_dir;
    std::vector<PlannedFile> files;
    std::vector<PlannedCopy> copies;
    std::vector<ManagedEdit> edits;
    std::vector<Issue> issues;              // validation + problèmes de génération
};

/// Calcule le hash crypt(3) SHA-512 d'un mot de passe avec un sel
/// donné ; retourne "" en cas d'échec. Injectable pour les tests.
using PasswordHasher = std::function<std::string(const std::string& password, const std::string& salt)>;

/// Hasheur par défaut : `openssl passwd -6 -salt <sel> -stdin`.
[[nodiscard]] std::string openssl_password_hasher(const std::string& password, const std::string& salt);

/// Sel déterministe (16 caractères [./0-9A-Za-z]) dérivé de `seed` :
/// régénérer le layer sans changer le mot de passe ne modifie pas la
/// recette d'image, donc ne déclenche pas de rebuild inutile.
[[nodiscard]] std::string derive_salt(const std::string& seed);

/// Produit, à partir de la configuration et du projet, l'ensemble des
/// fichiers du layer et des modifications de configuration du build.
/// Ne lit ni n'écrit rien sur le disque : l'aperçu et l'application
/// sont faits par PlanApplier.
class LayerGenerator {
public:
    LayerGenerator(YoctoConfig config, PasswordHasher hasher);

    [[nodiscard]] GenerationPlan generate() const;

    // --- Aides exposées pour les tests ---

    /// ${WORKDIR} jusqu'à scarthgap, ${UNPACKDIR} à partir de styhead.
    [[nodiscard]] static std::string unpack_var(const std::string& poky_branch);
    /// /usr/bin/foo -> ${bindir}/foo, /etc/x -> ${sysconfdir}/x...
    [[nodiscard]] static std::string install_path_var(const std::string& abs_path);
    /// https://host/repo.git -> git://host/repo.git;protocol=https;branch=...
    [[nodiscard]] static std::string git_src_uri(const std::string& url, const std::string& branch);
    /// Catégorie recipes-* d'une recette connue de poky (bbappends).
    [[nodiscard]] static std::string recipe_category(const std::string& recipe);
    /// Échappe les '$' d'un hash crypt pour EXTRA_USERS_PARAMS.
    [[nodiscard]] static std::string escape_hash(const std::string& hash);

    /// Chemins des couches à déclarer dans bblayers.conf.
    [[nodiscard]] std::vector<std::string> layer_paths() const;

private:
    [[nodiscard]] std::filesystem::path layer_dir() const;
    void gen_layer_conf(GenerationPlan& plan) const;
    void gen_apps(GenerationPlan& plan) const;
    void gen_rootfs_and_appends(GenerationPlan& plan) const;
    void gen_image(GenerationPlan& plan) const;
    void gen_build_conf(GenerationPlan& plan) const;
    void gen_readme(GenerationPlan& plan) const;

    YoctoConfig config_;
    PasswordHasher hasher_;
};

// --- Blocs gérés -------------------------------------------------------------

inline constexpr const char* kLocalConfBegin = "# >>> yocto-tui : projet (bloc géré automatiquement, ne pas éditer) >>>";
inline constexpr const char* kLocalConfEnd   = "# <<< yocto-tui : projet <<<";
inline constexpr const char* kBblayersBegin  = "# >>> yocto-tui : couches du projet (bloc géré automatiquement) >>>";
inline constexpr const char* kBblayersEnd    = "# <<< yocto-tui : couches du projet <<<";

/// Remplace (ou ajoute en fin de fichier) le bloc compris entre `begin`
/// et `end`. Un `body` vide supprime le bloc. Idempotent.
[[nodiscard]] std::string merge_managed_block(const std::string& existing, const std::string& begin,
                                              const std::string& end, const std::string& body);

/// Contenu final d'un fichier de configuration après application de
/// `edit` sur `existing`. Pour bblayers.conf, les couches déjà déclarées
/// hors du bloc géré ne sont pas répétées (BitBake refuse les doublons).
[[nodiscard]] std::string render_managed_edit(const ManagedEdit& edit, const std::string& existing);

} // namespace yocto
