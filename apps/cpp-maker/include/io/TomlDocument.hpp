/* @file io/TomlDocument.hpp
 *
 * Document TOML modifiable « en place » : les modifications portent sur le
 * texte source, si bien que les commentaires, l'ordre et la mise en forme
 * des lignes non touchées sont conservés.
 *
 *   auto doc = toml::Document::load("project.toml");
 *   doc.set({"build"}, "jobs", toml::Value(4));
 *   doc.remove({"package"}, "strip");
 *   doc.save();
 *
 * Chaque modification est re-analysée : une opération qui produirait un
 * fichier invalide lève toml::ParseError et laisse le document intact.
 */
#pragma once

#include "io/toml.hpp"

#include <filesystem>
#include <string>
#include <vector>

namespace toml {

class Document {
public:
    Document() = default;
    static Document load(const std::filesystem::path& p);
    static Document from_text(std::string text, std::filesystem::path p = {});

    [[nodiscard]] const Value& data() const { return data_; }
    [[nodiscard]] const std::string& text() const { return text_; }
    [[nodiscard]] const std::filesystem::path& path() const { return path_; }
    [[nodiscard]] bool modified() const { return text_ != original_; }

    /// Valeur à `table.key` (nullptr si absente).
    [[nodiscard]] const Value* get(const std::vector<std::string>& table, const std::string& key) const;
    [[nodiscard]] bool has_table(const std::vector<std::string>& table) const;

    /// Crée ou remplace `key` dans `table` (créée si besoin).
    void set(const std::vector<std::string>& table, const std::string& key, const Value& v);
    /// Retire une clé ; renvoie faux si elle n'existait pas.
    bool remove(const std::vector<std::string>& table, const std::string& key);
    /// Ajoute l'en-tête [table] si absente (table vide).
    void ensure_table(const std::vector<std::string>& table);
    /// Retire [table] et toutes ses sous-tables [table.*].
    bool remove_table(const std::vector<std::string>& table);
    /// Renomme l'en-tête [old] (et [old.*]) en [new].
    bool rename_table(const std::vector<std::string>& old_path, const std::vector<std::string>& new_path);
    /// Remplace tout le texte (éditeur brut) ; valide la syntaxe.
    void replace_text(std::string text);

    /// Écrit le fichier (atomique). `p` vide = chemin d'origine.
    void save(const std::filesystem::path& p = {});

private:
    struct Stmt {
        enum Kind { Header, ArrayHeader, KeyValue } kind;
        std::vector<std::string> table;   ///< table courante (ou table de l'en-tête)
        std::vector<std::string> key;     ///< chemin de clé (KeyValue)
        size_t first = 0, last = 0;       ///< lignes [first, last]
        std::string comment;              ///< commentaire de fin (KeyValue mono-ligne)
        size_t comment_col = 0;           ///< colonne du commentaire (alignement)
        std::string indent;
    };

    std::filesystem::path path_;
    std::string text_, original_;
    Value data_;

    [[nodiscard]] std::vector<std::string> lines() const;
    [[nodiscard]] std::vector<Stmt> scan(const std::vector<std::string>& lines) const;
    void commit(const std::vector<std::string>& lines);
};

/// Chemin pointé « a.b.c » → {"a","b","c"} (sans gestion des guillemets).
std::vector<std::string> split_path(std::string_view dotted);
std::string join_path(const std::vector<std::string>& path);

} // namespace toml
