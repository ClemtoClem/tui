/* @file panels/project/YoctoFilesEditor.cpp */

#include "panels/project/Editors.hpp"
#include "panels/project/Ui.hpp"
#include "project/PlanApplier.hpp"

#include <algorithm>
#include <fstream>
#include <set>

namespace yocto {

namespace fs = std::filesystem;
using tui::LayoutParams;

YoctoFilesEditor::YoctoFilesEditor(ProjectSpec& spec, std::function<void()> on_changed)
    : EditorBase(spec, std::move(on_changed)) {
    using namespace ui;

    list_ = list();
    list_->set_on_select([this](size_t) { open_selected(); });
    path_label_ = muted("");
    warn_label_ = muted("", true);
    editor_ = editor();  // Ctrl+S : intercepté globalement (ProjectPanel::handle_save_shortcut)

    auto refresh = button("Rafraîchir", [this] { refresh_list(); });
    auto reload_btn = button("Recharger", [this] { open_selected(); });
    auto save = button("Enregistrer (Ctrl+S)", [this] { save_current(); });

    auto left = std::make_shared<tui::Vertical>();
    left->add_child(list_, LayoutParams::stretch(1));
    left->add_child(row({refresh}), LayoutParams::fixed(1));
    left->add_child(muted("Fichiers réels du lab : configuration du build et contenu du layer "
                          "(après « Générer & appliquer »).", true),
                    LayoutParams::fixed(3));

    auto right = std::make_shared<tui::Vertical>();
    right->add_child(path_label_, LayoutParams::fixed(1));
    right->add_child(warn_label_, LayoutParams::fixed(2));
    right->add_child(row({reload_btn, save}), LayoutParams::fixed(1));
    right->add_child(editor_, LayoutParams::stretch(1));

    auto columns = std::make_shared<tui::Horizontal>();
    columns->add_child(box("Fichiers Yocto sur disque", left), LayoutParams::fixed(44));
    columns->add_child(box("Édition", right), LayoutParams::stretch(1));
    root_ = columns;
}

void YoctoFilesEditor::set_config(const YoctoConfig& cfg) {
    config_ = cfg;
    config_.apply_defaults();
    refresh_list();
}

void YoctoFilesEditor::reload() { refresh_list(); }

void YoctoFilesEditor::refresh_list() {
    const fs::path previous = open_path_;
    paths_.clear();
    std::vector<tui::ListItem> items;
    std::error_code ec;

    auto add = [&](const fs::path& p, const std::string& label, const std::string& detail) {
        const bool exists = fs::exists(p, ec);
        paths_.push_back(p);
        items.push_back(tui::ListItem{label, exists ? detail : "absent", true, exists ? "·" : "○"});
    };
    const fs::path conf = config_.build_dir_path() / "conf";
    add(conf / "local.conf", "local.conf", "configuration du build");
    add(conf / "bblayers.conf", "bblayers.conf", "couches du build");
    add(config_.env_script_path(), "env.sh", "environnement du lab");

    const fs::path layer = config_.layers_dir() / spec_.layer_name;
    if (fs::is_directory(layer, ec)) {
        const auto manifest = read_manifest(layer);
        const std::set<std::string> generated(manifest.begin(), manifest.end());
        std::vector<fs::path> files;
        for (fs::recursive_directory_iterator it(layer, ec), end; !ec && it != end; it.increment(ec)) {
            if (it->is_directory() && it->path().filename() == ".git") { it.disable_recursion_pending(); continue; }
            if (it->is_regular_file() && it->path().filename() != kManifestName) files.push_back(it->path());
        }
        std::sort(files.begin(), files.end());
        for (const auto& f : files) {
            const std::string rel = fs::relative(f, layer, ec).string();
            add(f, spec_.layer_name + "/" + rel, generated.count(rel) ? "généré" : "manuel / devtool");
        }
    }
    list_->set_items(std::move(items));

    // Rouvre le même fichier si possible.
    size_t select = 0;
    for (size_t i = 0; i < paths_.size(); ++i) {
        if (paths_[i] == previous) select = i;
    }
    list_->set_selected_index(select);
    open_selected();
}

void YoctoFilesEditor::open_selected() {
    const size_t i = list_->selected_index();
    if (i >= paths_.size()) return;
    open_path_ = paths_[i];
    bool exists = false;
    open_content_ = read_file(open_path_, &exists);
    editor_->set_text(open_content_);
    editor_->set_read_only(!exists);
    path_label_->set_text(open_path_.string());

    const std::string detail = list_->items()[i].detail;
    if (!exists) {
        warn_label_->set_text("Fichier absent : préparez le lab (F1) ou appliquez le projet (F3).");
        warn_label_->set_foreground(kMuted);
    } else if (detail == "généré") {
        warn_label_->set_text("Fichier généré : vos modifications seront écrasées à la prochaine application. "
                              "Modifiez plutôt le projet.");
        warn_label_->set_foreground(kWarning);
    } else if (open_path_.filename() == "local.conf" || open_path_.filename() == "bblayers.conf") {
        warn_label_->set_text("Le bloc « yocto-tui » est régénéré ; le reste du fichier vous appartient.");
        warn_label_->set_foreground(kMuted);
    } else {
        warn_label_->set_text("");
    }
}

void YoctoFilesEditor::save_current() {
    if (open_path_.empty() || editor_->read_only()) {
        status("Erreur : aucun fichier existant ouvert");
        return;
    }
    const std::string content = editor_->text();
    if (content == open_content_) {
        status("Aucune modification à enregistrer.");
        return;
    }
    std::ofstream out(open_path_, std::ios::binary | std::ios::trunc);
    out << content;
    if (!out) {
        status("Erreur : écriture impossible : " + open_path_.string());
        return;
    }
    open_content_ = content;
    status("✓ Enregistré : " + open_path_.string());
}

} // namespace yocto
