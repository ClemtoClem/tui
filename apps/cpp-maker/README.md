# cpp-maker 1.0

Outil de création, de compilation, de test et de livraison de projets C et C++, utilisable
**en ligne de commande** ou via une **interface TUI** qui édite les fichiers TOML du projet.

Un seul binaire, sans CMake ni Make : cpp-maker lit la configuration, construit un graphe de
tâches (compilation → archive/édition de liens → tests) et l'exécute en parallèle avec une
compilation incrémentale fiable (empreinte FNV-1a de l'outil, de la ligne de commande et de
chaque entrée, `#include` transitifs compris).

```sh
cpp-maker new hello && cd hello
cpp-maker build          # compile (mode debug)
cpp-maker run            # compile et exécute
cpp-maker test           # compile et lance les tests
cpp-maker tui            # interface interactive
```

---

## 1. Organisation d'un projet

```
hello/
├── project.toml                       projet : build, modes, toolchains, tests, paquets, déploiements
├── libs/                              layers ([paths].layers)
│   └── core/
│       ├── layer.toml                 layer : dépendances et options héritées par ses modules
│       └── hello/
│           ├── module.toml            module : bibliothèque static | shared | header-only
│           ├── include/hello/hello.hpp
│           ├── src/hello.cpp
│           └── tests/test_hello.cpp
└── apps/                              applications ([paths].applications)
    └── hello/
        ├── application.toml           application : exécutable
        └── src/main.cpp
```

| Notion | Fichier | Identifiant | Rôle |
|---|---|---|---|
| Projet | `project.toml` | — | réglages communs, modes, toolchains, paquets, déploiements |
| Layer | `libs/<layer>/layer.toml` | `<layer>` | couche d'architecture regroupant des modules |
| Module | `libs/<layer>/<module>/module.toml` | `<layer>.<module>` | bibliothèque |
| Application | `apps/<app>/application.toml` | `<app>` | exécutable (jamais une dépendance) |

