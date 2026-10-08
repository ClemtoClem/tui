#pragma once

#include "LinearLayout.hpp"

namespace tui {

/// Aligne ses enfants horizontalement (axe principal = largeur).
class Horizontal : public LinearLayout {
public:
    Horizontal() : LinearLayout(/*vertical=*/false) {}
};

} // namespace tui
