/* @file model/Scaffold.hpp
 *
 * Création de projets, layers, modules et applications : fichiers TOML
 * commentés et sources d'exemple (C ou C++) compilables immédiatement.
 */
#pragma once

#include "model/Editor.hpp"

#include <string>
#include <vector>

namespace scaffold {

struct ProjectOptions {
    std::string name;
    fs::path dir;                       ///< dossier à créer (défaut : ./<name>)
    std::string language = "c++";
    std::string cxx_standard = "c++23";
    std::string c_standard = "c17";
    std::string layout = "full";        ///< full | app | lib | empty
    std::string layer = "core";
    std::string module;                 ///< défaut : nom du projet
    std::string app;                    ///< défaut : nom du projet
    bool in_place = false;              ///< init : dossier existant
};

struct ModuleOptions {
    std::string layer, name;
    std::string type = "static";        ///< static | shared | header-only
    std::string language;               ///< vide = langage du projet
    std::vector<std::string> deps;
    std::string description;
    bool sources = true;
};

struct AppOptions {
    std::string name;
    std::string dir;                    ///< dossier d'applications (défaut : le premier de [paths].applications)
    std::string language;
    std::vector<std::string> deps;
    std::string description;
    bool sources = true;
};

struct LayerOptions {
    std::string name;
    std::string description;
    std::vector<std::string> deps;
    std::string dir;                    ///< dossier de layers (défaut : le premier de [paths].layers)
};

/// Crée un projet complet ; renvoie sa racine.
fs::path create_project(const ProjectOptions& o);

void add_layer(model::Transaction& tx, const model::Workspace& ws, const LayerOptions& o);
void add_module(model::Transaction& tx, const model::Workspace& ws, const ModuleOptions& o);
void add_app(model::Transaction& tx, const model::Workspace& ws, const AppOptions& o);

/// Textes des fichiers de configuration (utilisés aussi par la TUI).
std::string project_toml(const ProjectOptions& o, const std::vector<std::string>& layer_order);
std::string layer_toml(const LayerOptions& o);
std::string module_toml(const ModuleOptions& o, const std::string& language);
std::string app_toml(const AppOptions& o, const std::string& language);

} // namespace scaffold