**Architecture en couches.** `[layers].order` liste les layers de la plus basse à la plus
haute : un module ne peut dépendre que de modules de sa couche ou d'une couche inférieure.
La validation refuse aussi les cycles, les dépendances inconnues, les paquets interdits
(`[forbid].pkg_config` d'un layer) et les artefacts en double.

**Désigner des cibles** (dans `dependencies.modules` comme en ligne de commande) :

| Forme | Désigne |
|---|---|
| `core.log` | le module `log` du layer `core` |
| `log` | le module `log` s'il est unique (ou dans le même layer) |
| `core.*` ou `core` | tous les modules du layer `core` |
| `hello` | l'application `hello` (prioritaire en ligne de commande) |
| `all` | toutes les cibles |
| *(rien)* | la cible du dossier courant, sinon tout le projet |

Artefacts : `build/<toolchain>/<mode>/{bin,lib,tests}/` · paquets : `dist/`.

## 2. Fichiers de configuration

Toutes les clés sont décrites par un **schéma** unique (`cpp-maker config keys project|layer|module|app`)
qui sert à la validation (clés inconnues signalées avec suggestion), à `config set` et aux
formulaires de la TUI. Les modifications faites par cpp-maker conservent les commentaires et
la mise en forme ; un fichier n'est écrit que si **tout le projet** reste valide.

```toml
# project.toml
[project]
name = "hello"
version = "0.1.0"
language = "c++"              # langage par défaut des nouvelles cibles
cxx_standard = "c++23"
c_standard = "c17"

[paths]                       # chaque chemin : "dossier" ou ["dossier", …]
layers = "libs"                         # ou ["libs", "vendor"]
applications = ["apps", "examples"]     # ou "apps"
build = "build"                         # un seul dossier (tableau d'un élément accepté)
dist = "dist"                           # idem
out = ".out"                            # fichiers de --output/--stdout/--stderr sans dossier

[layers]
order = ["core", "net"]       # du plus bas au plus haut

[build]
default_mode = "debug"
default_toolchain = "host"
jobs = 0                      # 0 = cœurs - 1

[build.modes.profiling]       # mode personnalisé
base = "relwithdebinfo"
flags = ["-fno-omit-frame-pointer"]

[toolchains.armhf]            # compilation croisée (exécution via qemu si présent)
prefix = "arm-linux-gnueabihf-"

[tests]
framework = "builtin"         # builtin | gtest | none
timeout = 300

[package]
formats = ["tar.gz", "deb"]

[deploy.rpi]
method = "rsync"              # rsync | scp | copy | script
host = "raspberrypi.local"
path = "/home/pi/{name}"
toolchain = "armhf"

[generate.proto]              # génération de code avant le build
command = "protoc --cpp_out=libs/net/msg/src msg.proto"
inputs = ["msg.proto"]
```

```toml
# libs/net/http/module.toml
[module]
name = "http"
type = "shared"               # static | shared | header-only
language = "c++"

[dependencies]
modules = ["core.log"]
pkg_config = ["zlib"]
system = ["pthread"]

[dependencies.git.fmt]        # projet cpp-maker distant : modules importés sous « fmt.* »
url = "https://example.com/fmt-cpp-maker.git"
version = "v1.2.0"            # versions verrouillées dans cpp-maker.lock

[platform.windows]
system = ["ws2_32"]
```

```toml
# apps/hello/application.toml
[application]
name = "hello"

[dependencies]
modules = ["core.hello", "net.*"]

[run]
args = ["--verbose"]          # arguments par défaut de « cpp-maker run »
env = { LANG = "C" }
```

Tests : avec `framework = "builtin"`, cpp-maker fournit `#include <cpp-maker/test.hpp>`
(macros `TEST`, `CHECK_EQ`, `REQUIRE`, `SKIP`, compatibilité `EXPECT_*`/`ASSERT_*`) et le
`main` des tests ; chaque cible ayant un dossier `tests/` produit `<cible>-tests`.

## 3. Commandes

`cpp-maker help <commande>` ou `cpp-maker <commande> --help` détaille chaque commande.
Options globales : `-C <dossier>`, `--no-color`, et les redirections `--output <f>` (stdout +
stderr), `--stdout <f>`, `--stderr <f>`, placées avant ou après la commande et combinables. Un nom
sans dossier est rangé dans `[paths].out` (défaut `.out`) ; la sortie des processus lancés
(compilateur, tests) est capturée aussi : `cpp-maker test --output tests.log`.

### Projet
| Commande | Rôle |
|---|---|
| `new <nom> [--layout full\|app\|lib\|empty] [--lang c\|c++] [--std …]` | créer un projet |
| `init` | créer `project.toml` dans le dossier courant |
| `add layer <nom> [--dir D]` | ajouter un layer (dans l'un des dossiers de `[paths].layers`) |
| `add module <layer> <nom> [--type …] [--deps …]` | ajouter un module (crée le layer si besoin) |
| `add app <nom> [--deps …] [--dir D]` | ajouter une application (dans l'un des dossiers de `[paths].applications`) |
| `remove layer\|module\|app <nom> [--force] [--purge]` | retirer (corbeille `.cpp-maker-trash/`, ou définitif) |
| `rename layer\|module\|app <ancien> <nouveau>` | renommer et mettre à jour toutes les références |
| `list [layers\|modules\|apps] [--names]` | lister |
| `info [<cible>\|<layer>]` | détail du projet, d'une cible ou d'un layer |
| `graph [--dot] [--all]` | graphe des dépendances |

### Dépendances
| Commande | Rôle |
|---|---|
| `deps list [<cible>]` | dépendances résolues et versions git verrouillées |
| `deps add <cible> <dep>… [--module\|--pkg-config\|--system]` | ajouter (type détecté sinon) ; `<cible>` peut être un layer ou `project` |
| `deps remove <cible> <dep>…` | retirer |
| `deps fetch` / `deps update` / `deps clean` | résoudre / mettre à jour / vider le cache git |

### Construction
| Commande | Rôle |
|---|---|
| `build [mode…] [cible…]` | compiler (`-t tc`, `-j N`, `-k`, `-n`, `-v`, `--tests`, `--clean`, `--backend ninja`) |
| `run [mode] [app] [-- args]` | compiler puis exécuter (`run valgrind` : sous valgrind) |
| `test [mode…] [cible…] [--filter] [--junit F]` | compiler et lancer les tests |
| `check [modes] [cible…]` | tests sur plusieurs modes (défaut : debug, asan, ubsan) |
| `clean [mode] [cible…] [-t tc] [--all]` | supprimer les artefacts |
| `size [mode] [cible…]` | tailles text/data/bss (lecteur ELF intégré) |

Les modes s'écrivent en positionnel, seuls ou en liste : `build release`, `test debug,asan`.
Sanitizers à la volée : `--sanitize address,undefined` (mode dérivé `debug+address+undefined`
dans son propre dossier), `--sanitize all` (une passe par runtime), `--sanitize-trap`,
`--no-sanitize-recover`, `--sanitize-ignorelist` ; catalogue : `cpp-maker help sanitize`.

### Livraison
| Commande | Rôle |
|---|---|
| `package [mode] [--format tar.gz,tar.xz,zip,deb,stage]` | paquets dans `dist/` (+ SHA-256) |
| `install [mode] [--prefix P] [--destdir D]` | installation (bin, lib, include, pkg-config) |
| `deploy list` / `deploy <nom> [-n]` | déploiement rsync, scp, copie ou script |

### Outils
| Commande | Rôle |
|---|---|
| `config show\|get\|set\|unset\|add\|remove [--layer L\|--module M\|--app A]` | lire / modifier les fichiers toml |
| `config validate` / `config keys [type]` / `config edit` | valider / lister les clés / éditer dans `$EDITOR` |
| `export compdb\|ninja\|xml` | `compile_commands.json`, `build.ninja`, archive XML de cibles |
| `toolchain list\|show\|which\|add\|use\|install\|remove` | compilateurs, profils, versions gcc (apt) |
| `doctor [--fix] [--strict]` | diagnostic (config, outils, toolchains, `#include` non déclarés) |
| `tui` | interface interactive |

```sh
cpp-maker config set build.jobs 4
cpp-maker config set module.type shared --module core.log
cpp-maker config add build.warnings -- -Wshadow          # valeurs commençant par « - » après --
cpp-maker config set deploy.rpi.method=rsync deploy.rpi.host=rpi.local deploy.rpi.path=/opt/app
```

### Git et règles pre-commit
| Commande | Rôle |
|---|---|
| `git status` | état du dépôt groupé par cible (● indexé, ○ modifié, ? non suivi) |
| `git add <cible…>\|--all` / `git unstage …` | indexer / désindexer les fichiers de cibles |
| `git commit -m <msg> [-a] [--no-verify]` | valider (le crochet pre-commit s'exécute) |
| `git log [<cible>]` / `git diff [<cible>] [--staged]` | historique / différences d'une cible |
| `git push` / `git pull` / `git init` | synchronisation ; init = dépôt + `.gitignore` + crochet |
| `git hook install\|uninstall\|status` | crochet pre-commit (un crochet existant est sauvegardé avec `--force`) |
| `git rule list\|add\|remove` | règles pre-commit des fichiers toml |
| `git pre-commit [--all] [-n] [-k]` | exécuter les règles (appelé par le crochet) |

Les règles se déclarent dans **n'importe lequel des quatre fichiers** ; leur portée en dépend :
`project.toml` = tous les fichiers indexés, `layer.toml` = ceux du layer, `module.toml` /
`application.toml` = ceux de la cible. Une règle ne s'exécute que si un fichier indexé de sa portée
(filtré par `when`) est concerné.

```toml
[git.pre_commit.tests]                  # project.toml : tests des cibles modifiées
run = "test {targets}"                  # arguments de cpp-maker
env = { SDL_VIDEO_DRIVER = "offscreen" }

[git.pre_commit.sanitizers]             # module.toml de vk : seulement si vk est touché
run = "check --modes asan,tsan {targets}"

[git.pre_commit.perf]                   # commande shell, sur certains chemins, non bloquante
command = "tools/perf_gate.sh"
when = ["libs/infrastructure/render3d/**", "apps/voxelia/**"]
on_failure = "warn"
```

Variables : `{targets}` (cibles touchées de la portée), `{files}`, `{root}`, `{name}`. Les règles
sont **tenues à jour automatiquement** : `rename` réécrit leurs références et chemins, `remove`
retire les références (et supprime une règle qui ne visait plus que l'élément retiré), `doctor`
signale les références mortes et `doctor --fix` les nettoie (une règle vidée est désactivée).

## 4. Interface TUI

`cpp-maker tui` (ou `cpp-maker ui`) depuis n'importe quel dossier du projet :

- **F2 Configuration** — arbre du projet (projet, compilation, tests, paquets, modes,
  toolchains, déploiements, génération, layers ▸ modules, applications). Pour l'élément
  sélectionné : un **formulaire généré depuis le schéma** (champ vide = valeur par défaut,
  `‹ choix ›` modifiables avec ←/→) et des boutons d'action (ajouter, renommer, supprimer,
  compiler, tester…). **Ctrl+S** enregistre après validation complète du projet.
- **F3 Fichier TOML** — édition brute, avec coloration, du fichier de l'élément sélectionné ;
  le fichier n'est écrit que si le projet reste valide.
- **F8 Git** — modifications groupées par cible (Entrée : indexer / désindexer), commit avec ou
  sans crochet, simulation du pre-commit, journal, différences, push / pull, installation du crochet.
  Les règles pre-commit sont éditables dans l'arbre de F2 (nœud « Règles pre-commit »).
- **F4 Exécution** — choix du mode, de la toolchain, de la cible, des sanitizers… et boutons
  qui lancent les commandes dans un **terminal intégré** (F5 build, F6 run, F7 test).

Les actions de la TUI relancent la ligne de commande de cpp-maker : mêmes règles, mêmes
messages. Si les fichiers changent hors de la TUI, elle se recharge (sauf édition en cours).
Sans projet, elle propose d'en créer un ou d'adopter le dossier courant.

**Souris** : clic pour choisir un onglet, un élément de l'arbre, un champ ou un bouton ;
double-clic pour ouvrir (aller au formulaire, indexer un fichier) ; molette pour défiler ;
les raccourcis du pied de page sont cliquables. Maintenir Maj pour sélectionner du texte.

**Thèmes et style** : l'interface suit un thème de couleurs (celui de Textual) — `planor`
par défaut, `cpp-maker tui --themes` pour la liste. **F9** ouvre le sélecteur (aperçu
immédiat, Entrée pour garder, Échap pour revenir), **Ctrl+T** passe au suivant ; le choix
est mémorisé dans `~/.config/cpp-maker/tui-theme`.

```sh
cpp-maker tui --theme nord            # thème pour cette session
cpp-maker tui --css perso.tcss        # feuille de style en plus (rechargée à chaud)
```

`~/.config/cpp-maker/tui.tcss`, s'il existe, est chargé à chaque lancement et rechargé dès
qu'il change. Exemple :

```css
Border:focus-within { border-color: $success; }
NavTree:focus { cursor-background: $accent; }
Button.-primary { background: $secondary; }
#status { color: $text; }
```

Sélecteurs utiles : `NavTree`, `ChoiceField`, `Input`, `Button` (variantes `.-primary`,
`.-success`, `.-error`), `Border`, `Tabs`, `Header`, `Footer`, `Dialog`, `#title`,
`#status`, `#help`, `.section` (titres du formulaire), `.field-label`, et les tons
`.-accent .-info .-muted .-success .-warning .-error`. Syntaxe complète : `libs/tui/README.md`.

## 5. Correspondance avec cpp-maker 0.8

| 0.8 | 1.0 |
|---|---|
| `cpp-maker.toml` (+ `[workspace]`) | `project.toml` ; layers et applications découverts automatiquement |
| `module.toml` dans un workspace | `libs/<layer>/<module>/module.toml` (bibliothèques) et `apps/<app>/application.toml` (exécutables) |
| `[layers]` du projet | `[layers].order` + un `layer.toml` par layer |
| `add <chemin>`, `module add` | `add layer`, `add module`, `add app` |
| `module rename`, `module remove` | `rename …`, `remove …` |
| `module deps …` | `deps list\|add\|remove` |
| `deps list\|update\|clean` (git) | `deps list\|fetch\|update\|clean` |
| `modules` | `list` |
| `generate ninja\|compdb` | `export ninja\|compdb` |
| `backup` | `export xml` |
| `valgrind` | `run valgrind` |
| `doctor` (corrige par défaut) | `doctor` (diagnostic) / `doctor --fix` |
| `check` (défaut debug,asan,tsan) | `check` (défaut debug,asan,ubsan) |
| options `--output/--stdout/--stderr` | identiques ; `--filter`/`--grep` : `grep` du shell |

## 6. Architecture du code

```
include/ src/
  core/        utilitaires, sortie console, processus, version
  io/          toml (parseur, sérialiseur, Document éditable), args (analyse des options)
  model/       modèle, schéma, chargement/validation (Workspace), transactions, scaffolding, git
  toolchain/   détection des compilateurs, cibles croisées, sanitizers
  build/       planificateur (DAG), exécuteur parallèle, sessions, tests, exports
  deliver/     staging, paquets, installation, déploiement
  analysis/    lecteur ELF, analyse des #include
  vcs/         git : état par cible, crochet, règles pre-commit et leur maintenance
  commands/    une classe par commande (patron Commande) + GlobalContext (registre, aiguillage)
  ui/          TUI : widgets, formulaires générés, application
tests/         tests GoogleTest (toml, args, workspace/schéma, règles git)
```

Ajouter une commande : dériver `commands::Command` (`GetName`, `GetDescription`,
`ConfigureArgs`, `Run`…), ou `CommandGroup` pour une commande à sous-commandes, puis
l'enregistrer dans `commands/BuiltinCommands.cpp`.

Compilation dans ce dépôt : `make build-app NAME=cpp-maker` (binaire dans
`build/<mode>/apps/cpp-maker/`) ; tests : `make run-tests NAME=cpp-maker` (GoogleTest requis).
Variable `CPP_MAKER_EXE` : exécutable relancé par la TUI pour ses actions (défaut : lui-même).
