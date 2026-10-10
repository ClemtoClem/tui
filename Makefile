# ============================================================================
#  Makefile - Bibliothèques, Applications et Tests
# ============================================================================
#  Arborescence attendue :
#
#    lib/<nom>/include/<nom>/...   en-têtes publics   -> #include <nom/X.hpp>
#    lib/<nom>/src/**.cpp          sources            -> libs/<nom>/lib<nom>.a
#    lib/<nom>/tests/**.cpp        tests GoogleTest   -> un binaire par fichier
#
#    apps/<nom>/include/**         en-têtes internes  -> #include "X.hpp"
#    apps/<nom>/src/main.cpp       point d'entrée     -> apps/<nom>/<nom>
#    apps/<nom>/src/**.cpp         autres sources     -> lib<nom>-core.a (tests)
#    apps/<nom>/tests/**.cpp       tests GoogleTest   -> un binaire par fichier
#
#  Les objets sont rangés sous build/<mode>/, si bien que passer de release à
#  debug (et inversement) ne recompile pas tout et ne mélange jamais des
#  objets compilés avec des drapeaux différents.
#
#  Utilisation : make <cible> [NAME=<nom>] [MODE=release|debug] [ARGS='...']
# ============================================================================

.DEFAULT_GOAL := help
.DELETE_ON_ERROR:
.SUFFIXES:

MODE ?= release
V    ?= 0

CXX    := g++
AR     := ar
CXXSTD := -std=c++23

# ----------------------------------------------------------------------------
#  Affichage : V=1 montre les commandes complètes
# ----------------------------------------------------------------------------
ifeq ($(V),1)
  Q    :=
  ECHO := @true
else
  Q    := @
  ECHO := @echo
endif

