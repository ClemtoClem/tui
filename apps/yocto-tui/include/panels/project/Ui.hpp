/* apps/yocto-tui/panels/project/Ui.hpp */
#pragma once

#include "../../Theme.hpp"

#include <tui/widget/layout/Border.hpp>
#include <tui/widget/layout/Horizontal.hpp>
#include <tui/widget/layout/ScrollableContainer.hpp>
#include <tui/widget/layout/Vertical.hpp>
#include <tui/widgets/Button.hpp>
#include <tui/widgets/Checkbox.hpp>
#include <tui/widgets/Input.hpp>
#include <tui/widgets/Label.hpp>
#include <tui/widgets/ListView.hpp>
#include <tui/widgets/RadioSet.hpp>
#include <tui/widgets/TextArea.hpp>

#include <functional>
#include <initializer_list>
#include <memory>
#include <string>

/// Petites fabriques partagées par les éditeurs du projet : elles
/// gardent un style homogène (bordures arrondies, libellés atténués)
/// sans répéter dix lignes de configuration par widget.
namespace yocto::ui {

inline std::shared_ptr<tui::Label> muted(const std::string& text, bool wrap = false) {
    auto l = std::make_shared<tui::Label>(text);
    l->set_foreground(kMuted);
    l->set_wrap(wrap);
    return l;
}

inline std::shared_ptr<tui::Border> box(const std::string& title, std::shared_ptr<tui::Widget> child) {
    auto b = std::make_shared<tui::Border>();
    b->set_border_style(tui::BorderStyle::Rounded);
    b->set_color(kBorder);
    b->set_title(title);
    b->set_child(std::move(child));
    return b;
}

inline std::shared_ptr<tui::Button> button(const std::string& label, std::function<void()> on_click) {
    auto b = std::make_shared<tui::Button>(label);
    b->set_on_click(std::move(on_click));
    return b;
}

/// Rangée de widgets à leur taille naturelle (boutons).
inline std::shared_ptr<tui::Horizontal> row(std::initializer_list<std::shared_ptr<tui::Widget>> widgets) {
    auto r = std::make_shared<tui::Horizontal>();
    for (const auto& w : widgets) {
        r->add_child(w, tui::LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    }
    return r;
}

/// Libellé sur une ligne, champ en dessous.
inline void field(tui::Vertical& form, const std::string& label, std::shared_ptr<tui::Widget> w, int height = 1) {
    form.add_child(muted(label), tui::LayoutParams::fixed(1));
    form.add_child(std::move(w), tui::LayoutParams::fixed(height));
}

inline std::shared_ptr<tui::Input> input(const std::string& placeholder = "") {
    auto i = std::make_shared<tui::Input>();
    i->set_placeholder(placeholder);
    return i;
}

inline std::shared_ptr<tui::TextArea> editor() {
    auto t = std::make_shared<tui::TextArea>();
    t->set_show_scrollbar(true);
    t->set_accent_color(kAccent);
    return t;
}

inline std::shared_ptr<tui::TextArea> viewer() {
    auto t = editor();
    t->set_read_only(true);
    return t;
}

inline std::shared_ptr<tui::ListView> list() {
    auto l = std::make_shared<tui::ListView>();
    l->set_colors(kAccent, kFg, kMuted);
    l->set_show_scrollbar(true);
    return l;
}

/// Formulaire vertical défilant (pour les formulaires plus hauts que
/// l'écran) : renvoie le conteneur à insérer et le Vertical à remplir.
inline std::pair<std::shared_ptr<tui::Widget>, std::shared_ptr<tui::Vertical>> scroll_form() {
    auto form = std::make_shared<tui::Vertical>();
    auto scroll = std::make_shared<tui::ScrollableVertical>();
    scroll->set_child(form);
    return {scroll, form};
}

/// Garde RAII contre la ré-entrance des callbacks pendant le chargement
/// d'un élément dans un formulaire (set_checked, set_selected_index
/// déclenchent on_change).
class LoadingGuard {
public:
    explicit LoadingGuard(bool& flag) : flag_(flag), previous_(flag) { flag_ = true; }
    ~LoadingGuard() { flag_ = previous_; }
    LoadingGuard(const LoadingGuard&) = delete;
    LoadingGuard& operator=(const LoadingGuard&) = delete;

private:
    bool& flag_;
    bool previous_;
};

} // namespace yocto::ui
