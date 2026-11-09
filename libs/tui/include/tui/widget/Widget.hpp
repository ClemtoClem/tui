/**
 * @file Widget.hpp
 * @brief Classe de base de l'arbre de widgets retained-mode.
 *
 * Note d'ordonnancement : ce fichier appartient conceptuellement à la
 * sous-phase 1.4 (plan Phase 1, "Widget/Container base, layout model")
 * mais la classe de base seule est introduite ici, en sous-phase 1.3,
 * car App::run() a besoin d'un type Widget concret pour orchestrer
 * measure/arrange/paint - même genre de dépendance en avance que
 * celle déjà documentée pour Dropdown/ListView. Container, Layout,
 * FocusManager et les conteneurs de mise en page restent en 1.4.
 */

#pragma once

#include "../core/Event.hpp"
#include "../core/Geometry.hpp"
#include "../style/Style.hpp"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace tui {

class Buffer;

class Widget : public std::enable_shared_from_this<Widget> {
public:
    virtual ~Widget() = default;

    /// Calcule la taille désirée sous les contraintes données. Pur ;
    /// ne doit avoir aucun effet de bord observable hors du widget lui-même.
    [[nodiscard]] virtual Size measure(const Constraints& constraints) const = 0;

    /// Assigne le rectangle final (dans les coordonnées du parent).
    /// L'implémentation par défaut se contente de le stocker ; les
    /// Container (sous-phase 1.4) la surchargent pour aussi arranger
    /// leurs enfants.
    virtual void arrange(Rect final_rect) { bounds_ = final_rect; }

    /// Peint le widget dans `buffer`, aux coordonnées de bounds().
    virtual void paint(Buffer& buffer) const = 0;

    [[nodiscard]] Rect bounds() const { return bounds_; }

    [[nodiscard]] Widget* parent() const { return parent_; }
    void set_parent(Widget* parent) { parent_ = parent; }

    [[nodiscard]] virtual bool focusable() const { return false; }
    virtual void on_focus() {}
    virtual void on_blur() {}

    /// Retourne true si l'événement a été consommé (arrête la remontée).
    [[nodiscard]] virtual bool on_key(const KeyEvent&) { return false; }
    [[nodiscard]] virtual bool on_mouse(const MouseEvent&) { return false; }

    /// Demande à la chaîne d'ancêtres de rendre ce widget visible. À
    /// appeler typiquement par FocusManager après un changement de
    /// focus : chaque ancêtre capable de défiler ajuste son offset
    /// pour que ce widget entre dans sa zone visible.
    void ensure_visible() {
        if (parent_) parent_->make_descendant_visible(this);
    }

    /// Appelé par un descendant (via ensure_visible()) pour demander à
    /// ce widget de rendre `target` visible. L'implémentation par défaut
    /// se contente de propager au parent (un widget non-défilable n'a
    /// rien à faire, mais doit laisser la demande remonter).
    ///
    /// Les conteneurs défilables (LinearLayout, ScrollableContainer)
    /// surchargent pour ajuster leur propre offset : les bornes de
    /// `target` sont exprimées dans le même repère que `bounds_`
    /// (décalage de défilement éventuel déjà inclus), donc la
    /// comparaison est directe.
    virtual void make_descendant_visible(const Widget* target) {
        if (parent_) parent_->make_descendant_visible(target);
    }

    /// Signale qu'un repaint est nécessaire ; remonte jusqu'à la racine
    /// (celle dont parent() == nullptr), qui déclenche le callback
    /// enregistré par App::set_root() pour marquer la frame sale.
    void need_repaint() {
        if (parent_) { parent_->need_repaint(); return; }
        if (on_repaint_) on_repaint_();
    }

    /// Réservé à App : n'a d'effet que sur le widget racine (sans parent).
    void set_repaint_callback(std::function<void()> cb) { on_repaint_ = std::move(cb); }

    /// Virtuelle pour que des widgets comme Dialog puissent lier leur
    /// visibilité à un état interne (ex: showing_) plutôt qu'au seul
    /// drapeau visible_ - important pour que FocusManager (qui appelle
    /// visible() via un Widget* de base) ne traverse jamais le contenu
    /// d'une modale fermée.
    [[nodiscard]] virtual bool visible() const { return visible_ && css_style_.display.value_or(true); }
    void set_visible(bool visible) { visible_ = visible; }

    /// Liste des enfants directs, dans l'ordre de traversée logique.
    /// Vide pour un widget feuille ; surchargé par Container.
    [[nodiscard]] virtual std::vector<std::shared_ptr<Widget>> children() const { return {}; }

    // ------------------------------------------------------------------
    // Identité et états CSS (voir style/Stylesheet.hpp)
    // ------------------------------------------------------------------

    /// Nom de type CSS : par défaut le nom de la classe C++ dynamique, sans
    /// espace de noms (« Button », « NavTree »…).
    [[nodiscard]] virtual std::string type_name() const;

    [[nodiscard]] const std::string& id() const { return css_id_; }
    void set_id(std::string id) { css_id_ = std::move(id); need_repaint(); }

