/**
 * @file MenuItem.hpp
 * @brief Entrée d'un menu déroulant (MenuDialog).
 */

#pragma once

#include <functional>
#include <string>

namespace tui {

struct MenuItem {
    std::string label;
    std::function<void()> on_activate;
    bool separator = false; // ligne séparatrice non sélectionnable si true (label ignoré)
};

} // namespace tui
