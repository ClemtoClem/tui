/* @file io/toml.hpp
 *
 * TOML 1.0 : modèle de valeurs, parseur et sérialiseur.
 *
 *  - Les tables conservent l'ordre d'insertion des clés (affichage et
 *    réécriture fidèles à l'ordre du fichier).
 *  - Les dates/heures TOML sont conservées comme chaînes brutes.
 *  - toml::Document (io/TomlDocument.hpp) permet de modifier un fichier
 *    existant sans perdre ses commentaires ni sa mise en forme.
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace toml {

/// Erreur de syntaxe ou de type, avec position (ligne/colonne à partir de 1).
class ParseError : public std::runtime_error {
public:
    ParseError(const std::string& msg, std::string file = {}, size_t line = 0, size_t column = 0);
    [[nodiscard]] const std::string& file() const { return file_; }
    [[nodiscard]] size_t line() const { return line_; }
    [[nodiscard]] size_t column() const { return column_; }
    [[nodiscard]] const std::string& message() const { return msg_; }

private:
    std::string msg_, file_;
    size_t line_, column_;
};

enum class Type { None, String, Integer, Float, Boolean, Array, Table };

std::string_view type_name(Type t);

class Value {
public:
    Value() = default;
    Value(const char* s) : type_(Type::String), str_(s) {}
    Value(std::string s) : type_(Type::String), str_(std::move(s)) {}
    Value(std::string_view s) : type_(Type::String), str_(s) {}
    Value(int v) : type_(Type::Integer), int_(v) {}
    Value(int64_t v) : type_(Type::Integer), int_(v) {}
    Value(double v) : type_(Type::Float), float_(v) {}
    Value(bool v) : type_(Type::Boolean), bool_(v) {}
    Value(const std::vector<std::string>& v);

    static Value table();
    static Value array();

    [[nodiscard]] Type type() const { return type_; }
    [[nodiscard]] bool is_none() const { return type_ == Type::None; }
    [[nodiscard]] bool is_string() const { return type_ == Type::String; }
    [[nodiscard]] bool is_integer() const { return type_ == Type::Integer; }
    [[nodiscard]] bool is_float() const { return type_ == Type::Float; }
    [[nodiscard]] bool is_bool() const { return type_ == Type::Boolean; }
    [[nodiscard]] bool is_array() const { return type_ == Type::Array; }
    [[nodiscard]] bool is_table() const { return type_ == Type::Table; }

    // --- Scalaires
    [[nodiscard]] const std::string& as_string() const { return str_; }
    [[nodiscard]] int64_t as_integer() const { return int_; }
    [[nodiscard]] double as_float() const { return type_ == Type::Integer ? static_cast<double>(int_) : float_; }
    [[nodiscard]] bool as_bool() const { return bool_; }

    // --- Tableaux
    [[nodiscard]] const std::vector<Value>& items() const { return arr_; }
    std::vector<Value>& items() { return arr_; }
    void push_back(Value v) { arr_.push_back(std::move(v)); }
    /// Tableau de chaînes ; vide (et `ok=false`) si un élément n'est pas une chaîne.
    [[nodiscard]] std::vector<std::string> as_strings(bool* ok = nullptr) const;

    // --- Tables (ordre d'insertion conservé)
    [[nodiscard]] size_t size() const { return type_ == Type::Table ? keys_.size() : arr_.size(); }
    [[nodiscard]] const std::vector<std::string>& keys() const { return keys_; }
    [[nodiscard]] const Value& value_at(size_t i) const { return vals_[i]; }
    Value& value_at(size_t i) { return vals_[i]; }
    [[nodiscard]] const Value* find(std::string_view key) const;
    Value* find(std::string_view key);
    /// Insère une valeur vide si la clé est absente.
    Value& operator[](std::string_view key);
    void set(std::string_view key, Value v);
    bool erase(std::string_view key);
    /// Accès par chemin (« build.modes.release »). nullptr si absent.
    [[nodiscard]] const Value* at_path(const std::vector<std::string>& path) const;

    /// Table déclarée en ligne ({ a = 1 }) ou via une en-tête [a] ?
    [[nodiscard]] bool is_inline() const { return inline_; }
    void set_inline(bool v) { inline_ = v; }
    /// Table créée implicitement par [a.b] (sans en-tête [a]).
    [[nodiscard]] bool is_implicit() const { return implicit_; }
    void set_implicit(bool v) { implicit_ = v; }
    /// Tableau déclaré par [[a]] (tableau de tables).
    [[nodiscard]] bool is_table_array() const { return table_array_; }
    void set_table_array(bool v) { table_array_ = v; }

    bool operator==(const Value& o) const;

private:
    Type type_ = Type::None;
    std::string str_;
    int64_t int_ = 0;
    double float_ = 0;
    bool bool_ = false;
    std::vector<Value> arr_;
    std::vector<std::string> keys_;
    std::vector<Value> vals_;
    bool inline_ = false;
    bool implicit_ = false;
    bool table_array_ = false;
};

/// Analyse un texte TOML ; `file` sert uniquement aux messages d'erreur.
Value parse(std::string_view text, const std::string& file = "<texte>");
Value parse_file(const std::filesystem::path& p);

/// Clé TOML : nue si possible, sinon entre guillemets.
std::string format_key(std::string_view key);
/// Valeur au format TOML sur une ligne (tables → tables en ligne).
std::string format_value(const Value& v);
/// Chaîne TOML entre guillemets, échappée.
std::string quote(std::string_view s);
/// Sérialise une table racine (en-têtes [section] pour les sous-tables).
std::string serialize(const Value& root);

/// Interprète une saisie utilisateur (CLI/TUI) selon le type attendu.
/// `[a, b]` ou `a,b` pour une liste ; `true/false/oui/non` pour un booléen.
std::optional<Value> value_from_text(std::string_view text, Type expected, std::string* error = nullptr);
/// Inverse de value_from_text : rendu éditable d'une valeur (listes « a, b »).
std::string value_to_text(const Value& v);

} // namespace toml
