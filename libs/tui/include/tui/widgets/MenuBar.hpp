/**
 * @file MenuBar.hpp
 * @brief Barre de menus horizontale ; chaque entrée ouvre un MenuDialog.
 */

#pragma once

#include "../Buffer.hpp"
#include "../core/Text.hpp"
#include "../widget/Widget.hpp"
#include "MenuDialog.hpp"
#include "MenuItem.hpp"

#include <memory>
#include <string>
#include <vector>

namespace tui {

struct MenuBarEntry {
    std::string label;
    std::vector<MenuItem> items;
};

class MenuBar : public Widget {
public:
    void set_menus(std::vector<MenuBarEntry> menus) {
        menus_ = std::move(menus);
        dialogs_.clear();
        for (auto& m : menus_) {
            auto dialog = std::make_shared<MenuDialog>();
            dialog->set_parent(this);
            dialog->set_items(m.items);
            dialogs_.push_back(dialog);
        }
        active_index_ = 0;
        need_repaint();
    }

    [[nodiscard]] Size measure(const Constraints& c) const override { return c.clamp({c.max_width, 1}); }

    void arrange(Rect final_rect) override {
        bounds_ = final_rect;
        int x = final_rect.x;
        offsets_.clear();
        for (const auto& m : menus_) {
            offsets_.push_back(x);
            x += static_cast<int>(TextHelper::display_width(TextHelper::decode_utf8(m.label))) + 2;
        }
        for (size_t i = 0; i < dialogs_.size(); ++i) {
            dialogs_[i]->set_position({offsets_[i], final_rect.y + 1});
            dialogs_[i]->arrange(Rect{});
        }
    }

    /// Ne peint QUE la barre elle-même, jamais le popup ouvert : un
    /// conteneur peint ses enfants dans l'ordre d'ajout, donc un frère
    /// ajouté après MenuBar (ex: Tabs, sous la barre) écraserait le
    /// popup si celui-ci était peint ici. Voir paint_overlay().
    void paint(Buffer& buffer) const override {
        int x = bounds_.x;
        for (size_t i = 0; i < menus_.size(); ++i) {
            auto decoded = TextHelper::decode_utf8(" " + menus_[i].label + " ");
            TextStyle style = (focused_ && i == active_index_) ? TextStyle::Reverse : TextStyle::None;
            int cx = x;
            for (char32_t ch : decoded) {
                buffer.set(cx, bounds_.y, Cell{ch, 1, Color::Default(), Color::Default(), style});
                ++cx;
            }
            x = cx;
        }
    }

    /// Peint uniquement le popup actuellement ouvert, s'il y en a un.
    /// À appeler explicitement APRÈS tout le reste de l'arbre (ex: via
    /// un widget compagnon ajouté en dernier dans un Stack racine),
    /// pour que le popup ne soit jamais recouvert par un frère peint
    /// après la barre de menu elle-même.
    void paint_overlay(Buffer& buffer) const {
        for (const auto& d : dialogs_) {
            if (d->is_open()) d->paint(buffer);
        }
    }

    [[nodiscard]] bool has_open_overlay() const {
        for (const auto& d : dialogs_) {
            if (d->is_open()) return true;
        }
        return false;
    }

    [[nodiscard]] bool focusable() const override { return !menus_.empty(); }
    void on_focus() override { focused_ = true; need_repaint(); }
    void on_blur() override {
        focused_ = false;
        if (auto* dlg = active_dialog()) dlg->close();
        need_repaint();
    }

    bool on_key(const KeyEvent& e) override {
        if (menus_.empty()) return false;

        if (auto* dlg = active_dialog(); dlg && dlg->is_open()) {
            if (e.key == Key::Left) { dlg->close(); move_active(-1); open_active(); return true; }
            if (e.key == Key::Right) { dlg->close(); move_active(1); open_active(); return true; }
            return dlg->on_key(e);
        }

        if (e.key == Key::Left) { move_active(-1); return true; }
        if (e.key == Key::Right) { move_active(1); return true; }
        if (e.key == Key::Enter || e.key == Key::Down) { open_active(); return true; }
        return false;
    }

