/**
 * @file ITerminalBackend.hpp
 * @brief Abstraction du terminal physique (ou simulé) utilisée par le Renderer/App.
 */

#pragma once

#include "../core/Cell.hpp"
#include "../core/Event.hpp"

#include <chrono>
#include <optional>
#include <stdexcept>
#include <string>

namespace tui {

/// Erreur fatale d'initialisation/E-S du terminal (remplace l'ancien
/// Expected<T,E> abandonné - ces échecs sont rares et se produisent au
/// démarrage, une exception classique est plus simple qu'un type
/// monadique dédié).
class TerminalError : public std::runtime_error {
public:
    explicit TerminalError(const std::string& message) : std::runtime_error(message) {}
};

/// Interface implémentée par chaque backend terminal (Posix réel, ou
/// backend "headless" utilisé par les tests).
class ITerminalBackend {
public:
    virtual ~ITerminalBackend() = default;

    /// Passe le terminal en mode raw / écran alternatif. Lève
    /// TerminalError en cas d'échec (ex: pas un vrai TTY).
    virtual void init() = 0;

    /// Restaure l'état du terminal tel qu'avant init().
    virtual void shutdown() = 0;

    /// Attend un événement au plus `timeout`. Retourne std::nullopt si
    /// le délai expire sans événement.
    [[nodiscard]] virtual std::optional<Event> poll_event(std::chrono::milliseconds timeout) = 0;

    /// Dessine une seule cellule (le Buffer n'appelle ceci que pour les
    /// cellules qui ont changé depuis la dernière frame).
    virtual void draw_cell(int x, int y, const Cell& cell) = 0;

    /// Pousse physiquement le contenu dessiné à l'écran.
    virtual void flush() = 0;

    [[nodiscard]] virtual int get_width() const = 0;
    [[nodiscard]] virtual int get_height() const = 0;

    virtual void clear_screen() = 0;

    virtual void set_cursor_visible(bool visible) = 0;
    virtual void set_cursor_position(int x, int y) = 0;

    /// Active/désactive le rapport des événements souris (SGR mode).
    virtual void enable_mouse(bool enabled) = 0;

    /// Indique si le terminal annonce un support true-color (24-bit).
    /// Si faux, les couleurs RGB doivent être quantifiées (256 couleurs)
    /// par le backend avant émission.
    [[nodiscard]] virtual bool supports_true_color() const = 0;

    /// Réveille un poll_event() bloquant depuis un autre thread (utilisé
    /// par App::post()). Pas d'effet par défaut : seul un backend dont
    /// poll_event() bloque réellement (PosixTerminalBackend) a besoin
    /// d'implémenter ce mécanisme.
    virtual void wake_up() {}
};

} // namespace tui
