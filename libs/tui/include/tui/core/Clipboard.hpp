/**
 * @file Clipboard.hpp
 * @brief Presse-papiers partagé par les widgets d'édition (TextArea, Input…).
 *
 * Deux niveaux :
 *  - un presse-papiers interne au processus (copier/coller au sein de la TUI) ;
 *  - un « puits système » optionnel : quand un backend le fournit (OSC 52 sur
 *    un terminal réel), App l'enregistre ici pour que la copie atteigne aussi
 *    le presse-papiers du bureau.
 */

#pragma once

#include <functional>
#include <string>

namespace tui {

class Clipboard {
public:
    [[nodiscard]] static Clipboard& instance() {
        static Clipboard c;
        return c;
    }

    /// Copie `text` dans le presse-papiers interne et, si un puits système
    /// est enregistré, le propage (OSC 52).
    void set(std::string text) {
        text_ = std::move(text);
        if (system_sink_) system_sink_(text_);
    }

    [[nodiscard]] const std::string& get() const { return text_; }
    [[nodiscard]] bool empty() const { return text_.empty(); }

    /// Enregistre (ou retire, en passant un callback vide) le puits système.
    void set_system_sink(std::function<void(const std::string&)> sink) { system_sink_ = std::move(sink); }

private:
    Clipboard() = default;

    std::string text_;
    std::function<void(const std::string&)> system_sink_;
};

} // namespace tui
