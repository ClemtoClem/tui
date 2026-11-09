/* @file commands/DeliverCommands.hpp
 *
 *   package [mode] [cible…] [--format …]    archives dans dist/
 *   install [mode] [cible…] [--prefix …]    installation locale
 *   deploy  <nom> | deploy list             déploiement ([deploy.<nom>])
 */
#pragma once

#include "commands/Command.hpp"

namespace commands {

class PackageCommand : public Command {
public:
    std::string GetName() const override { return "package"; }
    std::string GetDescription() const override { return "Produire les paquets (tar.gz, tar.xz, zip, deb)"; }
    std::string GetUsage() const override { return "[mode] [cible…] [options]"; }
    std::string GetDetails() const override {
        return "Mode par défaut : release. Contenu : bin/, lib/, include/, lib/pkgconfig/,\n"
               "share/doc/<projet>/. Les binaires sont allégés (strip) sauf si le mode a -g.";
    }
    std::vector<std::string> GetExamples() const override {
        return {"cpp-maker package", "cpp-maker package --format tar.xz,deb", "cpp-maker package -t armhf"};
    }
    Category GetCategory() const override { return Category::Delivery; }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;
};

class InstallCommand : public Command {
public:
    std::string GetName() const override { return "install"; }
    std::string GetDescription() const override { return "Installer les artefacts (bin, lib, include, pkg-config)"; }
    std::string GetUsage() const override { return "[mode] [cible…] [--prefix P] [--destdir D]"; }
    std::vector<std::string> GetExamples() const override {
        return {"cpp-maker install --prefix ~/.local", "cpp-maker install --destdir /tmp/stage --prefix /usr"};
    }
    Category GetCategory() const override { return Category::Delivery; }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;
};

class DeployCommand : public Command {
public:
    std::string GetName() const override { return "deploy"; }
    std::string GetDescription() const override { return "Déployer vers une cible [deploy.<nom>]"; }
    std::string GetUsage() const override { return "<nom>|list [-n] [-v]"; }
    std::vector<std::string> GetExamples() const override {
        return {"cpp-maker deploy list", "cpp-maker deploy rpi --dry-run", "cpp-maker deploy rpi"};
    }
    Category GetCategory() const override { return Category::Delivery; }
    void ConfigureArgs(args::Arguments& a) override;
    int Run(Context& ctx) override;
};

} // namespace commands
