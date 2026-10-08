# ============================================================================
# Makefile - Text Unit Interface
# ============================================================================

# Compilateur et options C++
CXX      := g++

# Répertoires
SRCDIR   := src
INCDIR   := include
BUILDDIR := build
TESTDIR  := tests
APPSDIR  := apps

# Inclusions (-I) :
# - I $(INCDIR) : permet #include <affichage.hpp> et #include "affichage.hpp" si le fichier est dans include/
# - I $(SRCDIR) : permet d'inclure les en-têtes directement depuis n'importe quel sous-dossier de src/
CXXFLAGS := -Wall -Wextra -std=c++23 -I $(INCDIR) -I $(SRCDIR) -MMD -MP -g
LDFLAGS  :=
LDLIBS   := -lutil # forkpty()/openpty() (glibc) - utilisé par le panneau Terminal de l'application code-editor
TEST_LDLIBS := -lgtest -lgtest_main -pthread

# Bibliothèque statique du module ui (tout src/**/*.cpp) : c'est elle que
# tests et applications lient, plutôt que de recompiler/relinker les .o du
# projet un par un pour chaque binaire.
LIB       := $(BUILDDIR)/libtui.a
SRCS      := $(shell find $(SRCDIR) -name '*.cpp' 2>/dev/null)
OBJS      := $(patsubst $(SRCDIR)/%.cpp,$(BUILDDIR)/src/%.o,$(SRCS))

# Sources, objets et exécutables des tests
TEST_SRCS := $(shell find $(TESTDIR) -name '*.cpp' 2>/dev/null)
TEST_OBJS := $(patsubst $(TESTDIR)/%.cpp,$(BUILDDIR)/tests/%.o,$(TEST_SRCS))
TEST_BINS := $(patsubst $(TESTDIR)/%.cpp,$(BUILDDIR)/bin/%,$(TEST_SRCS))

# Fichiers de dépendances (.d) - complétés plus bas par les règles d'applications
DEPS      := $(OBJS:.o=.d) $(TEST_OBJS:.o=.d)

