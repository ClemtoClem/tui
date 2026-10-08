/* apps/yocto-tui/panels/QemuPanel.cpp */
#include "QemuPanel.hpp"

#include "../Theme.hpp"
#include "../util/Shell.hpp"

#include <tui/widget/layout/Horizontal.hpp>
#include <tui/widget/layout/Vertical.hpp>

#include <algorithm>
#include <sstream>

using tui::Border;
using tui::BorderStyle;
using tui::Button;
using tui::Horizontal;
using tui::Label;
using tui::LayoutParams;
using tui::ListItem;
using tui::ListView;
using tui::ListViewStyle;
using tui::Vertical;

namespace yocto {

namespace {

std::string human_size(std::uintmax_t bytes) {
    std::ostringstream oss;
    oss.precision(1);
    if (bytes >= 1024ULL * 1024ULL * 1024ULL) {
        oss << std::fixed << (static_cast<double>(bytes) / (1024.0 * 1024.0 * 1024.0)) << " Gio";
    } else if (bytes >= 1024ULL * 1024ULL) {
        oss << std::fixed << (static_cast<double>(bytes) / (1024.0 * 1024.0)) << " Mio";
    } else if (bytes >= 1024ULL) {
        oss << std::fixed << (static_cast<double>(bytes) / 1024.0) << " Kio";
    } else {
        oss << bytes << " o";
    }
    return oss.str();
}

} // namespace

QemuPanel::QemuPanel(YoctoConfig config) : config_(std::move(config)) {
    config_.apply_defaults();
    build_ui();
    refresh();
}

void QemuPanel::build_ui() {
    auto lbl = [](const std::string& s) {
        auto l = std::make_shared<Label>(s);
        l->set_foreground(kMuted);
        return l;
    };

    machine_label_ = std::make_shared<Label>("MACHINE : " + config_.machine);
    machine_label_->set_foreground(kFg);

    ready_label_ = std::make_shared<Label>("");
    ready_label_->set_wrap(true);

    deploy_label_ = std::make_shared<Label>("");
    deploy_label_->set_foreground(kMuted);
    deploy_label_->set_wrap(true);

    file_list_ = std::make_shared<ListView>();
    file_list_->set_style(ListViewStyle::Detailed);
    file_list_->set_colors(kAccent, kFg, kMuted);
    file_list_->set_show_scrollbar(true);

    nographic_button_ = std::make_shared<Button>("runqemu nographic");
    nographic_button_->set_on_click([this] {
        if (on_run_command_) on_run_command_(qemu_command("nographic"));
    });

    graphical_button_ = std::make_shared<Button>("runqemu fenêtre");
    graphical_button_->set_on_click([this] {
        if (on_run_command_) on_run_command_(qemu_command(""));
    });

    env_button_ = std::make_shared<Button>("Charger env.sh");
    env_button_->set_on_click([this] {
        if (on_run_command_) on_run_command_("source " + shell_quote(config_.env_script_path().string()));
    });

    auto row1 = std::make_shared<Horizontal>();
    row1->add_child(nographic_button_, LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    row1->add_child(graphical_button_, LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});

    auto row2 = std::make_shared<Horizontal>();
    row2->add_child(env_button_, LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});

    auto hint1 = std::make_shared<Label>("Connexion : root, sans mot de passe.");
    hint1->set_foreground(kMuted);
    hint1->set_wrap(true);
    auto hint2 = std::make_shared<Label>("nographic : quitter QEMU avec Ctrl+A puis x.");
    hint2->set_foreground(kMuted);
    hint2->set_wrap(true);
    auto hint3 = std::make_shared<Label>("Fenêtre : Ctrl+Alt+G libère la souris capturée.");
    hint3->set_foreground(kMuted);
    hint3->set_wrap(true);

    auto content = std::make_shared<Vertical>();
    content->add_child(machine_label_, LayoutParams::fixed(1));
    content->add_child(ready_label_, LayoutParams::fixed(2));
    content->add_child(deploy_label_, LayoutParams::fixed(2));
    content->add_child(row1, LayoutParams::fixed(1));
    content->add_child(row2, LayoutParams::fixed(1));
    content->add_child(std::make_shared<Label>(""), LayoutParams::fixed(1));
    content->add_child(lbl("Fichiers déployés :"), LayoutParams::fixed(1));
    content->add_child(file_list_, LayoutParams::stretch(1));
    content->add_child(std::make_shared<Label>(""), LayoutParams::fixed(1));
    content->add_child(hint1, LayoutParams::fixed(1));
    content->add_child(hint2, LayoutParams::fixed(1));
    content->add_child(hint3, LayoutParams::fixed(1));

