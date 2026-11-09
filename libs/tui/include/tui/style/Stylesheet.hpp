/**
 * @file style/Stylesheet.hpp
 * @brief Feuilles de style à la Textual (TCSS) : sélecteurs, variables,
 * cascade par spécificité.
 *
 * Syntaxe reconnue :
 *
 *     $accent2: #ff8800;                 variable (surcharge celles du thème)
 *     Button { background: $surface; }   type (nom de classe C++, bases incluses)
 *     #save, .-primary, *                id, classe, universel
 *     Vertical > Button.big:focus        enfant direct, classes et pseudo-classes
 *     Screen Label                       descendant
 *     Button { &:hover { … } .icon { … } }  imbrication (« & » = sélecteur parent)
 *     /​* commentaires *​/  et  // commentaires de fin de ligne
 *
 * Pseudo-classes : :focus :hover :disabled :enabled :focus-within :root
 * :dark :light :first-child :last-child.
 *
 * Propriétés interprétées : color, background, text-style, border (et
 * border-color), border-title-color, border-title-style, padding, width,
 * height, min-width, max-width, min-height, max-height, display. Toute
 * autre propriété est conservée (variables résolues) dans
 * ComputedStyle::extra pour les widgets qui la lisent (text-align…).
 *
 * Couleurs : #rgb, #rrggbb, rgb(r,g,b), noms, $variable, « auto »
 * (texte contrasté), suffixe « N% » (opacité mélangée au fond du thème).
 */

#pragma once

#include "Style.hpp"

#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tui {

class Widget;

namespace css {

struct ParseError {
    std::string origin;
    int line = 0, column = 0;
    std::string message;
    [[nodiscard]] std::string to_string() const;
};

struct SimpleSelector {
    std::string type;                 ///< vide = pas de contrainte de type
    std::string id;
    std::vector<std::string> classes;
    std::vector<std::string> pseudos;
    bool universal = false;
};

struct SelectorStep {
    SimpleSelector simple;
    char combinator = ' ';   ///< lien avec l'étape PRÉCÉDENTE : ' ' descendant, '>' enfant
};

struct Selector {
    std::vector<SelectorStep> steps;   ///< de gauche à droite
    int ids = 0, classes = 0, types = 0;
    [[nodiscard]] std::string to_string() const;
};

struct Declaration {
    std::string property;
    std::string value;
    int line = 0;
};

struct Rule {
    std::vector<Selector> selectors;
    std::vector<Declaration> declarations;
    int tier = 0;       ///< 0 = feuille par défaut, 1 = feuille de l'application
    size_t order = 0;   ///< ordre d'apparition (départage à spécificité égale)
    std::string origin;
    ComputedStyle compiled;
};

/// État dont dépend la correspondance des pseudo-classes.
struct MatchContext {
    const Widget* focused = nullptr;
    bool dark = true;
};

/// Enregistre un nom de type CSS correspondant à toute instance de `T`
/// (classes dérivées incluses), comme les sélecteurs de type de Textual.
void register_type(std::string name, std::function<bool(const Widget&)> test);
template <class T> void register_type(std::string name) {
    register_type(std::move(name), [](const Widget& w) { return dynamic_cast<const T*>(&w) != nullptr; });
}
/// Vrai si le widget correspond au type CSS `name` (nom de sa classe C++
/// sans espace de noms, ou nom enregistré d'une de ses bases).
[[nodiscard]] bool matches_type(const Widget& w, std::string_view name);

class Stylesheet {
public:
    /// Ajoute une source TCSS. Les erreurs sont accumulées (errors()) ; les
    /// règles valides sont conservées. Renvoie false s'il y a eu des erreurs.
    bool add_source(std::string_view text, std::string origin = "<css>", int tier = 1);
    /// Ajoute le contenu d'un fichier (erreur rapportée si illisible).
    bool add_file(const std::filesystem::path& path, int tier = 1);
    void clear();
    /// Retire les règles d'une origine donnée (rechargement d'un fichier).
    void remove_origin(const std::string& origin);

    /// Variables du thème ; celles déclarées dans les feuilles ($nom: …) les surchargent.
    void set_theme_variables(std::map<std::string, std::string> vars);
    [[nodiscard]] std::map<std::string, std::string> variables() const;
    /// Résout les $variables d'une valeur.
    [[nodiscard]] std::string resolve(std::string_view value) const;

    [[nodiscard]] const std::vector<ParseError>& errors() const { return errors_; }
    [[nodiscard]] const std::vector<Rule>& rules() const { return rules_; }
    [[nodiscard]] bool empty() const { return rules_.empty(); }

    /// Style calculé de `w` (règles + style en ligne du widget).
    [[nodiscard]] ComputedStyle compute(const Widget& w, const MatchContext& ctx) const;
    /// Calcule et affecte le style de tout le sous-arbre de `root`.
    void apply(Widget& root, const MatchContext& ctx) const;

    /// Compile une liste de déclarations (« color: red; padding: 1 2 »).
    [[nodiscard]] ComputedStyle compile_inline(std::string_view declarations,
                                               std::vector<ParseError>* errors = nullptr) const;

    [[nodiscard]] static bool matches(const Selector& s, const Widget& w, const MatchContext& ctx);
    /// Analyse un sélecteur seul (« Vertical > Button.-primary:hover »).
    [[nodiscard]] static std::optional<Selector> parse_selector(std::string_view text, std::string* error = nullptr);

    /// Incrémenté à chaque modification (règles ou variables).
    [[nodiscard]] unsigned generation() const { return generation_; }

private:
    std::vector<Rule> rules_;
    std::vector<ParseError> errors_;         ///< parse_errors_ + value_errors_
    std::vector<ParseError> parse_errors_, value_errors_;
    mutable std::map<std::string, ComputedStyle> inline_cache_;
    std::map<std::string, std::string> theme_vars_;
    std::map<std::string, std::string> css_vars_;   ///< nom → valeur brute
    std::vector<std::pair<std::string, std::string>> var_origins_;   ///< (origine, nom)
    size_t next_order_ = 0;
    unsigned generation_ = 0;

    void recompile();
    void compile_rule(Rule& r);
    void compile_declaration(ComputedStyle& out, const Declaration& d, const std::string& origin,
                             std::vector<ParseError>* errors) const;
};

/// Feuille de style par défaut, à l'apparence de Textual (à charger avec
/// tier 0 : l'application la surcharge).
[[nodiscard]] std::string_view default_stylesheet();

} // namespace css
} // namespace tui