# Couleurs pour la cible help
YELLOW    := \033[33m
GREEN     := \033[32m
CYAN      := \033[36m
RED       := \033[31m
RESET     := \033[0m

# ============================================================================
# Bibliothèque du module ui
# ============================================================================

.PHONY: all lib clean re tests check help run list-apps run-tests run-app

all: lib ## Compile la bibliothèque du module ui (libtui.a)

lib: $(LIB) ## Alias explicite pour la bibliothèque

$(LIB): $(OBJS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^
	@echo "=== Bibliotheque compilee : $(LIB) ==="

# Compilation des sources du projet (.cpp -> .o dans build/src/)
$(BUILDDIR)/src/%.o: $(SRCDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

help: ## Affiche ce message d'aide
	@printf "$(YELLOW)Commandes disponibles :$(RESET)\n"
	@awk 'BEGIN {FS=":[[:space:]]*.*## "} \
		/^[[:alnum:]_.-]+[[:space:]]*:/ && /##/ { \
			printf "  $(CYAN)%-18s$(RESET) %s\n", $$1, $$2 \
		}' $(MAKEFILE_LIST)
	@printf "\n$(YELLOW)Point d'entree unique :$(RESET)\n"
	@printf "  $(CYAN)./run$(RESET)                    lance l'application par defaut\n"
	@printf "  $(CYAN)./run --tests$(RESET)             compile et lance la suite de tests\n"
	@printf "  $(CYAN)./run --app NOM$(RESET)       compile et lance l'application NOM\n"
	@printf "  $(CYAN)./run --app$(RESET)           liste les applications disponibles\n"
	@printf "  (equivalents make purs : make run-tests / make run-app APP=NOM / make list-apps)\n"

# ============================================================================
# Tests
# ============================================================================

# Compilation des sources de test (.cpp -> .o dans build/tests/)
$(BUILDDIR)/tests/%.o: $(TESTDIR)/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Édition des liens pour chaque test (lie directement contre libtui.a)
$(BUILDDIR)/bin/%: $(BUILDDIR)/tests/%.o $(LIB)
	@mkdir -p $(dir $@)
	$(CXX) $(LDFLAGS) $< $(LIB) $(LDLIBS) $(TEST_LDLIBS) -o $@

tests: check ## Alias pour exécuter les tests
run-tests: check ## Alias make-natif de "./run --tests"

check: $(TEST_BINS) ## Compile et exécute automatiquement tous les tests
	@echo "$(CYAN)== Lancement des tests ==$(RESET)"
	@for t in $(TEST_BINS); do \
		echo "$(GREEN)[RUN] $$t$(RESET)"; \
		$$t || exit 1; \
	done
	@echo "$(GREEN)Tous les tests ont réussi !$(RESET)"

# ============================================================================
# Exemples ($(APPSDIR)/<nom>/**/*.cpp -> build/bin/<nom>)
#
# Auto-découverte : tout sous-répertoire direct de $(APPSDIR)/ est un application
# à part entière, quel que soit son organisation interne (sous-dossiers
# model/, widgets/, etc. - cf. spreadsheet ou monitor-system). Le nom du
# binaire est toujours celui du répertoire ; rien à déclarer ici pour
# ajouter un application, contrairement à l'ancien schéma EXDIR/EX2DIR/EX3DIR.
# ============================================================================

APPS := $(notdir $(patsubst %/,%,$(wildcard $(APPSDIR)/*/)))

define APP_RULES
EX_$(1)_SRCS := $$(shell find $(APPSDIR)/$(1) -name '*.cpp' 2>/dev/null)
EX_$(1)_OBJS := $$(patsubst $(APPSDIR)/$(1)/%.cpp,$$(BUILDDIR)/$(APPSDIR)/$(1)/%.o,$$(EX_$(1)_SRCS))
DEPS += $$(EX_$(1)_OBJS:.o=.d)

$$(BUILDDIR)/$(APPSDIR)/$(1)/%.o: $(APPSDIR)/$(1)/%.cpp
	@mkdir -p $$(dir $$@)
	$$(CXX) $$(CXXFLAGS) -c $$< -o $$@

$$(BUILDDIR)/bin/$(1): $$(EX_$(1)_OBJS) $$(LIB)
	@mkdir -p $$(dir $$@)
	$$(CXX) $$(LDFLAGS) $$(EX_$(1)_OBJS) $$(LIB) $$(LDLIBS) -o $$@

.PHONY: $(1) run-$(1)
$(1): $$(BUILDDIR)/bin/$(1) ## Compile l'application $(1)
run-$(1): $(1) ## Compile et lance l'application $(1)
	@./$$(BUILDDIR)/bin/$(1)
endef

$(foreach ex,$(APPS),$(eval $(call APP_RULES,$(ex))))

list-apps: ## Liste les applications disponibles (build/<nom> pour compiler)
	@printf "$(YELLOW)Exemples disponibles :$(RESET)\n"
	@for e in $(APPS); do printf "  $(CYAN)%s$(RESET)\n" "$$e"; done

# Dispatcher "make run" : cible par défaut de ./run.sh, comprend deux variables
# optionnelles passées en VAR=valeur (le seul mécanisme dont dispose make
# pour recevoir des "options" - une syntaxe --flag littérale sur la ligne de
# commande est interceptée par make lui-même avant d'atteindre ce fichier,
# d'où le petit script ./run.sh qui traduit --tests/--app pour vous).
APP ?=
DEFAULT_APP := $(firstword $(APPS))

run: ## Lance l'application par defaut (ou APP=nom / voir aussi ./run.sh)
ifneq ($(APP),)
	@$(MAKE) --no-print-directory run-$(APP)
else
ifeq ($(DEFAULT_APP),)
	@printf "$(RED)Aucun application trouve sous $(APPSDIR)/$(RESET)\n"
else
	@$(MAKE) --no-print-directory run-$(DEFAULT_APP)
endif
endif

# Nettoyage
clean: ## Supprime les dossiers de build
	rm -rf $(BUILDDIR)
	@echo "=== Repertoire build supprime ==="

# Reconstruction complète
re: clean all ## Reconstruit entièrement le projet

# Inclusion automatique des dépendances générées par GCC (-MMD -MP)
-include $(DEPS)
