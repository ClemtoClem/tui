/* @file ui/Form.hpp
 *
 * Formulaire d'édition d'un fichier TOML généré depuis le schéma : une
 * ligne par clé (libellé + champ), regroupées par section. Les champs vides
 * correspondent aux clés absentes (valeur par défaut, affichée en grisé).
 */
#pragma once

#include "io/TomlDocument.hpp"
#include "model/Schema.hpp"
#include "ui/Widgets.hpp"

#include <tui/widget/layout/Vertical.hpp>

#include <functional>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace ui {

struct FormSection {
    std::vector<std::string> table;   ///< section concrète ({"build","modes","fast"})
    std::string pattern;              ///< motif du schéma (« build.modes.* »)
    std::string title;                ///< titre affiché (défaut : titre du schéma)
};

class Form {
public:
    Form(schema::FileKind kind, std::vector<FormSection> sections, std::set<std::string> hidden = {});

    [[nodiscard]] std::shared_ptr<tui::Widget> widget() const { return root_; }
    [[nodiscard]] std::shared_ptr<tui::Widget> first_field() const;

    /// Remplit les champs avec les valeurs du document.
    void load(const toml::Document& doc);
    [[nodiscard]] bool dirty() const;
    /// Applique les champs modifiés au document (lève util::Error si une saisie est invalide).
    void apply(toml::Document& doc) const;

    void set_on_help(std::function<void(const std::string&)> cb) { on_help_ = std::move(cb); }
    void set_on_change(std::function<void()> cb) { on_change_ = std::move(cb); }
    void set_accent(tui::Color c);

private:
    struct Field {
        const schema::Field* spec = nullptr;
        std::vector<std::string> table;
        std::string original;
        std::shared_ptr<FormInput> input;
        std::shared_ptr<ChoiceField> choice;
        [[nodiscard]] std::string text() const;
    };

    schema::FileKind kind_;
    std::shared_ptr<tui::Vertical> root_;
    std::vector<Field> fields_;
    std::function<void(const std::string&)> on_help_;
    std::function<void()> on_change_;

    void help_for(const Field& f) const;
};

/// Rendu texte d'une valeur pour un champ (listes « a, b », tables « K=v, K2=v2 »).
std::string field_text(const schema::Field& f, const toml::Value* v);
/// Saisie texte → valeur ; std::nullopt si vide (clé à retirer).
std::optional<toml::Value> field_value(const schema::Field& f, const std::string& text);

} // namespace ui
