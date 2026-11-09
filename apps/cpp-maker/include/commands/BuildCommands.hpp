/* @file commands/BuildCommands.hpp
 *
 *   build [mode…] [cible…]     compiler (incrémental, parallèle)
 *   run   [mode] [app] [-- args]   compiler puis exécuter une application
 *   test  [mode…] [cible…]     compiler et lancer les tests
 *   check [cible…] [--modes]   tests sur plusieurs modes (debug, asan, ubsan…)
 *   clean [mode] [cible…]      supprimer les artefacts
 *   size  [mode] [cible…]      tailles text/data/bss des binaires (ELF)
 */
#pragma once

#include "commands/Command.hpp"

namespace commands {

class BuildCommand : public Command {
public:
    std::string GetName() const override { return "build"; }
    std::string GetDescription() const override { return "Compiler le projet ou des cibles"; }
    std::string GetUsage() const override { return "[mode…] [cible…] [options]"; }
    std::string GetDetails() const override;
    std::vector<std::string> GetExamples() const override {
        return {"cpp-maker build                      # mode par défaut, cible du dossier ou tout",
                "cpp-maker build release              # mode release",
                "cpp-maker build debug,asan core.log  # deux modes, un module et ses dépendances",
                "cpp-maker build -t all -j 8          # toutes les toolchains du projet",
                "cpp-maker build --sanitize address,undefined",
                "cpp-maker build --backend ninja"};
    }
    std::vector<std::string> GetAliases() const override { return {"b"}; }
    Category GetCategory() const override { return Category::Build; }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;
};

class RunCommand : public Command {
public:
    std::string GetName() const override { return "run"; }
    std::string GetDescription() const override { return "Compiler puis exécuter une application"; }
    std::string GetUsage() const override { return "[mode] [application] [options] [-- arguments]"; }
    std::string GetDetails() const override {
        return "Sans application : celle du dossier courant, sinon l'unique application du projet.\n"
               "Les arguments après « -- » remplacent [run].args de application.toml.\n"
               "Mode valgrind : « cpp-maker run valgrind ». Cible croisée : via qemu/wine s'ils existent.";
    }
    std::vector<std::string> GetExamples() const override {
        return {"cpp-maker run", "cpp-maker run release hello -- --port 8080", "cpp-maker run valgrind",
                "cpp-maker run --sanitize address"};
    }
    std::vector<std::string> GetAliases() const override { return {"r"}; }
    Category GetCategory() const override { return Category::Build; }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;
};

class TestCommand : public Command {
public:
    std::string GetName() const override { return "test"; }
    std::string GetDescription() const override { return "Compiler et exécuter les tests"; }
    std::string GetUsage() const override { return "[mode…] [cible…] [options] [-- arguments]"; }
    std::string GetDetails() const override {
        return "Chaque cible ayant un dossier tests/ produit un exécutable <cible>-tests.\n"
               "Code 0 : réussite ; 77 : tous les tests ignorés ; autre : échec.";
    }
    std::vector<std::string> GetExamples() const override {
        return {"cpp-maker test", "cpp-maker test release core.log --filter add",
                "cpp-maker test --sanitize all", "cpp-maker test --junit rapport.xml"};
    }
    std::vector<std::string> GetAliases() const override { return {"t"}; }
    Category GetCategory() const override { return Category::Build; }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;
};

class CheckCommand : public Command {
public:
    std::string GetName() const override { return "check"; }
    std::string GetDescription() const override { return "Tests sur plusieurs modes (défaut : debug, asan, ubsan)"; }
    std::string GetUsage() const override { return "[mode,…] [cible…] [options]"; }
    std::vector<std::string> GetExamples() const override {
        return {"cpp-maker check", "cpp-maker check debug,tsan --junit ci.xml"};
    }
    Category GetCategory() const override { return Category::Build; }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;
};

class CleanCommand : public Command {
public:
    std::string GetName() const override { return "clean"; }
    std::string GetDescription() const override { return "Supprimer les artefacts de compilation"; }
    std::string GetUsage() const override { return "[mode] [cible…] [-t tc] [--all]"; }
    std::string GetDetails() const override {
        return "Sans argument : tout le dossier build/. Avec un mode et/ou -t : build/<tc>/<mode>.\n"
               "Avec des cibles : leurs objets, artefacts et tests dans tous les modes.\n"
               "--all supprime aussi dist/.";
    }
    Category GetCategory() const override { return Category::Build; }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;
};

class SizeCommand : public Command {
public:
    std::string GetName() const override { return "size"; }
    std::string GetDescription() const override { return "Tailles text/data/bss des binaires produits"; }
    std::string GetUsage() const override { return "[mode] [cible…] [-t tc] [-v]"; }
    Category GetCategory() const override { return Category::Build; }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;
};

} // namespace commands
