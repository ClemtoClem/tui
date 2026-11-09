/* @file ui/Widgets.hpp
 *
 * Widgets propres à l'interface de cpp-maker, construits sur la
 * bibliothèque tui :
 *   NavTree      liste arborescente sélectionnable par identifiant ;
 *   ChoiceField  choix cyclique « ‹ valeur › » (sans popup) ;
 *   FormInput    tui::Input qui signale sa prise de focus.
 *
 * Les couleurs ne sont pas figées : chaque élément porte un ton sémantique
 * (Tone) résolu par la feuille de style du thème courant (propriétés
 * « accent-color », « info-color »… ; classes « -accent », « -error »…).
 */
#pragma once

#include <tui/Buffer.hpp>
#include <tui/core/Color.hpp>
#include <tui/widget/Widget.hpp>
#include <tui/widgets/Input.hpp>

#include <functional>
#include <string>
#include <vector>

namespace ui {

/// Ton sémantique d'un texte ; la couleur réelle vient du thème.
enum class Tone { Normal, Accent, Info, Muted, Success, Warning, Error };

/// Classe CSS du ton (« -accent »…), vide pour Normal.
[[nodiscard]] const char* tone_class(Tone t);
/// Remplace la classe de ton d'un widget.
void set_tone(tui::Widget& w, Tone t);
/// Couleur du ton selon le style de `w` (« <ton>-color »), avec repli sur
/// la palette historique si aucune feuille de style n'est chargée.
[[nodiscard]] tui::Color tone_color(const tui::Widget& w, Tone t);

struct NavItem {
    std::string id;
    std::string label;
    int depth = 0;
    Tone tone = Tone::Normal;
    std::string hint;   ///< texte discret à droite
};

class NavTree : public tui::Widget {
public:
    void set_items(std::vector<NavItem> items);
    [[nodiscard]] const std::vector<NavItem>& items() const { return items_; }
    /// Sélectionne l'élément `id` (ou le premier) sans déclencher on_select.
    void select_id(const std::string& id);
    [[nodiscard]] std::string selected_id() const;
    void set_on_select(std::function<void(const std::string&)> cb) { on_select_ = std::move(cb); }
    void set_on_activate(std::function<void(const std::string&)> cb) { on_activate_ = std::move(cb); }
    void set_accent(tui::Color c) { accent_ = c; }

    [[nodiscard]] tui::Size measure(const tui::Constraints& c) const override;
    void arrange(tui::Rect r) override;
    void paint(tui::Buffer& buffer) const override;
    [[nodiscard]] bool focusable() const override { return true; }
    void on_focus() override { focused_ = true; need_repaint(); }
    void on_blur() override { focused_ = false; need_repaint(); }
    bool on_key(const tui::KeyEvent& e) override;
    bool on_mouse(const tui::MouseEvent& e) override;

private:
    std::vector<NavItem> items_;
    size_t selected_ = 0;
    int scroll_ = 0;
    int hover_ = -1;   ///< ligne survolée (index d'élément), -1 sinon
    bool focused_ = false;
    tui::Color accent_ = tui::Color::Default();
    std::function<void(const std::string&)> on_select_, on_activate_;

    void move_to(long index);
    void ensure_selected_visible();
};

class ChoiceField : public tui::Widget {
public:
    /// `labels` vide : les valeurs servent de libellés.
    void set_choices(std::vector<std::string> values, std::vector<std::string> labels = {});
    void set_value(const std::string& v);
    [[nodiscard]] std::string value() const;
    void set_on_change(std::function<void(const std::string&)> cb) { on_change_ = std::move(cb); }
    void set_on_focus(std::function<void()> cb) { on_focus_cb_ = std::move(cb); }
    void set_accent(tui::Color c) { accent_ = c; }

    [[nodiscard]] tui::Size measure(const tui::Constraints& c) const override;
    void paint(tui::Buffer& buffer) const override;
    [[nodiscard]] bool focusable() const override { return true; }
    void on_focus() override;
    void on_blur() override { focused_ = false; need_repaint(); }
    bool on_key(const tui::KeyEvent& e) override;
    bool on_mouse(const tui::MouseEvent& e) override;

private:
    std::vector<std::string> values_, labels_;
    size_t index_ = 0;
    bool focused_ = false;
    tui::Color accent_ = tui::Color::Default();
    std::function<void(const std::string&)> on_change_;
    std::function<void()> on_focus_cb_;

    void step(int delta);
};

/// Texte multi-ligne : paragraphes séparés par « \n », chacun replié à la largeur.
class TextBlock : public tui::Widget {
public:
    explicit TextBlock(std::string text = {}) : text_(std::move(text)) {}
    void set_text(std::string t) { text_ = std::move(t); need_repaint(); }
    [[nodiscard]] const std::string& text() const { return text_; }
    [[nodiscard]] tui::Size measure(const tui::Constraints& c) const override;
    void paint(tui::Buffer& buffer) const override;

private:
    std::string text_;
};

class FormInput : public tui::Input {
public:
    void set_on_focus(std::function<void()> cb) { on_focus_cb_ = std::move(cb); }
    void on_focus() override {
        tui::Input::on_focus();
        if (on_focus_cb_) on_focus_cb_();
    }

private:
    std::function<void()> on_focus_cb_;
};

} // namespace ui
