/* @file panels/project/AppsEditor.cpp */

#include "panels/project/Editors.hpp"
#include "panels/project/Ui.hpp"
#include "project/PlanApplier.hpp"
#include "project/Presets.hpp"

#include <algorithm>
#include <cstdlib>
#include <filesystem>

namespace yocto {

namespace fs = std::filesystem;
using tui::LayoutParams;

namespace {

const std::vector<std::string> kKindLabels = {
    "Scripts / fichiers (installés tels quels)",
    "Fichiers C/C++ (un exécutable par .c)",
    "Projet Makefile (oe_runmake)",
    "Projet CMake (inherit cmake)",
};

const std::vector<std::string> kSourceLabels = {
    "Inline (contenu édité ici)",
    "Répertoire de l'hôte (copié)",
    "Dépôt git (fetcher BitBake)",
};

std::string expand_home(const std::string& p) {
    if (p.rfind("~/", 0) == 0) {
        const char* home = std::getenv("HOME");
        if (home) return std::string(home) + p.substr(1);
    }
    return p;
}

bool is_compilable(const std::string& n) {
    const auto ext = fs::path(n).extension().string();
    return ext == ".c" || ext == ".cpp" || ext == ".cc";
}

bool is_executable(const fs::path& p) {
    std::error_code ec;
    return (fs::status(p, ec).permissions() & fs::perms::owner_exec) != fs::perms::none;
}

} // namespace

AppsEditor::AppsEditor(ProjectSpec& spec, std::function<void()> on_changed)
    : EditorBase(spec, std::move(on_changed)) {
    using namespace ui;

    // --- Liste des applications ---
    apps_ = list();
    apps_->set_on_select([this](size_t i) { load_app(i); });

    auto add = button("Ajouter", [this] {
        std::string name = "mon-appli";
        for (int n = 2; std::any_of(spec_.apps.begin(), spec_.apps.end(),
                                    [&](const CustomApp& a) { return a.name == name; }); ++n) {
            name = "mon-appli-" + std::to_string(n);
        }
        spec_.apps.push_back(app_template(AppKind::CFiles, name));
        refresh_apps(spec_.apps.size() - 1);
        changed();
        status("Application « " + name + " » ajoutée : choisissez son type et ses sources.");
    });
    auto dup = button("Dupliquer", [this] {
        auto* a = current_app();
        if (!a) return;
        CustomApp copy = *a;
        copy.name += "-copie";
        spec_.apps.push_back(copy);
        refresh_apps(spec_.apps.size() - 1);
        changed();
    });
    auto remove = button("Supprimer", [this] {
        const size_t i = apps_->selected_index();
        if (i >= spec_.apps.size()) return;
        const std::string name = spec_.apps[i].name;
        spec_.apps.erase(spec_.apps.begin() + static_cast<long>(i));
        refresh_apps(i > 0 ? i - 1 : 0);
        changed();
        status("Application « " + name + " » retirée du projet.");
    });

    auto left = std::make_shared<tui::Vertical>();
    left->add_child(apps_, LayoutParams::stretch(1));
    left->add_child(row({add, dup}), LayoutParams::fixed(1));
    left->add_child(row({remove}), LayoutParams::fixed(1));
    left->add_child(muted("✓ dans l'image  ○ recette seule  ✗ désactivée", true), LayoutParams::fixed(2));

    // --- Recette ---
    name_     = input("mon-appli");
    version_  = input("1.0");
    summary_  = input("Description courte");
    license_  = input("MIT");
    chksum_   = input("vide pour MIT / CLOSED");
    kind_ = std::make_shared<tui::RadioSet>();
    kind_->set_options(kKindLabels);
    source_ = std::make_shared<tui::RadioSet>();
    source_->set_options(kSourceLabels);
    source_dir_ = input("/home/moi/projets/mon-appli");
    git_url_    = input("https://github.com/moi/mon-appli.git");
    git_branch_ = input("main");
    git_srcrev_ = input("hash du commit ou ${AUTOREV}");
    depends_    = input("ex : libgpiod zlib");
    rdepends_   = input("ex : bash python3-core");
    extra_conf_ = input("EXTRA_OEMAKE / EXTRA_OECMAKE");
    autostart_  = input("nom du script de /etc/init.d");
    autostart_params_ = input("defaults 90");
    enabled_  = std::make_shared<tui::Checkbox>("Générer la recette");
    in_image_ = std::make_shared<tui::Checkbox>("Installer dans l'image");

    bind(name_, [this](const std::string& v) {
        if (auto* a = current_app()) { a->name = v; refresh_apps(apps_->selected_index()); }
    });
    bind(version_, [this](const std::string& v) { if (auto* a = current_app()) a->version = v; });
    bind(summary_, [this](const std::string& v) { if (auto* a = current_app()) a->summary = v; });
    bind(license_, [this](const std::string& v) { if (auto* a = current_app()) a->license = v; });
    bind(chksum_, [this](const std::string& v) { if (auto* a = current_app()) a->lic_files_chksum = v; });
    bind(kind_, [this](size_t i) {
        if (auto* a = current_app()) { a->kind = static_cast<AppKind>(i); refresh_apps(apps_->selected_index()); }
    });
    bind(source_, [this](size_t i) {
        if (auto* a = current_app()) { a->source = static_cast<SourceKind>(i); update_source_widgets(); }
    });
    bind(source_dir_, [this](const std::string& v) {
        if (auto* a = current_app()) { a->source_dir = expand_home(v); update_source_widgets(); }
    });
    bind(git_url_, [this](const std::string& v) { if (auto* a = current_app()) a->git_url = v; });
    bind(git_branch_, [this](const std::string& v) { if (auto* a = current_app()) a->git_branch = v; });
    bind(git_srcrev_, [this](const std::string& v) { if (auto* a = current_app()) a->git_srcrev = v; });
    bind(depends_, [this](const std::string& v) { if (auto* a = current_app()) a->depends = v; });
    bind(rdepends_, [this](const std::string& v) { if (auto* a = current_app()) a->rdepends = v; });
    bind(extra_conf_, [this](const std::string& v) { if (auto* a = current_app()) a->extra_conf = v; });
    bind(autostart_, [this](const std::string& v) { if (auto* a = current_app()) a->autostart = v; });
    bind(autostart_params_, [this](const std::string& v) { if (auto* a = current_app()) a->autostart_params = v; });
    bind(enabled_, [this](bool v) {
        if (auto* a = current_app()) { a->enabled = v; refresh_apps(apps_->selected_index()); }
    });
    bind(in_image_, [this](bool v) {
        if (auto* a = current_app()) { a->in_image = v; refresh_apps(apps_->selected_index()); }
    });

    auto [scroll, form] = scroll_form();
    field(*form, "Nom de recette (minuscules, chiffres, - + .)", name_);
    field(*form, "Version (PV)", version_);
    field(*form, "Description (SUMMARY)", summary_);
    field(*form, "Licence (MIT, CLOSED, GPL-2.0-only...)", license_);
    field(*form, "LIC_FILES_CHKSUM (requis hors MIT/CLOSED)", chksum_);
    field(*form, "Construction", kind_, 4);
    field(*form, "Sources", source_, 3);
    field(*form, "Répertoire de l'hôte (sources « hôte »)", source_dir_);
    field(*form, "URL git (sources « git »)", git_url_);
    field(*form, "Branche git", git_branch_);
    field(*form, "SRCREV", git_srcrev_);
    field(*form, "DEPENDS (bibliothèques nécessaires à la compilation)", depends_);
    field(*form, "RDEPENDS (paquets nécessaires à l'exécution)", rdepends_);
    field(*form, "Options make / cmake", extra_conf_);
    field(*form, "Démarrage auto : script /etc/init.d/<nom>", autostart_);
    field(*form, "Paramètres update-rc.d", autostart_params_);
    form->add_child(enabled_, LayoutParams::fixed(1));
    form->add_child(in_image_, LayoutParams::fixed(1));

    // --- Fichiers ---
    files_ = list();
    files_->set_on_select([this](size_t i) { load_file(i); });
    file_name_ = input("src/main.c");
    file_dest_ = input("/usr/bin/mon-appli (vide = source seulement)");
    file_mode_ = input("0755");
    file_mode_->set_validator([](char32_t c) { return c >= U'0' && c <= U'7'; });
    content_ = editor();
    content_hint_ = muted("");
    import_path_ = input("chemin d'un fichier ou répertoire de l'hôte");

    bind(file_name_, [this](const std::string& v) {
        if (auto* f = current_file()) { f->name = v; refresh_files(files_->selected_index()); }
    });
    bind(file_dest_, [this](const std::string& v) {
        if (auto* f = current_file()) { f->dest = v; refresh_files(files_->selected_index()); }
    });
    bind(file_mode_, [this](const std::string& v) { if (auto* f = current_file()) f->mode = v; });
    bind(content_, [this](const std::string& v) {
        auto* a = current_app();
        if (auto* f = current_file(); f && a && a->source == SourceKind::Inline) f->content = v;
    });

    auto add_file = button("+ Fichier", [this] {
        auto* a = current_app();
        if (!a) return;
        std::string name = "nouveau.txt";
        for (int n = 2; std::any_of(a->files.begin(), a->files.end(),
                                    [&](const ProjectFile& f) { return f.name == name; }); ++n) {
            name = "nouveau-" + std::to_string(n) + ".txt";
        }
        a->files.push_back(ProjectFile{name, "", "0644", ""});
        refresh_files(a->files.size() - 1);
        changed();
    });
    auto remove_file = button("Retirer", [this] {
        auto* a = current_app();
        const size_t i = files_->selected_index();
        if (!a || i >= a->files.size()) return;
        a->files.erase(a->files.begin() + static_cast<long>(i));
        refresh_files(i > 0 ? i - 1 : 0);
        changed();
    });
    auto import = button("Importer", [this] { import_from_host(); });

    auto right = std::make_shared<tui::Vertical>();
    right->add_child(files_, LayoutParams::fixed(6));
    right->add_child(row({add_file, remove_file}), LayoutParams::fixed(1));
    field(*right, "Chemin dans les sources", file_name_);
    field(*right, "Installé dans la cible en (chemin absolu)", file_dest_);
    field(*right, "Mode", file_mode_);
    right->add_child(muted("Importer depuis l'hôte (fichier, ou répertoire entier) :"), LayoutParams::fixed(1));
    auto import_row = std::make_shared<tui::Horizontal>();
    import_row->add_child(import_path_, LayoutParams::stretch(1));
    import_row->add_child(import, LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    right->add_child(import_row, LayoutParams::fixed(1));
    right->add_child(content_hint_, LayoutParams::fixed(1));
    right->add_child(content_, LayoutParams::stretch(1));

    auto columns = std::make_shared<tui::Horizontal>();
    columns->add_child(box("Applications", left), LayoutParams::fixed(28));
    columns->add_child(box("Recette", scroll), LayoutParams::fixed(50));
    columns->add_child(box("Fichiers de l'application", right), LayoutParams::stretch(1));
    root_ = columns;
    reload();
}

void AppsEditor::reload() { refresh_apps(0); }

CustomApp* AppsEditor::current_app() {
    const size_t i = apps_->selected_index();
    return i < spec_.apps.size() ? &spec_.apps[i] : nullptr;
}

ProjectFile* AppsEditor::current_file() {
    auto* a = current_app();
    const size_t i = files_->selected_index();
    return a && i < a->files.size() ? &a->files[i] : nullptr;
}

void AppsEditor::refresh_apps(size_t select) {
    std::vector<tui::ListItem> items;
    for (const auto& a : spec_.apps) {
        const char* icon = !a.enabled ? "✗" : (a.in_image ? "✓" : "○");
        items.push_back(tui::ListItem{a.name.empty() ? "(sans nom)" : a.name, to_string(a.kind), true, icon});
    }
    const bool reselect = apps_->selected_index() != select || items.size() != apps_->items().size();
    apps_->set_items(std::move(items));
    if (reselect) apps_->set_selected_index(select);
    load_app(apps_->selected_index());
}

void AppsEditor::load_app(size_t index) {
    {
        ui::LoadingGuard guard(loading_);
        const CustomApp empty{};
        const CustomApp& a = index < spec_.apps.size() ? spec_.apps[index] : empty;
        auto set = [](const std::shared_ptr<tui::Input>& in, const std::string& v) {
            if (in->text() != v) in->set_text(v);
        };
        set(name_, a.name);
        set(version_, a.version);
        set(summary_, a.summary);
        set(license_, a.license);
        set(chksum_, a.lic_files_chksum);
        kind_->set_selected_index(static_cast<size_t>(a.kind));
        source_->set_selected_index(static_cast<size_t>(a.source));
        set(source_dir_, a.source_dir);
        set(git_url_, a.git_url);
        set(git_branch_, a.git_branch);
        set(git_srcrev_, a.git_srcrev);
        set(depends_, a.depends);
        set(rdepends_, a.rdepends);
        set(extra_conf_, a.extra_conf);
        set(autostart_, a.autostart);
        set(autostart_params_, a.autostart_params);
        enabled_->set_checked(a.enabled);
        in_image_->set_checked(a.in_image);
    }
    refresh_files(0);
}

void AppsEditor::refresh_files(size_t select) {
    std::vector<tui::ListItem> items;
    if (auto* a = current_app()) {
        for (const auto& f : a->files) {
            std::string detail = f.dest.empty() ? "source" : "→ " + f.dest;
            if (a->kind == AppKind::CFiles && is_compilable(f.name) && !f.dest.empty()) detail = "compilé " + detail;
            items.push_back(tui::ListItem{f.name.empty() ? "(sans nom)" : f.name, detail, true, "·"});
        }
    }
    const bool reselect = files_->selected_index() != select || items.size() != files_->items().size();
    files_->set_items(std::move(items));
    if (reselect) files_->set_selected_index(select);
    load_file(files_->selected_index());
}

void AppsEditor::load_file(size_t index) {
    ui::LoadingGuard guard(loading_);
    auto* a = current_app();
    const ProjectFile empty{};
    const ProjectFile& f = a && index < a->files.size() ? a->files[index] : empty;
    if (file_name_->text() != f.name) file_name_->set_text(f.name);
    if (file_dest_->text() != f.dest) file_dest_->set_text(f.dest);
    if (file_mode_->text() != f.mode) file_mode_->set_text(f.mode);
    update_source_widgets();
}

void AppsEditor::update_source_widgets() {
    ui::LoadingGuard guard(loading_);
    auto* a = current_app();
    auto* f = current_file();
    if (!a) {
        content_hint_->set_text("Ajoutez une application (bouton « Ajouter »).");
        content_->set_read_only(true);
        content_->set_text("");
        return;
    }
    switch (a->source) {
        case SourceKind::Inline:
            content_hint_->set_text("Contenu (stocké dans la configuration) :");
            content_->set_read_only(false);
            if (f && content_->text() != f->content) content_->set_text(f->content);
            if (!f) content_->set_text("");
            break;
        case SourceKind::HostDir: {
            content_->set_read_only(true);
            if (!f) {
                content_hint_->set_text("Sources copiées depuis " + a->source_dir);
                content_->set_text("");
                break;
            }
            bool exists = false;
            const std::string text = read_file(fs::path(a->source_dir) / f->name, &exists);
            content_hint_->set_text(exists ? "Lecture seule : " + (fs::path(a->source_dir) / f->name).string()
                                           : "Introuvable dans " + a->source_dir);
            content_->set_text(exists ? text : "");
            break;
        }
        case SourceKind::Git:
            content_hint_->set_text("Sources récupérées par BitBake : entrée = fichier à compiler / installer.");
            content_->set_read_only(true);
            content_->set_text("");
            break;
    }
}

void AppsEditor::import_from_host() {
    auto* a = current_app();
    if (!a) return;
    const fs::path path = expand_home(import_path_->text());
    std::error_code ec;
    if (path.empty() || !fs::exists(path, ec)) {
        status("Erreur : chemin introuvable : " + path.string());
        return;
    }

    if (a->source == SourceKind::HostDir) {
        if (!fs::is_directory(path, ec)) {
            status("Erreur : sources « hôte » : indiquez un répertoire");
            return;
        }
        a->source_dir = fs::absolute(path, ec).string();
        { ui::LoadingGuard guard(loading_); source_dir_->set_text(a->source_dir); }
        update_source_widgets();
        changed();
        status("✓ Répertoire source : " + a->source_dir + " (copié dans le layer à l'application)");
        return;
    }
    if (a->source == SourceKind::Git) {
        status("Erreur : sources git : rien à importer (BitBake clone le dépôt)");
        return;
    }

    // Inline : le contenu des fichiers est copié dans la configuration.
    std::vector<std::pair<std::string, fs::path>> found;  // nom relatif, chemin hôte
    if (fs::is_regular_file(path, ec)) {
        found.emplace_back(path.filename().string(), path);
    } else {
        fs::recursive_directory_iterator it(path, fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            const std::string n = it->path().filename().string();
            if (it->is_directory() && (n == ".git" || n == "build")) { it.disable_recursion_pending(); continue; }
            if (it->is_regular_file()) found.emplace_back(fs::relative(it->path(), path, ec).string(), it->path());
        }
    }

    int imported = 0;
    int skipped = 0;
    for (const auto& [rel, host] : found) {
        std::error_code ec2;
        if (fs::file_size(host, ec2) > 256 * 1024) { ++skipped; continue; }
        const std::string content = read_file(host);
        if (content.find('\0') != std::string::npos) { ++skipped; continue; }

        ProjectFile f{rel, "", is_executable(host) ? "0755" : "0644", content};
        const std::string base = fs::path(rel).filename().string();
        if (a->kind == AppKind::Scripts) {
            f.dest = "/usr/bin/" + base;
            f.mode = "0755";
        } else if (a->kind == AppKind::CFiles && is_compilable(rel)) {
            f.dest = "/usr/bin/" + fs::path(rel).stem().string();
            f.mode = "0755";
        }
        auto it = std::find_if(a->files.begin(), a->files.end(), [&](const ProjectFile& x) { return x.name == rel; });
        if (it != a->files.end()) {
            it->content = content;  // conserve destination et mode choisis
        } else {
            a->files.push_back(f);
        }
        ++imported;
    }
    refresh_files(files_->selected_index());
    changed();
    status("✓ " + std::to_string(imported) + " fichier(s) importé(s)" +
           (skipped ? ", " + std::to_string(skipped) + " ignoré(s) (binaires ou > 256 Kio)" : std::string()));
}

} // namespace yocto
