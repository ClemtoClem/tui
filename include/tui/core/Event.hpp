/**
 * @file Event.hpp
 * @brief Événements d'entrée normalisés (clavier, souris, resize, paste, focus).
 *
 * Note d'ordonnancement : ce fichier appartient conceptuellement à la
 * sous-phase 1.3 (plan Phase 1) mais est introduit ici en sous-phase 1.2
 * car `ITerminalBackend::poll_event()` doit retourner un `Event` — même
 * dépendance en avance que celle déjà documentée pour Dropdown/ListView.
 */

#pragma once

#include <string>
#include <variant>

namespace tui {

enum class Key {
    Unknown,
    Char, // caractère imprimable : voir KeyEvent::codepoint
    Enter,
    Escape,
    Backspace,
    Tab,
    BackTab, // Shift+Tab
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    PageUp,
    PageDown,
    Delete,
    Insert,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
};

struct KeyEvent {
    Key key = Key::Unknown;
    char32_t codepoint = 0; // valide seulement si key == Key::Char
    bool shift = false;
    bool ctrl = false;
    bool alt = false;

    constexpr bool operator==(const KeyEvent&) const = default;
};

struct MouseEvent {
    enum class Button { None, Left, Middle, Right, WheelUp, WheelDown };
    enum class Action { Press, Release, Drag, Move };

    int x = 0;
    int y = 0;
    Button button = Button::None;
    Action action = Action::Move;
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
};

struct ResizeEvent {
    int width = 0;
    int height = 0;
};

struct PasteEvent {
    std::string text;
};

struct FocusEvent {
    bool gained = false;
};

using Event = std::variant<KeyEvent, MouseEvent, ResizeEvent, PasteEvent, FocusEvent>;

} // namespace tui