    [[nodiscard]] std::vector<std::shared_ptr<Widget>> children() const override {
        std::vector<std::shared_ptr<Widget>> result;
        for (const auto& d : dialogs_) result.push_back(d);
        return result;
    }

    /// Index du menu dont le titre couvre la colonne x (-1 sinon).
    [[nodiscard]] int menu_at(int x) const {
        for (size_t i = 0; i < menus_.size() && i < offsets_.size(); ++i) {
            int w = TextHelper::display_width(TextHelper::decode_utf8(menus_[i].label)) + 2;
            if (x >= offsets_[i] && x < offsets_[i] + w) return static_cast<int>(i);
        }
        return -1;
    }

    /// Clic sur un titre : ouvre / ferme son menu ; avec un menu ouvert, le
    /// survol d'un autre titre bascule dessus ; clic dans le menu : voir MenuDialog.
    bool on_mouse(const MouseEvent& e) override {
        if (menus_.empty()) return false;
        MenuDialog* open = active_dialog();
        bool any_open = open && open->is_open();
        if (any_open && open->bounds().contains({e.x, e.y})) return open->on_mouse(e);
        if (e.y != bounds_.y) {
            if (any_open && e.action == MouseEvent::Action::Press) {
                open->close();
                need_repaint();
                return true;
            }
            return false;
        }
        int i = menu_at(e.x);
        if (e.action == MouseEvent::Action::Move) {
            if (any_open && i >= 0 && static_cast<size_t>(i) != active_index_) {
                open->close();
                active_index_ = static_cast<size_t>(i);
                open_active();
                return true;
            }
            return false;
        }
        if (e.button != MouseEvent::Button::Left || e.action != MouseEvent::Action::Press) return i >= 0;
        if (i < 0) {
            if (any_open) open->close();
            need_repaint();
            return true;
        }
        if (any_open && static_cast<size_t>(i) == active_index_) {
            open->close();
        } else {
            if (any_open) open->close();
            active_index_ = static_cast<size_t>(i);
            open_active();
        }
        need_repaint();
        return true;
    }

    /// Le menu ouvert déborde de la barre : il capte la souris en priorité.
    [[nodiscard]] bool overlay_contains(Point p) const override {
        for (const auto& d : dialogs_)
            if (d->is_open() && d->bounds().contains(p)) return true;
        return false;
    }

private:
    [[nodiscard]] MenuDialog* active_dialog() {
        return dialogs_.empty() ? nullptr : dialogs_[active_index_].get();
    }

    void move_active(int delta) {
        if (menus_.empty()) return;
        long count = static_cast<long>(menus_.size());
        long next = ((static_cast<long>(active_index_) + delta) % count + count) % count;
        active_index_ = static_cast<size_t>(next);
        need_repaint();
    }

    void open_active() {
        if (auto* d = active_dialog()) d->open();
        need_repaint();
    }

    std::vector<MenuBarEntry> menus_;
    std::vector<std::shared_ptr<MenuDialog>> dialogs_;
    std::vector<int> offsets_;
    size_t active_index_ = 0;
    bool focused_ = false;
};

/// Widget compagnon minimal : ne peint que le popup actuellement ouvert
/// d'un MenuBar donné. À ajouter en DERNIER parmi les enfants d'un
/// Stack racine (après le reste de l'interface), pour garantir que le
/// popup d'un menu placé en haut de l'écran reste visible par-dessus
/// tout ce qui est peint après la barre elle-même (ex: Tabs juste en
/// dessous). Ne participe ni au focus ni aux événements : MenuBar lui-
/// même les gère déjà en entier.
class MenuBarOverlay : public Widget {
public:
    explicit MenuBarOverlay(std::shared_ptr<MenuBar> menu_bar) : menu_bar_(std::move(menu_bar)) {}

    [[nodiscard]] Size measure(const Constraints& c) const override { return c.clamp({0, 0}); }
    void paint(Buffer& buffer) const override {
        if (menu_bar_) menu_bar_->paint_overlay(buffer);
    }

private:
    std::shared_ptr<MenuBar> menu_bar_;
};

} // namespace tui
