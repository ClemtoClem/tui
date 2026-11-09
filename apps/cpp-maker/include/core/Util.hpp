/* @file core/Util.hpp
 *
 * Utilitaires transverses de cpp-maker : chaînes, chemins, fichiers,
 * empreintes FNV-1a, échappements (shell, JSON, XML) et verrou de projet.
 * Aucune dépendance vers le reste de l'application.
 */
#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace fs = std::filesystem;

namespace util {

/// Erreur « utilisateur » : message affiché tel quel, code de sortie 1.
/// Toutes les couches lèvent cette exception plutôt que d'appeler exit().
struct Error : std::runtime_error {
    explicit Error(const std::string& m) : std::runtime_error(m) {}
};

/// Erreur d'usage d'une commande : main() affiche en plus l'aide courte.
struct UsageError : Error {
    explicit UsageError(const std::string& m) : Error(m) {}
};

template <class... Args>
[[noreturn]] void fail(std::format_string<Args...> f, Args&&... a) {
    throw Error(std::format(f, std::forward<Args>(a)...));
}

// --- Chaînes -----------------------------------------------------------------
std::string trim(std::string_view s);
std::string lower(std::string s);
std::string upper(std::string s);
std::string join(const std::vector<std::string>& v, std::string_view sep);
std::vector<std::string> split(std::string_view s, char sep, bool keep_empty = false);
/// Découpe sur les virgules en supprimant les blancs et les éléments vides.
std::vector<std::string> split_list(std::string_view s, char sep = ',');
std::vector<std::string> split_ws(std::string_view s);
std::string replace_all(std::string s, std::string_view from, std::string_view to);
bool contains(const std::vector<std::string>& v, std::string_view x);
/// Ajoute `x` à `v` s'il n'y est pas déjà ; renvoie vrai si ajouté.
bool push_unique(std::vector<std::string>& v, const std::string& x);
void push_unique(std::vector<std::string>& v, const std::vector<std::string>& xs);
/// Identifiant C/C++ dérivé d'un nom (« my-lib » → « my_lib »).
std::string identifier(std::string_view name);
/// Nom de fichier/dossier sûr : [A-Za-z0-9_.+-], ni vide, ni « . », ni « .. ».
bool is_safe_name(std::string_view s);
/// Distance de Levenshtein (suggestions « vouliez-vous dire… »).
size_t edit_distance(std::string_view a, std::string_view b);
/// Meilleure suggestion parmi `candidates` (vide si rien de proche).
std::string suggest(std::string_view word, const std::vector<std::string>& candidates);
/// Comparaison de versions « 1.2.10 » > « 1.2.9 ».
int compare_versions(std::string_view a, std::string_view b);

// --- Échappements ------------------------------------------------------------
std::string shell_quote(const std::string& a);
std::string shell_join(const std::vector<std::string>& v);
std::string json_escape(std::string_view s);
std::string xml_escape(std::string_view s);

// --- Fichiers ----------------------------------------------------------------
bool read_file(const fs::path& p, std::string& out);
std::string read_file_or_throw(const fs::path& p);
/// Écriture atomique (fichier temporaire + rename) ; crée les dossiers parents.
void write_file(const fs::path& p, const std::string& content);
/// N'écrit que si le contenu diffère (préserve le mtime sinon).
bool write_file_if_changed(const fs::path& p, const std::string& content);
/// `p` relatif à `dir` (sauf s'il est absolu), normalisé.
fs::path under(const fs::path& dir, const fs::path& p);
/// Chemin affichable relatif à `base` si possible.
std::string display(const fs::path& p, const fs::path& base);
std::optional<fs::path> find_in_path(const std::string& name);
inline bool have(const std::string& name) { return find_in_path(name).has_value(); }
/// Chemin absolu de l'exécutable courant (/proc/self/exe).
fs::path self_exe();

// --- Divers ------------------------------------------------------------------
std::string elapsed(std::chrono::steady_clock::time_point t0);
std::string human_size(uintmax_t n);

/// Empreinte FNV-1a 64 bits (incrémental compilé).
struct Hasher {
    uint64_t h = 1469598103934665603ULL;
    void feed(std::string_view s);
    void feed_u64(uint64_t v);
    [[nodiscard]] std::string hex() const { return std::format("{:016x}", h); }
};

/// Verrou exclusif (flock) sur un fichier, tenu tant que l'objet vit.
/// Une seconde instance attend en affichant le PID du détenteur.
class FileLock {
public:
    FileLock() = default;
    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;
    FileLock(FileLock&& o) noexcept : fd_(o.fd_) { o.fd_ = -1; }
    FileLock& operator=(FileLock&& o) noexcept;
    ~FileLock() { release(); }

    bool acquire(const fs::path& p, std::string_view waiting_message = {});
    void release();
    [[nodiscard]] bool held() const { return fd_ >= 0; }

private:
    int fd_ = -1;
};

} // namespace util
