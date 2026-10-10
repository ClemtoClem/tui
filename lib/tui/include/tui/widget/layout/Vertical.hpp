#pragma once

#include "LinearLayout.hpp"

namespace tui {

/// Empile ses enfants verticalement (axe principal = hauteur).
class Vertical : public LinearLayout {
public:
    Vertical() : LinearLayout(/*vertical=*/true) {}
};

} // namespace tui
