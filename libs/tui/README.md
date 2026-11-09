# libtui — styles, thèmes et souris

Bibliothèque TUI retained-mode en C++23 (`#include <tui/Tui.hpp>`). Cette page
décrit les feuilles de style à la Textual, les thèmes, la gestion de la souris
et les widgets ajoutés. Démonstration complète : `make build-app NAME=tui-gallery`
puis `build/release/apps/tui-gallery/tui-gallery [fichier.tcss]`.

## Compatibilité

Sans feuille de style, le rendu est **strictement** celui d'avant : les styles
ne sont calculés que lorsqu'une feuille est chargée. Les applications existantes
n'ont rien à changer.

## Démarrage rapide

```cpp
tui::App app(std::make_unique<tui::PosixTerminalBackend>());
app.use_default_stylesheet();          // apparence Textual (optionnel)
app.set_theme("nord");                 // textual-dark par défaut
app.set_css("Button.-danger { background: $error; }");
app.load_css_file("app.tcss", /*watch=*/true);   // rechargé dès qu'il change

auto ok = std::make_shared<tui::Button>("Valider");
ok->set_id("ok");
ok->add_class("-primary");
ok->set_inline_style("min-width: 16");          // style en ligne, prioritaire
```

Les erreurs d'analyse (avec fichier, ligne, colonne) sont dans `app.css_errors()` ;
les règles valides restent appliquées.

## Syntaxe TCSS

```css
$accent2: #ff8800;                    /* variable (surcharge celles du thème) */

Screen { background: $background; }   /* le widget racine */
Button { background: $surface; }      /* type = nom de la classe C++ */
#save, .-primary, *                   /* id, classe, universel */
Vertical > Button.big:focus { }       /* enfant direct, classes, pseudo-classe */
Border Label { }                      /* descendant */

Button {
    color: $text;                     // commentaire de fin de ligne
    &:hover { background: $surface-lighten-1; }   /* imbrication */
    &.-primary { background: $primary; }
}
```

Un sélecteur de type correspond aussi aux classes dérivées pour les types de la
bibliothèque (`Input` correspond à un `FormInput : tui::Input`). Pour vos propres
bases : `tui::css::register_type<MaBase>("MaBase")`.

**Pseudo-classes** : `:focus`, `:blur`, `:hover`, `:disabled`, `:enabled`,
`:focus-within`, `:root`, `:dark`, `:light`, `:first-child`, `:last-child`,
`:even`, `:odd`.

**Cascade** : feuille par défaut < feuille de l'application < style en ligne ;
puis spécificité (ids, classes et pseudo-classes, types) ; puis ordre d'apparition.

### Propriétés

| Propriété | Valeurs |
|---|---|
| `color`, `background` | `#rgb`, `#rrggbb`, `rgb(r,g,b)`, nom, `$variable`, suffixe `N%` (opacité), `color: auto [N%]` (texte contrasté avec le fond de chaque cellule) |
| `text-style` | `bold italic underline reverse dim strike blink none` |
| `border` | `none`, `solid`, `round`, `heavy`, `double`, `ascii`, `dashed`, `blank`, `thick` + couleur facultative |
| `border-color`, `border-title-color`, `border-title-style` | couleur / style de texte |
| `padding` | 1, 2 ou 4 entiers |
| `width`, `height` | cellules, `N%`, `auto` |
| `min-width`, `max-width`, `min-height`, `max-height` | cellules |
| `display` | `block`, `none` |

Toute autre propriété est conservée pour les widgets qui la lisent :
`text-align` (Button, Digits), `cursor-background`, `cursor-color`,
`cursor-text-style`, `cursor-blurred-background` (ListView, TreeView),
`key-color`, `description-color`, `hover-background` (Footer), `icon-color`
(Header), `line-style` (Rule), `min-color`, `max-color` (Sparkline),
`dim-color` (LoadingIndicator), `title-color` (Collapsible), `frame` (Border).
Un widget les lit avec `style().extra_color("…")` / `extra_value("…")`.

Le fond et la couleur d'un widget s'appliquent aux cellules qu'il peint avec la
couleur « par défaut » ; ils se propagent donc à ses enfants. Le titre de
bordure se règle par `widget->set_border_title("…")` / `set_border_subtitle`.

## Thèmes