    panel_ = std::make_shared<Border>();
    panel_->set_border_style(BorderStyle::Rounded);
    panel_->set_color(kBorder);
    panel_->set_title("Image & QEMU");
    panel_->set_child(content);
}

void QemuPanel::set_config(const YoctoConfig& cfg) {
    config_ = cfg;
    config_.apply_defaults();
    refresh();
}

std::string QemuPanel::qemu_command(const std::string& mode) const {
    // Séquence du lab : source de l'environnement puis runqemu.
    // L'environnement (MACHINE, chemins) vient de local.conf.
    std::string cmd = "cd " + shell_quote(config_.poky_dir().string()) +
                      " && source oe-init-build-env " + shell_quote(config_.build_dir_path().string());
    if (!mode.empty()) cmd += " && runqemu " + mode;
    return cmd;
}

ImageStatus QemuPanel::detect() const {
    ImageStatus st;
    std::error_code ec;
    std::filesystem::path deploy =
        config_.build_dir_path() / "tmp" / "deploy" / "images" / config_.machine;
    st.deploy_dir = deploy;

    if (!std::filesystem::exists(deploy, ec)) {
        st.message = "Aucune image : préparez le lab (onglet 1) puis lancez un build (onglet 2).";
        return st;
    }

    // Le répertoire contient à la fois les fichiers horodatés et les
    // liens symboliques stables (ex : core-image-minimal-qemux86-64.ext4,
    // bzImage-qemux86-64.bin, *.qemuboot.conf). On privilégie les
    // liens stables ; s'il n'y en a pas (cas rares), tous les fichiers.
    std::vector<std::pair<std::string, std::uintmax_t>> stable;
    std::vector<std::pair<std::string, std::uintmax_t>> all;

    for (const auto& entry : std::filesystem::directory_iterator(deploy, ec)) {
        const std::string name = entry.path().filename().string();
        std::error_code ec2;
        if (entry.is_symlink(ec2)) {
            const auto size = std::filesystem::file_size(entry.path(), ec2);
            stable.emplace_back(name, ec2 ? 0 : size);
            if (name.find(".qemuboot.conf") != std::string::npos) st.ready = true;
        } else if (entry.is_regular_file(ec2)) {
            const auto size = entry.file_size(ec2);
            all.emplace_back(name, ec2 ? 0 : size);
            if (name.find(".qemuboot.conf") != std::string::npos) st.ready = true;
        }
    }

    auto& files = !stable.empty() ? stable : all;
    std::sort(files.begin(), files.end(),
              [](const auto& a, const auto& b) { return a.second > b.second; });
    if (files.size() > 15) files.resize(15);
    for (const auto& [name, size] : files) {
        st.files.emplace_back(name, static_cast<std::size_t>(size));
    }
    if (st.files.empty()) {
        st.message = "Répertoire de déploiement vide : lancez un build (onglet 2).";
    }
    return st;
}

void QemuPanel::refresh() {
    const ImageStatus st = detect();

    machine_label_->set_text("MACHINE : " + config_.machine);
    deploy_label_->set_text("Déploiement : " + st.deploy_dir.string());

    if (st.ready) {
        ready_label_->set_text("✓ Image prête pour runqemu.");
        ready_label_->set_foreground(kSuccess);
    } else if (!st.files.empty()) {
        ready_label_->set_text("Image incomplète : aucun .qemuboot.conf, relancez le build.");
        ready_label_->set_foreground(kWarning);
    } else {
        ready_label_->set_text(st.message);
        ready_label_->set_foreground(kMuted);
    }

    std::vector<ListItem> items;
    for (const auto& [name, size] : st.files) {
        items.push_back(ListItem{name, human_size(static_cast<std::uintmax_t>(size)), true, ""});
    }
    file_list_->set_items(std::move(items));
}

} // namespace yocto