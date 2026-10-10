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

#include <functional>
#include <memory>
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
    [[nodiscard]] virtual bool visible() const { return visible_; }
    void set_visible(bool visible) { visible_ = visible; }

    /// Liste des enfants directs, dans l'ordre de traversée logique.
    /// Vide pour un widget feuille ; surchargé par Container.
    [[nodiscard]] virtual std::vector<std::shared_ptr<Widget>> children() const { return {}; }

protected:
    Rect bounds_;
    Widget* parent_ = nullptr;
    bool visible_ = true;
    std::function<void()> on_repaint_;
};

} // namespace tui