Thèmes intégrés : `textual-dark`, `textual-light`, `nord`, `gruvbox`,
`catppuccin-mocha|macchiato|frappe|latte`, `dracula`, `tokyo-night`, `monokai`,
`flexoki`, `solarized-dark|light`, `rose-pine`, `rose-pine-moon`,
`rose-pine-dawn`, `atom-one-dark|light`, `planor`.

Chaque thème génère, comme Textual, `$primary $secondary $accent $warning
$error $success $foreground $background $surface $panel $boost`, leurs nuances
`-lighten-1..3` / `-darken-1..3`, ainsi que `$text $text-muted $text-disabled
$text-primary… $primary-muted… $border $border-blurred $block-cursor-*
$footer-* $input-* $link-* $scrollbar*`.

```cpp
tui::themes::ThemeSpec t;
t.name = "maison";
t.primary = {200, 60, 60};
t.background = tui::Color{16, 16, 20};
t.variables["ma-couleur"] = "#00aa88";
tui::themes::register_theme(t);
app.set_theme("maison");
app.next_theme();                // thème suivant
app.role_theme();                // palette par rôle (tui::Theme) dérivée
```

## Souris

`App::run()` active la souris dans le terminal (clics, molette, mouvements —
modes 1000/1002/1003 + SGR) et la désactive en sortant. `app.set_mouse_enabled(false)`
la coupe. Tant qu'elle est active, maintenir **Maj** permet de sélectionner du
texte à la souris dans la plupart des terminaux.

- **clic = sélection** dans les widgets à éléments : onglets (`Tabs`, molette sur
  la barre = onglet voisin), `ListView` (double-clic = activer, clic sur `[x]` =
  cocher), `TreeView` (clic sur ▸ ou double-clic = déplier), `TableView`
  (double-clic ou re-clic = activer, molette), `RadioSet`, `CheckboxList`,
  `Dropdown` (ouvrir, choisir, molette sur la ligne fermée), `MenuBar` (ouvrir
  un menu, survol pour passer d'un menu à l'autre, clic sur un élément) ;
- **double-clic** : `MouseEvent::clicks` vaut 2 (délai réglable :
  `app.set_double_click_interval`) ;
- **popups débordants** : un widget qui peint hors de ses bornes (menu, liste
  déroulante) surcharge `overlay_contains(Point)` pour capter la souris dans
  cette zone ;
- **clic = focus** : l'ancêtre focusable le plus proche du point cliqué reçoit le
  focus (désactivable : `app.set_click_to_focus(false)`) ;
- **remontée** : un événement non consommé par un widget est proposé à ses parents ;
- **capture** : après un appui consommé, glisser et relâcher reviennent au même
  widget (un `Button` relâché hors de lui annule le clic) ;
- **survol** : `widget->hovered()` et la pseudo-classe `:hover` ;
- **modales** : avec une `Dialog` ouverte, le reste de l'interface ne reçoit rien ;
- **désactivé** : `set_disabled(true)` retire le widget (et ses descendants) du
  focus et de la souris ;
- `Input` place son curseur au clic.

Pour les tests : `app.dispatch(Event{…})`, `app.refresh()`, `app.widget_at(x, y)`.

## Nouveaux widgets

| Widget | Rôle |
|---|---|
| `Header` | titre — sous-titre centrés, icône cliquable, horloge |
| `Footer` | raccourcis cliquables `^q Quitter`, survol |
| `Rule` | séparateur horizontal / vertical (plusieurs traits) |
| `Sparkline` | mini-graphique en barres, rééchantillonné |
| `Digits` | grands chiffres sur 3 lignes |
| `LoadingIndicator` | points animés (`advance()` depuis un timer) |
| `Log` | journal défilant, limite de lignes, suivi automatique |
| `Collapsible` | section repliable (clic, Entrée, flèches) |
| `ContentSwitcher` | une page visible à la fois, choisie par id |
| `Placeholder` | bloc coloré de maquette |
| `Link` | lien activable (clic, Entrée) |

## Écrire un widget compatible

- peindre avec `Color::Default()` ce qui doit suivre le thème ;
- dans un conteneur, appeler `child->measure_outer()`, `child->layout()` et
  `child->render()` plutôt que `measure/arrange/paint` (bordure, padding,
  dimensions et couleurs CSS) ;
- consulter `has_focus()`, `hovered()`, `disabled()` et `style()` au besoin.
