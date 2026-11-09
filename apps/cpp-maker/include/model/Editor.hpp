/* @file model/Editor.hpp
 *
 * Modifications transactionnelles d'un projet : plusieurs fichiers TOML
 * édités (commentaires conservés), fichiers et dossiers créés, renommés ou
 * mis à la corbeille. commit() écrit tout, recharge et valide le projet ;
 * en cas d'erreur, tout est restauré à l'identique.
 *
 *   model::Transaction tx(ws.root);
 *   tx.doc(ws.project.file).set({"build"}, "jobs", toml::Value(4));
 *   tx.commit();   // lève util::Error (et restaure) si le projet devient invalide
 */
#pragma once

#include "io/TomlDocument.hpp"
#include "model/Workspace.hpp"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace model {

class Transaction {
public:
    explicit Transaction(fs::path root) : root_(std::move(root)) {}
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;
    ~Transaction();

    /// Document à modifier (chargé au premier accès ; vide s'il n'existe pas).
    toml::Document& doc(const fs::path& file);

    /// Nouveau fichier (erreur s'il existe déjà, sauf `overwrite`).
    void create_file(const fs::path& p, const std::string& content, bool overwrite = false);
    void create_dir(const fs::path& p);
    /// Renommage immédiat (annulé en cas d'échec du commit).
    void rename(const fs::path& from, const fs::path& to);
    /// Déplacement vers <racine>/.cpp-maker-trash/ (récupérable) ou suppression définitive.
    void remove(const fs::path& p, bool purge);

    /// Écrit les documents, recharge et valide. Renvoie le projet rechargé.
    Workspace commit(bool validate = true);
    void rollback();

    [[nodiscard]] std::vector<fs::path> touched_files() const;
    /// Chemin de corbeille du dernier remove() (vide si purge).
    [[nodiscard]] const fs::path& trashed() const { return trashed_; }

private:
    fs::path root_;
    std::map<fs::path, toml::Document> docs_;
    std::map<fs::path, std::optional<std::string>> originals_;   ///< nullopt : fichier absent
    std::vector<std::function<void()>> undo_;
    std::vector<fs::path> purge_later_;
    fs::path trashed_;
    bool done_ = false;
};

/// Liste de chaînes d'une clé (vide si absente).
std::vector<std::string> get_list(const toml::Document& d, const std::vector<std::string>& table, const std::string& key);

} // namespace model
