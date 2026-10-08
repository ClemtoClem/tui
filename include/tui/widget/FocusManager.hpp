/**
 * @file FocusManager.hpp
 * @brief Registre de focus sans état persistant : parcourt l'arbre en direct à chaque appel.
 *
 * Volontairement stateless au-delà du widget actuellement focusé :
 * focus_next()/focus_prev() reconstruisent l'ordre de traversée à
 * chaque appel plutôt que de maintenir une liste enregistrée à part,
 * pour ne jamais désynchroniser quand des widgets sont ajoutés/retirés
 * dynamiquement (ex: ouverture d'un Dialog en sous-phase 1.8).
 */

#pragma once

#include "Widget.hpp"

#include <algorithm>
#include <memory>
#include <vector>

namespace tui {

class FocusManager {
public:
    void set_focus(std::shared_ptr<Widget> widget) {
        auto current = current_.lock();
        if (current == widget) return;
        if (current) current->on_blur();
        current_ = widget;
        if (widget) {
            widget->on_focus();
            widget->ensure_visible();
        }
    }

    [[nodiscard]] std::shared_ptr<Widget> current() const { return current_.lock(); }

    void focus_next(const std::shared_ptr<Widget>& root) { move(effective_root(root), true); }
    void focus_prev(const std::shared_ptr<Widget>& root) { move(effective_root(root), false); }

    /// Restreint focus_next()/focus_prev() au sous-arbre de `scope_root`
    /// jusqu'au pop_scope() correspondant, empêchant le focus de
    /// "s'échapper" vers le fond pendant qu'un widget modal (Dialog) est
    /// ouvert. Empilable : une modale peut en ouvrir une autre.
    void push_scope(std::shared_ptr<Widget> scope_root) { scope_stack_.push_back(std::move(scope_root)); }
    void pop_scope() { if (!scope_stack_.empty()) scope_stack_.pop_back(); }
    [[nodiscard]] std::shared_ptr<Widget> current_scope() const {
        return scope_stack_.empty() ? nullptr : scope_stack_.back();
    }

private:
    [[nodiscard]] const std::shared_ptr<Widget>& effective_root(const std::shared_ptr<Widget>& app_root) const {
        return scope_stack_.empty() ? app_root : scope_stack_.back();
    }

    static void collect_focusable(const std::shared_ptr<Widget>& w, std::vector<std::shared_ptr<Widget>>& out) {
        if (!w || !w->visible()) return;
        if (w->focusable()) out.push_back(w);
        for (const auto& child : w->children()) collect_focusable(child, out);
    }

    void move(const std::shared_ptr<Widget>& root, bool forward) {
        std::vector<std::shared_ptr<Widget>> order;
        collect_focusable(root, order);
        if (order.empty()) return;

        auto current = current_.lock();
        auto it = current ? std::find(order.begin(), order.end(), current) : order.end();

        std::shared_ptr<Widget> next;
        if (forward) {
            next = (it == order.end() || std::next(it) == order.end()) ? order.front() : *std::next(it);
        } else {
            next = (it == order.end() || it == order.begin()) ? order.back() : *std::prev(it);
        }
        set_focus(next);
    }

    std::weak_ptr<Widget> current_;
    std::vector<std::shared_ptr<Widget>> scope_stack_;
};

} // namespace tui
