/* @file style/DefaultCss.cpp — feuille de style par défaut (apparence Textual). */

#include "tui/style/Stylesheet.hpp"

namespace tui::css {

std::string_view default_stylesheet() {
    return R"TCSS(
/* ---------------------------------------------------------------
 * Feuille par défaut de libtui, inspirée de Textual.
 * Chargée avec App::use_default_stylesheet() ; le CSS de
 * l'application (App::set_css / load_css_file) la surcharge.
 * ------------------------------------------------------------- */

Screen {
    background: $background;
    color: $foreground;
}

*:disabled { color: $text-disabled; }

/* --- En-tête, pied, séparateurs -------------------------------- */
Header {
    background: $panel;
    color: $foreground;
    icon-color: $accent;
}
Footer {
    background: $footer-background;
    color: $footer-foreground;
    key-color: $footer-key-foreground;
    description-color: $footer-description-foreground;
    hover-background: $block-hover-background;
}
StatusBar, ShortcutBar { background: $panel; color: $foreground; }
Rule { color: $secondary; }

/* --- Boutons ---------------------------------------------------- */
Button {
    background: $surface-lighten-1;
    color: $button-foreground;
    text-style: bold;
    padding: 0 2;
    min-width: 10;
}
Button:hover { background: $surface-lighten-2; }
Button:focus { background: $primary-darken-1; color: $button-color-foreground; }
Button.-primary { background: $primary; color: auto 90%; }
Button.-primary:hover { background: $primary-lighten-1; }
Button.-success { background: $success; color: auto 90%; }
Button.-success:hover { background: $success-lighten-1; }
Button.-warning { background: $warning; color: auto 90%; }
Button.-warning:hover { background: $warning-lighten-1; }
Button.-error { background: $error; color: auto 90%; }
Button.-error:hover { background: $error-lighten-1; }
Button.-primary:focus, Button.-success:focus, Button.-warning:focus, Button.-error:focus {
    text-style: bold underline;
}
Button:disabled { background: $surface; color: $text-disabled; }

/* --- Saisie ----------------------------------------------------- */
Input, NumberInput {
    background: $surface;
    color: $foreground;
    padding: 0 1;
}
Input:hover { background: $surface-lighten-1; }
Input:focus { background: $boost; color: $text; }
TextArea, CodeEditor { background: $surface; color: $foreground; }

/* --- Listes et arbres ------------------------------------------- */
ListView, TreeView, TableView, CheckboxList {
    background: $surface;
    color: $foreground;
    cursor-background: $block-cursor-blurred-background;
    cursor-blurred-background: $block-cursor-blurred-background;
    cursor-blurred-color: $foreground;
}
ListView:focus, TreeView:focus, TableView:focus, CheckboxList:focus {
    cursor-background: $block-cursor-background;
    cursor-color: $block-cursor-foreground;
    cursor-text-style: bold;
}

/* --- Conteneurs ------------------------------------------------- */
Border { border-color: $border-blurred; border-title-color: $text-muted; }
Border:focus-within { border-color: $border; border-title-color: $text; border-title-style: bold; }
Dialog {
    color: $foreground;
    border-color: $primary;
    border-title-color: $text;
    dialog-background: $panel;
    scrim-color: $background-darken-3;
    scrim-opacity: 55;
}
Tabs {
    color: $foreground;
    active-color: $text;
    inactive-color: $text-muted;
    hover-background: $boost;
    indicator-color: $accent;
    track-color: $panel-lighten-1;
}

/* --- Contrôles divers ------------------------------------------- */
Checkbox, RadioSet, Switch { color: $foreground; }
Checkbox:focus, RadioSet:focus, Switch:focus { text-style: bold; }
ProgressBar { color: $primary; }
Label.-muted { color: $text-muted; }
Label.-primary { color: $text-primary; }
Label.-success { color: $text-success; }
Label.-warning { color: $text-warning; }
Label.-error { color: $text-error; }

/* --- Nouveaux widgets ------------------------------------------- */
Sparkline { min-color: $primary-darken-2; max-color: $accent; }
Digits { color: $text; }
LoadingIndicator { color: $accent; dim-color: $surface-lighten-1; }
Log { background: $surface; color: $foreground; }
Collapsible { title-color: $text; }
Collapsible:focus { title-color: $accent; }
Link { color: $text-accent; text-style: underline; }
Link:hover { background: $link-background-hover; color: $link-color-hover; }
Link:focus { text-style: bold underline; }
Placeholder { color: $text; }
)TCSS";
}

} // namespace tui::css