    [[nodiscard]] const std::vector<std::string>& classes() const { return css_classes_; }
    [[nodiscard]] bool has_class(std::string_view c) const {
        return std::find(css_classes_.begin(), css_classes_.end(), c) != css_classes_.end();
    }
    /// Ajoute une ou plusieurs classes séparées par des espaces.
    void add_class(std::string_view names);
    void remove_class(std::string_view names);
    void toggle_class(std::string_view name) {
        if (has_class(name)) remove_class(name);
        else add_class(name);
    }
    void set_class(std::string_view name, bool on) {
        if (on) add_class(name);
        else remove_class(name);
    }

    /// Style en ligne (« color: red; padding: 1 »), prioritaire sur les feuilles.
    void set_inline_style(std::string declarations) { css_inline_ = std::move(declarations); need_repaint(); }
    [[nodiscard]] const std::string& inline_style() const { return css_inline_; }

    /// Titre / sous-titre affichés dans la bordure CSS (« border-title »).
    void set_border_title(std::string t) { border_title_ = std::move(t); need_repaint(); }
    [[nodiscard]] const std::string& border_title() const { return border_title_; }
    void set_border_subtitle(std::string t) { border_subtitle_ = std::move(t); need_repaint(); }
    [[nodiscard]] const std::string& border_subtitle() const { return border_subtitle_; }

    /// Désactivé : ni focus, ni souris ; « :disabled » en CSS. Hérité des ancêtres.
    void set_disabled(bool d) { css_disabled_ = d; need_repaint(); }
    [[nodiscard]] bool disabled() const { return css_disabled_ || (parent_ && parent_->disabled()); }

    /// Tenus à jour par FocusManager et App (pseudo-classes :focus et :hover).
    [[nodiscard]] bool has_focus() const { return css_focus_; }
    void set_has_focus(bool f) { css_focus_ = f; }
    [[nodiscard]] bool hovered() const { return css_hovered_; }
    void set_hovered(bool h) { css_hovered_ = h; }

    /// Style calculé (vide sans feuille de style : rendu historique inchangé).
    [[nodiscard]] const ComputedStyle& style() const { return css_style_; }
    void set_computed_style(ComputedStyle s) { css_style_ = std::move(s); }
    [[nodiscard]] bool styled() const { return !css_style_.empty(); }

    /// Rectangle bordure + marge intérieure comprises (= bounds() sans style).
    [[nodiscard]] Rect outer_bounds() const {
        Spacing in = css_style_.insets();
        return {bounds_.x - in.left, bounds_.y - in.top, bounds_.width + in.left + in.right,
                bounds_.height + in.top + in.bottom};
    }

    /// Variantes « boîte CSS » de measure/arrange/paint, à utiliser par les
    /// conteneurs pour leurs enfants : elles ajoutent bordure, padding,
    /// dimensions CSS, fond et couleurs héritées. Sans style, elles
    /// équivalent exactement à measure/arrange/paint.
    [[nodiscard]] Size measure_outer(const Constraints& constraints) const;
    void layout(Rect outer);
    void render(Buffer& buffer) const;

    /// Rendu de la ligne courante des listes (ListView, TreeView…) : selon le
    /// CSS (« cursor-background », « cursor-color », « cursor-text-style »,
    /// « cursor-blurred-background ») ou, sans style, vidéo inverse si focus.
    struct RowCursor {
        bool active = false;   ///< la ligne sélectionnée est-elle mise en évidence ?
        Color fg = Color::Default(), bg = Color::Default();
        TextStyle style = TextStyle::None;
        bool full_row = false; ///< peindre toute la largeur
    };
    [[nodiscard]] RowCursor row_cursor(bool focused) const;

    /// Vrai si (x, y) tombe dans une zone que le widget peint HORS de ses
    /// bornes (menu déroulant ouvert, popup…) : App lui adresse alors la
    /// souris en priorité, quel que soit l'endroit où il est dans l'arbre.
    [[nodiscard]] virtual bool overlay_contains(Point) const { return false; }

    /// Premier ancêtre (ou soi-même) focusable et activé ; nullptr sinon.
    [[nodiscard]] Widget* focusable_ancestor() {
        for (Widget* w = this; w; w = w->parent_)
            if (w->focusable() && !w->disabled() && w->visible()) return w;
        return nullptr;
    }
    /// Vrai si `this` est `other` ou l'un de ses descendants.
    [[nodiscard]] bool is_descendant_of(const Widget* other) const {
        for (const Widget* w = this; w; w = w->parent_)
            if (w == other) return true;
        return false;
    }

protected:
    Rect bounds_;
    Widget* parent_ = nullptr;
    bool visible_ = true;
    std::function<void()> on_repaint_;

    std::string css_id_;
    std::vector<std::string> css_classes_;
    std::string css_inline_;
    std::string border_title_, border_subtitle_;
    ComputedStyle css_style_;
    bool css_disabled_ = false;
    bool css_focus_ = false;
    bool css_hovered_ = false;
    mutable std::string type_name_cache_;
};

} // namespace tui