# ----------------------------------------------------------------------------
#  Fonction utilitaire : recherche récursive de fichiers
#    $(call rwildcard,<répertoire>,*.cpp)
#  Renvoie une liste vide si le répertoire n'existe pas.
# ----------------------------------------------------------------------------
rwildcard = $(foreach d,$(wildcard $(1:=/*)),$(call rwildcard,$d,$2) \
                                             $(filter $(subst *,%,$2),$d))

# ----------------------------------------------------------------------------
#  Drapeaux selon le mode
# ----------------------------------------------------------------------------
#  SAN liste les sanitizers, séparés par des virgules (address, undefined,
#  thread, leak...) ou vaut 'none'. ASan et TSan sont mutuellement exclusifs :
#  pour une chasse aux races, utilisez MODE=debug SAN=thread.
# ----------------------------------------------------------------------------
ifeq ($(MODE),debug)
  MODE_FLAGS := -g3 -O0 -fno-omit-frame-pointer
  SAN        ?= address,undefined
else ifeq ($(MODE),release)
  MODE_FLAGS := -O2 -DNDEBUG
  SAN        ?= none
else
  $(error MODE doit valoir 'release' ou 'debug' (reçu : '$(MODE)'))
endif

COMMA := ,
SAN_LIST := $(subst $(COMMA), ,$(SAN))

ifeq ($(filter none,$(SAN_LIST)),)
  ifneq ($(filter address,$(SAN_LIST)),)
    ifneq ($(filter thread,$(SAN_LIST)),)
      $(error SAN : 'address' et 'thread' sont incompatibles ; choisissez l'un ou l'autre)
    endif
  endif
  SAN_FLAGS := $(addprefix -fsanitize=,$(SAN_LIST))
else
  ifneq ($(words $(SAN_LIST)),1)
    $(error SAN : 'none' ne se combine avec rien d'autre (reçu : '$(SAN)'))
  endif
  SAN_FLAGS :=
endif

WARNINGS := -Wall -Wextra

CXXFLAGS := $(CXXSTD) $(WARNINGS) $(MODE_FLAGS) $(SAN_FLAGS) -pthread
DEPFLAGS := -MMD -MP
LDFLAGS  := $(SAN_FLAGS) -pthread
LDLIBS   :=

# ----------------------------------------------------------------------------
#  GoogleTest
# ----------------------------------------------------------------------------
GTEST_CFLAGS := $(shell pkg-config --cflags gtest 2>/dev/null)
GTEST_LIBS   := $(shell pkg-config --libs gtest_main gtest 2>/dev/null)
ifeq ($(strip $(GTEST_LIBS)),)
  GTEST_LIBS := -lgtest_main -lgtest
endif

# ----------------------------------------------------------------------------
#  Répertoires de sortie (un arbre par mode)
# ----------------------------------------------------------------------------
BUILD_DIR  := build
OUT_DIR    := $(BUILD_DIR)/$(MODE)
LIBS_DIR   := $(OUT_DIR)/libs
APPS_DIR   := $(OUT_DIR)/apps
TESTS_DIR  := $(OUT_DIR)/tests

# ----------------------------------------------------------------------------
#  Découverte des bibliothèques et des applications
# ----------------------------------------------------------------------------
LIBS := $(sort $(patsubst lib/%/,%,$(wildcard lib/*/)))
APPS := $(sort $(patsubst apps/%/,%,$(wildcard apps/*/)))

# Chemins canoniques
lib_archive  = $(LIBS_DIR)/$(1)/lib$(1).a
app_binary   = $(APPS_DIR)/$(1)/$(1)
app_archive  = $(APPS_DIR)/$(1)/lib$(1)-core.a

ALL_LIB_INCLUDES := $(foreach l,$(LIBS),-Ilib/$(l)/include)
ALL_LIB_ARCHIVES := $(foreach l,$(LIBS),$(call lib_archive,$(l)))

# ============================================================================
#  Bibliothèques : lib/<nom>/src/**.cpp -> lib<nom>.a
# ============================================================================
define LIB_RULES

LIB_$(1)_SRCS  := $$(call rwildcard,lib/$(1)/src,*.cpp)
LIB_$(1)_OBJS  := $$(patsubst lib/$(1)/src/%.cpp,$(LIBS_DIR)/$(1)/obj/%.o,$$(LIB_$(1)_SRCS))
# Les en-têtes publics sont sous include/, les en-têtes privés à côté des sources.
LIB_$(1)_INC   := -Ilib/$(1)/include -Ilib/$(1)/src
LIB_$(1)_TESTS := $$(call rwildcard,lib/$(1)/tests,*.cpp)
LIB_$(1)_TBINS := $$(patsubst lib/$(1)/tests/%.cpp,$(TESTS_DIR)/libs/$(1)/%,$$(LIB_$(1)_TESTS))
# Les tests voient les en-têtes de la bibliothèque, ceux des autres
# bibliothèques, et leurs propres en-têtes d'aide (lib/<nom>/tests).
LIB_$(1)_TESTINC := -Ilib/$(1)/tests $$(LIB_$(1)_INC) \
                    $$(filter-out -Ilib/$(1)/include,$(ALL_LIB_INCLUDES))

$(call lib_archive,$(1)): $$(LIB_$(1)_OBJS)
	$$(Q)mkdir -p $$(@D)
	$$(ECHO) "  AR      $$@"
	$$(Q)rm -f $$@
	$$(Q)$$(AR) rcs $$@ $$^

$$(LIB_$(1)_OBJS): $(LIBS_DIR)/$(1)/obj/%.o: lib/$(1)/src/%.cpp
	$$(Q)mkdir -p $$(@D)
	$$(ECHO) "  CXX     $$<"
	$$(Q)$$(CXX) $$(CXXFLAGS) $$(DEPFLAGS) $$(LIB_$(1)_INC) -c $$< -o $$@

# Un binaire de test par fichier : un plantage ou un sanitizer qui déclenche
# dans un fichier n'emporte pas les autres.
$$(LIB_$(1)_TBINS): $(TESTS_DIR)/libs/$(1)/%: lib/$(1)/tests/%.cpp \
                    $(call lib_archive,$(1)) $(ALL_LIB_ARCHIVES)
	$$(Q)mkdir -p $$(@D)
	$$(ECHO) "  CXXLD   $$@"
	$$(Q)$$(CXX) $$(CXXFLAGS) $$(DEPFLAGS) $$(GTEST_CFLAGS) \
		$$(LIB_$(1)_TESTINC) \
		$$< $(call lib_archive,$(1)) $(ALL_LIB_ARCHIVES) \
		$$(LDFLAGS) $$(GTEST_LIBS) $$(LDLIBS) -o $$@

endef

$(foreach l,$(LIBS),$(eval $(call LIB_RULES,$(l))))

# ============================================================================
#  Applications : apps/<nom>/src/**.cpp -> <nom>
# ============================================================================
#  main.cpp est tenu à l'écart de l'archive lib<nom>-core.a pour que les tests
#  de l'application puissent se lier à son code sans rencontrer deux main().
#  Le binaire, lui, est lié directement depuis les objets : aucun objet n'est
#  écarté par l'éditeur de liens, les initialisations statiques survivent.
# ============================================================================
define APP_RULES

APP_$(1)_SRCS    := $$(call rwildcard,apps/$(1)/src,*.cpp)
APP_$(1)_MAIN    := $$(filter apps/$(1)/src/main.cpp,$$(APP_$(1)_SRCS))
APP_$(1)_CORE    := $$(filter-out $$(APP_$(1)_MAIN),$$(APP_$(1)_SRCS))
APP_$(1)_OBJS    := $$(patsubst apps/$(1)/src/%.cpp,$(APPS_DIR)/$(1)/obj/%.o,$$(APP_$(1)_SRCS))
APP_$(1)_COREOBJ := $$(patsubst apps/$(1)/src/%.cpp,$(APPS_DIR)/$(1)/obj/%.o,$$(APP_$(1)_CORE))
APP_$(1)_INC     := $$(addprefix -I,$$(wildcard apps/$(1)/include)) -Iapps/$(1)/src
APP_$(1)_TESTS   := $$(call rwildcard,apps/$(1)/tests,*.cpp)
APP_$(1)_TBINS   := $$(patsubst apps/$(1)/tests/%.cpp,$(TESTS_DIR)/apps/$(1)/%,$$(APP_$(1)_TESTS))
APP_$(1)_TESTINC := -Iapps/$(1)/tests $$(APP_$(1)_INC) $(ALL_LIB_INCLUDES)

# L'archive n'existe que si l'application a des sources en plus de main.cpp.
ifeq ($$(strip $$(APP_$(1)_CORE)),)
  APP_$(1)_ARCHIVE :=
else
  APP_$(1)_ARCHIVE := $(call app_archive,$(1))
endif

$(call app_binary,$(1)): $$(APP_$(1)_OBJS) $(ALL_LIB_ARCHIVES)
	$$(Q)mkdir -p $$(@D)
	$$(ECHO) "  LD      $$@"
	$$(Q)$$(CXX) $$(APP_$(1)_OBJS) $(ALL_LIB_ARCHIVES) \
		$$(LDFLAGS) $$(LDLIBS) -o $$@

$(call app_archive,$(1)): $$(APP_$(1)_COREOBJ)
	$$(Q)mkdir -p $$(@D)
	$$(ECHO) "  AR      $$@"
	$$(Q)rm -f $$@
	$$(Q)$$(AR) rcs $$@ $$^

$$(APP_$(1)_OBJS): $(APPS_DIR)/$(1)/obj/%.o: apps/$(1)/src/%.cpp
	$$(Q)mkdir -p $$(@D)
	$$(ECHO) "  CXX     $$<"
	$$(Q)$$(CXX) $$(CXXFLAGS) $$(DEPFLAGS) $$(APP_$(1)_INC) \
		$(ALL_LIB_INCLUDES) -c $$< -o $$@

$$(APP_$(1)_TBINS): $(TESTS_DIR)/apps/$(1)/%: apps/$(1)/tests/%.cpp \
                    $$(APP_$(1)_ARCHIVE) $(ALL_LIB_ARCHIVES)
	$$(Q)mkdir -p $$(@D)
	$$(ECHO) "  CXXLD   $$@"
	$$(Q)$$(CXX) $$(CXXFLAGS) $$(DEPFLAGS) $$(GTEST_CFLAGS) \
		$$(APP_$(1)_TESTINC) \
		$$< $$(APP_$(1)_ARCHIVE) $(ALL_LIB_ARCHIVES) \
		$$(LDFLAGS) $$(GTEST_LIBS) $$(LDLIBS) -o $$@

endef

$(foreach a,$(APPS),$(eval $(call APP_RULES,$(a))))

# ----------------------------------------------------------------------------
#  Agrégats
# ----------------------------------------------------------------------------
ALL_APP_BINS   := $(foreach a,$(APPS),$(if $(APP_$(a)_MAIN),$(call app_binary,$(a))))
ALL_LIB_TBINS  := $(foreach l,$(LIBS),$(LIB_$(l)_TBINS))
ALL_APP_TBINS  := $(foreach a,$(APPS),$(APP_$(a)_TBINS))
ALL_TEST_BINS  := $(ALL_LIB_TBINS) $(ALL_APP_TBINS)

# Applications sans main.cpp : signalées plutôt que silencieusement ignorées.
APPS_NO_MAIN := $(foreach a,$(APPS),$(if $(APP_$(a)_MAIN),,$(a)))

# ============================================================================
#  Cibles
# ============================================================================
.PHONY: help info list list-libs list-apps list-tests \
        build-lib build-libs rebuild-lib rebuild-libs \
        build-app build-apps rebuild-app rebuild-apps run-app \
        build-all rebuild-all \
        build-tests build-lib-tests build-app-tests \
        run-tests run-lib-tests run-app-tests check \
        require-name check-gtest compdb clean cleanup distclean

# ----------------------------------------------------------------------------
help:
	@echo "Usage : make <cible> [NAME=<nom>] [MODE=release|debug] [ARGS='...']"
	@echo ""
	@echo "Options :"
	@echo "  MODE=release|debug   release par défaut ; debug ajoute -g -O0 et des sanitizers"
	@echo "  SAN=<liste>|none     sanitizers (défaut : address,undefined en debug)"
	@echo "                       ex. make run-tests MODE=debug SAN=thread"
	@echo "  V=1                  afficher les commandes de compilation"
	@echo "  NAME=<nom>           bibliothèque ou application visée"
	@echo "  ARGS='...'           arguments passés au binaire exécuté"
	@echo ""
	@echo "Information :"
	@echo "  info                            Afficher la configuration courante"
	@echo "  list                            Lister bibliothèques, applications et tests"
	@echo "  list-libs / list-apps / list-tests"
	@echo ""
	@echo "Bibliothèques :"
	@echo "  build-lib     NAME=<lib>        Compiler une bibliothèque"
	@echo "  build-libs                      Compiler toutes les bibliothèques"
	@echo "  rebuild-lib   NAME=<lib>        Recompiler une bibliothèque de zéro"
	@echo "  rebuild-libs                    Recompiler toutes les bibliothèques"
	@echo ""
	@echo "Applications :"
	@echo "  build-app     NAME=<app>        Compiler une application"
	@echo "  build-apps                      Compiler toutes les applications"
	@echo "  rebuild-app   NAME=<app>        Recompiler une application de zéro"
	@echo "  rebuild-apps                    Recompiler toutes les applications"
	@echo "  run-app       NAME=<app>        Exécuter une application"
	@echo ""
	@echo "Tout :"
	@echo "  build-all / rebuild-all         Bibliothèques + applications"
	@echo ""
	@echo "Tests (GoogleTest, un binaire par fichier de test) :"
	@echo "  build-tests     [NAME=<nom>]    Compiler les tests (tous, ou ceux de NAME)"
	@echo "  run-tests       [NAME=<nom>]    Compiler puis exécuter les tests + rapport"
	@echo "  build-lib-tests / build-app-tests"
	@echo "  run-lib-tests   / run-app-tests"
	@echo "  check                           Alias de run-tests"
	@echo ""
	@echo "Outillage :"
	@echo "  compdb                          Générer compile_commands.json (clangd)"
	@echo ""
	@echo "Nettoyage :"
	@echo "  clean                           Supprimer les .o/.d/.a du mode courant"
	@echo "  cleanup | distclean             Supprimer tout le dossier build/"

info:
	@echo "MODE       : $(MODE)"
	@echo "SAN        : $(SAN)"
	@echo "CXX        : $(CXX) ($(shell $(CXX) -dumpversion 2>/dev/null))"
	@echo "CXXFLAGS   : $(CXXFLAGS)"
	@echo "LDFLAGS    : $(LDFLAGS)"
	@echo "GTEST_LIBS : $(GTEST_LIBS)"
	@echo "Sortie     : $(OUT_DIR)/"

# ----------------------------------------------------------------------------
list: list-libs list-apps list-tests

list-libs:
	@echo "Bibliothèques ($(words $(LIBS))) :"
	@$(foreach l,$(LIBS),\
	  printf '  - %-16s %3d source(s), %3d test(s)\n' \
	    '$(l)' $(words $(LIB_$(l)_SRCS)) $(words $(LIB_$(l)_TESTS));)

list-apps:
	@echo "Applications ($(words $(APPS))) :"
	@$(foreach a,$(APPS),\
	  printf '  - %-16s %3d source(s), %3d test(s)%s\n' \
	    '$(a)' $(words $(APP_$(a)_SRCS)) $(words $(APP_$(a)_TESTS)) \
	    '$(if $(APP_$(a)_MAIN),,   [pas de src/main.cpp]'')';)

list-tests:
	@echo "Tests ($(words $(ALL_TEST_BINS))) :"
	@$(foreach t,$(ALL_TEST_BINS),echo '  - $(t)';)
	@test -n "$(strip $(ALL_TEST_BINS))" || echo "  (aucun)"

# ----------------------------------------------------------------------------
#  Validation de NAME
# ----------------------------------------------------------------------------
require-name:
	@if [ -z "$(strip $(NAME))" ]; then \
	  echo "Erreur : NAME=<nom> requis."; exit 1; fi

# ----------------------------------------------------------------------------
#  Bibliothèques
# ----------------------------------------------------------------------------
build-lib: require-name
	@if ! echo ' $(LIBS) ' | grep -q ' $(NAME) '; then \
	  echo "Erreur : bibliothèque '$(NAME)' introuvable (voir « make list-libs »)."; \
	  exit 1; fi
	@$(MAKE) --no-print-directory MODE=$(MODE) SAN=$(SAN) V=$(V) \
	  $(call lib_archive,$(NAME))

build-libs: $(ALL_LIB_ARCHIVES)
	@echo "Bibliothèques à jour (MODE=$(MODE))."

rebuild-lib: require-name
	@if ! echo ' $(LIBS) ' | grep -q ' $(NAME) '; then \
	  echo "Erreur : bibliothèque '$(NAME)' introuvable (voir « make list-libs »)."; \
	  exit 1; fi
	@rm -rf $(LIBS_DIR)/$(NAME)
	@$(MAKE) --no-print-directory MODE=$(MODE) SAN=$(SAN) V=$(V) build-lib NAME=$(NAME)

rebuild-libs:
	@rm -rf $(LIBS_DIR)
	@$(MAKE) --no-print-directory MODE=$(MODE) SAN=$(SAN) V=$(V) build-libs

# ----------------------------------------------------------------------------
#  Applications
# ----------------------------------------------------------------------------
build-app: require-name
	@if ! echo ' $(APPS) ' | grep -q ' $(NAME) '; then \
	  echo "Erreur : application '$(NAME)' introuvable (voir « make list-apps »)."; \
	  exit 1; fi
	@if echo ' $(APPS_NO_MAIN) ' | grep -q ' $(NAME) '; then \
	  echo "Erreur : apps/$(NAME)/src/main.cpp est absent, aucun binaire à produire."; \
	  exit 1; fi
	@$(MAKE) --no-print-directory MODE=$(MODE) SAN=$(SAN) V=$(V) \
	  $(call app_binary,$(NAME))

build-apps: $(ALL_APP_BINS)
	@echo "Applications à jour (MODE=$(MODE))."
	@test -z "$(strip $(APPS_NO_MAIN))" || \
	  echo "Ignorées (pas de src/main.cpp) : $(APPS_NO_MAIN)"

rebuild-app: require-name
	@if ! echo ' $(APPS) ' | grep -q ' $(NAME) '; then \
	  echo "Erreur : application '$(NAME)' introuvable (voir « make list-apps »)."; \
	  exit 1; fi
	@rm -rf $(APPS_DIR)/$(NAME)
	@$(MAKE) --no-print-directory MODE=$(MODE) SAN=$(SAN) V=$(V) build-app NAME=$(NAME)

rebuild-apps:
	@rm -rf $(APPS_DIR)
	@$(MAKE) --no-print-directory MODE=$(MODE) SAN=$(SAN) V=$(V) build-apps

run-app: require-name
	@$(MAKE) --no-print-directory MODE=$(MODE) SAN=$(SAN) V=$(V) build-app NAME=$(NAME)
	@echo "--- Exécution de $(NAME) ($(MODE)) ---"
	@$(call app_binary,$(NAME)) $(ARGS)

# ----------------------------------------------------------------------------
#  Tout
# ----------------------------------------------------------------------------
build-all: build-libs build-apps

rebuild-all:
	@rm -rf $(OUT_DIR)
	@$(MAKE) --no-print-directory MODE=$(MODE) SAN=$(SAN) V=$(V) build-all

# ----------------------------------------------------------------------------
#  Tests
# ----------------------------------------------------------------------------
check-gtest:
	@printf '#include <gtest/gtest.h>\nint main(){return 0;}\n' > /tmp/.gtest_probe_$$$$.cpp; \
	if ! $(CXX) $(CXXSTD) $(GTEST_CFLAGS) /tmp/.gtest_probe_$$$$.cpp \
	     $(GTEST_LIBS) -pthread -o /dev/null 2>/dev/null; then \
	  rm -f /tmp/.gtest_probe_$$$$.cpp; \
	  echo "Erreur : GoogleTest est introuvable ou inutilisable."; \
	  echo "         Debian/Ubuntu : sudo apt install libgtest-dev"; \
	  echo "         Fedora        : sudo dnf install gtest-devel"; \
	  exit 1; \
	fi; \
	rm -f /tmp/.gtest_probe_$$$$.cpp

# NAME est optionnel : s'il est fourni, seuls les tests de cette
# bibliothèque ou de cette application sont concernés.
TEST_SELECTION := $(if $(strip $(NAME)),\
                    $(LIB_$(strip $(NAME))_TBINS) $(APP_$(strip $(NAME))_TBINS),\
                    $(ALL_TEST_BINS))

build-tests: check-gtest
	@if [ -n "$(strip $(NAME))" ] && [ -z "$(strip $(TEST_SELECTION))" ]; then \
	  echo "Erreur : aucun test pour '$(NAME)' (voir « make list-tests »)."; exit 1; fi
	@$(MAKE) --no-print-directory MODE=$(MODE) SAN=$(SAN) V=$(V) $(TEST_SELECTION)
	@echo "Tests compilés : $(words $(TEST_SELECTION)) binaire(s) (MODE=$(MODE))."

build-lib-tests: check-gtest
	@$(MAKE) --no-print-directory MODE=$(MODE) SAN=$(SAN) V=$(V) $(ALL_LIB_TBINS)

build-app-tests: check-gtest
	@$(MAKE) --no-print-directory MODE=$(MODE) SAN=$(SAN) V=$(V) $(ALL_APP_TBINS)

# Exécute une liste de binaires de test et produit un rapport.
#   $(call run_test_list,<binaires>,<intitulé>)
define run_test_list
	@set -e; \
	bins="$(strip $(1))"; \
	if [ -z "$$bins" ]; then \
	  echo "Aucun test $(2) à exécuter."; exit 0; \
	fi; \
	pass=0; fail=0; failed=""; \
	for t in $$bins; do \
	  echo "=== $$t ==="; \
	  if "$$t" $(ARGS); then \
	    pass=$$((pass+1)); \
	  else \
	    fail=$$((fail+1)); failed="$$failed $$t"; \
	  fi; \
	  echo ""; \
	done; \
	echo "=========================================="; \
	echo "        RAPPORT DE TESTS ($(MODE))"; \
	echo "=========================================="; \
	echo "Binaires : $$((pass+fail))"; \
	echo "Réussis  : $$pass"; \
	echo "Échoués  : $$fail"; \
	if [ $$fail -gt 0 ]; then \
	  echo "En échec :"; \
	  for t in $$failed; do echo "  - $$t"; done; \
	  exit 1; \
	fi
endef

run-tests:
	@$(MAKE) --no-print-directory MODE=$(MODE) SAN=$(SAN) V=$(V) \
	  build-tests NAME=$(NAME)
	$(call run_test_list,$(TEST_SELECTION),)

run-lib-tests:
	@$(MAKE) --no-print-directory MODE=$(MODE) SAN=$(SAN) V=$(V) build-lib-tests
	$(call run_test_list,$(ALL_LIB_TBINS),de bibliothèque)

run-app-tests:
	@$(MAKE) --no-print-directory MODE=$(MODE) SAN=$(SAN) V=$(V) build-app-tests
	$(call run_test_list,$(ALL_APP_TBINS),d'application)

check: run-tests

# ----------------------------------------------------------------------------
#  compile_commands.json (pour clangd / clang-tidy)
# ----------------------------------------------------------------------------
COMPDB := compile_commands.json

#  Une entrée par fichier source :
#    $(call compdb_entry,<source>,<drapeaux de compilation>)
#  $(file ...) s'expanse en chaîne vide : contrairement à des lignes shell,
#  les $(foreach) ci-dessous ne se retrouvent donc pas concaténés sur une
#  seule et même ligne de recette.
compdb_entry = $(file >>$(COMPDB).tmp,  {"directory": "$(CURDIR)"$(COMMA) "file": "$(CURDIR)/$(1)"$(COMMA) "command": "$(2) -c $(CURDIR)/$(1)"}$(COMMA))

compdb:
	$(file >$(COMPDB).tmp,)
	@$(foreach l,$(LIBS),\
	   $(foreach s,$(LIB_$(l)_SRCS),\
	     $(call compdb_entry,$(s),$(CXX) $(CXXFLAGS) $(LIB_$(l)_INC)))\
	   $(foreach s,$(LIB_$(l)_TESTS),\
	     $(call compdb_entry,$(s),$(CXX) $(CXXFLAGS) $(GTEST_CFLAGS) $(LIB_$(l)_TESTINC))))
	@$(foreach a,$(APPS),\
	   $(foreach s,$(APP_$(a)_SRCS),\
	     $(call compdb_entry,$(s),$(CXX) $(CXXFLAGS) $(APP_$(a)_INC) $(ALL_LIB_INCLUDES)))\
	   $(foreach s,$(APP_$(a)_TESTS),\
	     $(call compdb_entry,$(s),$(CXX) $(CXXFLAGS) $(GTEST_CFLAGS) $(APP_$(a)_TESTINC))))
	@{ echo '['; sed -e '/^$$/d' -e '$$ s/,$$//' $(COMPDB).tmp; echo ']'; } > $(COMPDB)
	@rm -f $(COMPDB).tmp
	@echo "$(COMPDB) généré ($$(grep -c '"file"' $(COMPDB)) entrée(s))."

# ----------------------------------------------------------------------------
#  Nettoyage
# ----------------------------------------------------------------------------
clean:
	@find $(OUT_DIR) -type f \( -name '*.o' -o -name '*.d' -o -name '*.a' \) \
		-delete 2>/dev/null || true
	@echo "Fichiers intermédiaires de $(OUT_DIR)/ supprimés (binaires conservés)."

cleanup distclean:
	@rm -rf $(BUILD_DIR) $(COMPDB) $(COMPDB).tmp
	@echo "Dossier $(BUILD_DIR)/ supprimé."

# ----------------------------------------------------------------------------
#  Dépendances auto-générées (-MMD -MP)
# ----------------------------------------------------------------------------
-include $(call rwildcard,$(OUT_DIR),*.d)
