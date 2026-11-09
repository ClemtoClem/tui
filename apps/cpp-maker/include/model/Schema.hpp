/* @file model/Schema.hpp
 *
 * Schéma des fichiers de configuration : chaque clé connue, son type, ses
 * valeurs possibles et sa description. Une seule source de vérité utilisée
 * par :
 *   - la validation (clés inconnues, types, valeurs hors liste) ;
 *   - `cpp-maker config keys|get|set` (conversion et contrôle des saisies) ;
 *   - les formulaires de l'interface TUI.
 *
 * Les familles de tables (build.modes.<nom>, toolchains.<nom>…) utilisent
 * « * » comme joker dans le chemin de section.
 */
#pragma once

#include "io/toml.hpp"

#include <string>
#include <vector>

namespace schema {

enum class FileKind { Project, Layer, Module, Application };

std::string_view file_name(FileKind k);
std::string_view kind_label(FileKind k);

enum class FieldType { String, Integer, Boolean, StringList, Enum, Map };

struct Field {
    std::string section;   ///< « build », « build.modes.* »…
    std::string key;
    FieldType type = FieldType::String;
    std::string label;     ///< libellé court (formulaires)
    std::string help;      ///< description
    std::vector<std::string> choices;   ///< Enum (et suggestions pour String)
    std::string placeholder;            ///< valeur par défaut affichée
};

struct Section {
    std::string path;   ///< « project », « build.modes.* »
    std::string title;
    std::string help;
    [[nodiscard]] bool is_family() const { return path.find('*') != std::string::npos; }
};

const std::vector<Section>& sections(FileKind k);
std::vector<const Field*> fields(FileKind k, const std::string& section_pattern);

/// Champ décrivant `section.key` (section concrète, ex. « build.modes.fast »).
const Field* find(FileKind k, const std::vector<std::string>& section, const std::string& key);
/// Motif de section correspondant à une section concrète (ou nullptr).
const Section* find_section(FileKind k, const std::vector<std::string>& section);

/// Type TOML attendu pour un champ.
toml::Type toml_type(const Field& f);
/// Contrôle une valeur : message d'erreur, vide si conforme.
std::string check(const Field& f, const toml::Value& v);
/// Convertit une saisie texte en valeur conforme au champ.
toml::Value parse_input(const Field& f, const std::string& text);

/// Avertissements : clés/sections inconnues, types incorrects.
std::vector<std::string> lint(FileKind k, const toml::Value& root, const std::string& file);

/// Découpe « section.cle » en (section, clé) à l'aide du schéma : la plus
/// longue section connue l'emporte (« build.modes.fast.flags »).
bool split_key(FileKind k, const std::string& dotted, std::vector<std::string>& section, std::string& key);

} // namespace schema
