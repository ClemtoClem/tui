#include "model/Sheet.hpp"
#include "widgets/SpreadsheetGrid.hpp"
#include "xml/SpreadsheetXml.hpp"

#include <tui/App.hpp>
#include <tui/terminal/PosixTerminalBackend.hpp>
#include <tui/widget/layout/Stack.hpp>
#include <tui/widget/layout/Vertical.hpp>
#include <tui/widgets/Dialog.hpp>
#include <tui/widgets/Input.hpp>
#include <tui/widgets/Label.hpp>
#include <tui/widgets/ListView.hpp>
#include <tui/widgets/StatusBar.hpp>

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;

// tui::Input ne gere pas Echap (seul Entree est cablee -> on_submit) ;
// la modale Enregistrer/Ouvrir a besoin d'un moyen d'annuler sans
// valider un chemin vide, d'ou ce petit sous-type local.
class PathInput : public tui::Input {
public:
    void set_on_cancel(std::function<void()> fn) { on_cancel_ = std::move(fn); }
    bool on_key(const tui::KeyEvent& e) override {
        if (e.key == tui::Key::Escape) {
            if (on_cancel_) on_cancel_();
            return true;
        }
        return tui::Input::on_key(e);
    }

private:
    std::function<void()> on_cancel_;
};

bool has_xml_extension(const fs::path& p) {
    std::string ext = p.extension().string();
    for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return ext == ".xml";
}

} // namespace

int main() {
    using namespace tui;
    using namespace sheetapp;

    App app(std::make_unique<PosixTerminalBackend>());

    Sheet sheet;
    auto grid = std::make_shared<SpreadsheetGrid>(sheet);

    auto selection_label = std::make_shared<Label>("A1:");
    auto status = std::make_shared<StatusBar>();
    status->set_left(
        "Fleches: naviguer  Entree/F2: editer  Echap: annuler  Suppr: effacer  "
        "Ctrl+S: enregistrer  Ctrl+O: ouvrir  Ctrl+Q: quitter");

    grid->set_on_selection_changed(
        [&](CellPos p, const std::string& raw) { selection_label->set_text(cell_ref(p) + ": " + raw); });

    // --- Modale Enregistrer sous / Ouvrir : champ de chemin + navigateur de fichiers ---

    auto path_input = std::make_shared<PathInput>();
    path_input->set_placeholder("chemin/vers/fichier.xml");

    auto browser_path_label = std::make_shared<Label>("");
    auto browser_list = std::make_shared<ListView>();

    auto dialog_content = std::make_shared<Vertical>();
    dialog_content->add_child(std::make_shared<Label>("Chemin du fichier :"), LayoutParams::fixed(1));
    dialog_content->add_child(path_input, LayoutParams::fixed(1));
    dialog_content->add_child(browser_path_label, LayoutParams::fixed(1));
    dialog_content->add_child(browser_list, LayoutParams::stretch(1));

    auto dialog = std::make_shared<Dialog>();
    dialog->set_content(dialog_content);
    dialog->set_size(Size{64, 20});
    dialog->set_focus_manager(&app.focus_manager());

    enum class DialogMode { Save, Open };
    DialogMode dialog_mode = DialogMode::Save;
    std::string current_path;

    struct BrowserEntry {
        std::string name;
        bool is_dir;
    };
    fs::path browser_dir = fs::current_path();
    std::vector<BrowserEntry> browser_entries;

    auto refresh_browser = [&] {
        browser_entries.clear();
        browser_path_label->set_text(browser_dir.string());

        std::vector<ListItem> items;
        if (browser_dir.has_parent_path() && browser_dir != browser_dir.root_path()) {
            items.push_back(ListItem{"..", "dossier parent", true, "[DIR]"});
            browser_entries.push_back({"..", true});
        }

        std::error_code ec;
        std::vector<fs::directory_entry> dirs, files;
        for (const auto& entry : fs::directory_iterator(browser_dir, fs::directory_options::skip_permission_denied, ec)) {
            if (entry.is_directory()) dirs.push_back(entry);
            else if (has_xml_extension(entry.path()))
                files.push_back(entry);
        }
        auto by_name = [](const fs::directory_entry& a, const fs::directory_entry& b) {
            return a.path().filename().string() < b.path().filename().string();
        };
        std::sort(dirs.begin(), dirs.end(), by_name);
        std::sort(files.begin(), files.end(), by_name);

        for (const auto& d : dirs) {
            std::string name = d.path().filename().string();
            items.push_back(ListItem{name, "", true, "[DIR]"});
            browser_entries.push_back({name, true});
        }
        for (const auto& fentry : files) {
            std::string name = fentry.path().filename().string();
            items.push_back(ListItem{name, "", true, ""});
            browser_entries.push_back({name, false});
        }
        browser_list->set_items(std::move(items));
    };

    auto close_dialog = [&] {
        dialog->hide();
        app.focus_manager().set_focus(grid);
        grid->need_repaint();
    };

    auto perform_action = [&](const std::string& path) {
        std::string error;
        bool ok = (dialog_mode == DialogMode::Save) ? save_xml(sheet, path, error) : load_xml(sheet, path, error);
        if (ok) current_path = path;
        close_dialog();
        status->set_left(ok ? ("Enregistre : " + path) : ("Erreur : " + error));
    };

    path_input->set_on_cancel(close_dialog);
    path_input->set_on_submit(perform_action);

    browser_list->set_on_activate([&](size_t idx) {
        if (idx >= browser_entries.size()) return;
        const BrowserEntry& entry = browser_entries[idx];
        if (entry.is_dir) {
            browser_dir = (entry.name == "..") ? browser_dir.parent_path() : browser_dir / entry.name;
            refresh_browser();
            return;
        }
        std::string full_path = (browser_dir / entry.name).string();
        path_input->set_text(full_path);
        if (dialog_mode == DialogMode::Open) perform_action(full_path);
    });

    auto open_dialog = [&](DialogMode mode) {
        dialog_mode = mode;
        dialog->set_title(mode == DialogMode::Save ? "Enregistrer sous (Entree=valider, Echap=annuler)"
                                                     : "Ouvrir (Entree=valider, Echap=annuler)");
        path_input->set_text(current_path);
        browser_dir = current_path.empty() ? fs::current_path() : fs::path(current_path).parent_path();
        if (browser_dir.empty()) browser_dir = fs::current_path();
        refresh_browser();
        dialog->show();
        app.focus_manager().set_focus(path_input);
    };

    grid->set_on_save_requested([&] { open_dialog(DialogMode::Save); });
    grid->set_on_open_requested([&] { open_dialog(DialogMode::Open); });
    grid->set_on_quit([&] { app.quit(0); });

    // --- Assemblage de l'ecran principal ---

    auto content = std::make_shared<Vertical>();
    content->add_child(selection_label, LayoutParams::fixed(1));
    content->add_child(grid, LayoutParams::stretch(1));
    content->add_child(status, LayoutParams::fixed(1));

    auto root = std::make_shared<Stack>();
    root->add_child(content);
    root->add_child(dialog);

    app.set_root(root);
    app.focus_manager().set_focus(grid);

    return app.run();
}
