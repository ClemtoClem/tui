/* panels/BuildPanel.cpp */
#include "panels/BuildPanel.hpp"
#include "Theme.hpp"

#include <tui/widget/layout/Horizontal.hpp>
#include <tui/widget/layout/Vertical.hpp>

#include <filesystem>
#include <regex>
#include <sstream>

using tui::Border;
using tui::BorderStyle;
using tui::Button;
using tui::Horizontal;
using tui::Input;
using tui::Label;
using tui::LayoutParams;
using tui::ProgressBar;
using tui::TextArea;
using tui::Vertical;

namespace yocto {

namespace {

std::string format_duration(std::chrono::steady_clock::duration d) {
    const auto total = std::chrono::duration_cast<std::chrono::seconds>(d).count();
    const int h = static_cast<int>(total / 3600);
    const int m = static_cast<int>((total % 3600) / 60);
    const int s = static_cast<int>(total % 60);
    std::ostringstream oss;
    if (h > 0) oss << h << " h ";
    oss << m << " min " << s << " s";
    return oss.str();
}

} // namespace

BuildPanel::BuildPanel(tui::App& app, YoctoConfig config)
    : app_(app), config_(std::move(config)), runner_(config_) {
    config_.apply_defaults();
    build_ui();

    // Les callbacks du runner sont appelés depuis son thread lecteur :
    // on les marshale vers le thread UI via App::post().
    runner_.set_on_output([this](const std::string& line) {
        app_.post([this, line] { on_output_line(line); });
    });
    runner_.set_on_done([this](bool ok, int /*code*/, const std::string& msg) {
        app_.post([this, ok, msg] {
            building_ = false;
            append_log("=== " + msg + " ===");
            if (ok) {
                progress_->set_progress(1.0);
                status_label_->set_text("✓ " + msg);
                status_label_->set_foreground(kSuccess);
            } else {
                status_label_->set_text("✗ " + msg);
                status_label_->set_foreground(kError);
            }
            if (on_status_) on_status_((ok ? "✓ " : "Erreur : ") + msg);
        });
    });
}

void BuildPanel::build_ui() {
    auto lbl = [](const std::string& s) {
        auto l = std::make_shared<Label>(s);
        l->set_foreground(kMuted);
        return l;
    };

    target_input_ = std::make_shared<Input>();
    target_input_->set_text(config_.image_recipe);
    target_input_->set_placeholder("core-image-minimal");

    start_button_ = std::make_shared<Button>("Lancer bitbake");
    start_button_->set_on_click([this] { start_build(); });

    cancel_button_ = std::make_shared<Button>("Annuler (SIGINT)");
    cancel_button_->set_on_click([this] { cancel_build(); });

    auto buttons = std::make_shared<Horizontal>();
    buttons->add_child(start_button_, LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    buttons->add_child(cancel_button_, LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});

    progress_ = std::make_shared<ProgressBar>();
    progress_->set_progress(0.0);

    progress_label_ = std::make_shared<Label>("Tâches : 0 / 0");
    progress_label_->set_foreground(kMuted);

    elapsed_label_ = std::make_shared<Label>("Temps écoulé : 0 s");
    elapsed_label_->set_foreground(kMuted);

    task_label_ = std::make_shared<Label>("Tâche courante : -");
    task_label_->set_foreground(kFg);

    status_label_ = std::make_shared<Label>("Prêt");
    status_label_->set_foreground(kMuted);

    auto hints = std::make_shared<Label>(
        "Premier build : comptez 1 à 3 h. La progression n'apparaît qu'au moment de "
        "l'exécution des tâches ; un build annulé reprend très vite là où il s'était arrêté.");
    hints->set_foreground(kMuted);
    hints->set_wrap(true);

    auto left = std::make_shared<Vertical>();
    left->add_child(lbl("Cible bitbake (recette d'image)"), LayoutParams::fixed(1));
    left->add_child(target_input_, LayoutParams::fixed(1));
    left->add_child(buttons, LayoutParams::fixed(1));
    left->add_child(std::make_shared<Label>(""), LayoutParams::fixed(1));
    left->add_child(progress_, LayoutParams::fixed(1));
    left->add_child(progress_label_, LayoutParams::fixed(1));
    left->add_child(elapsed_label_, LayoutParams::fixed(1));
    left->add_child(std::make_shared<Label>(""), LayoutParams::fixed(1));
    left->add_child(task_label_, LayoutParams::fixed(1));
    left->add_child(status_label_, LayoutParams::fixed(1));
    left->add_child(std::make_shared<Label>(""), LayoutParams::fixed(1));
    left->add_child(hints, LayoutParams::fixed(3));
    left->add_child(std::make_shared<Label>(""), LayoutParams::stretch(1));

    auto left_panel = std::make_shared<Border>();
    left_panel->set_border_style(BorderStyle::Rounded);
    left_panel->set_color(kBorder);
    left_panel->set_title("Pilotage");
    left_panel->set_child(left);

    log_view_ = std::make_shared<TextArea>();
    log_view_->set_read_only(true);
    log_view_->set_show_scrollbar(true);
    log_view_->set_accent_color(kAccent);

    auto log_panel = std::make_shared<Border>();
    log_panel->set_border_style(BorderStyle::Rounded);
    log_panel->set_color(kBorder);
    log_panel->set_title("Sortie bitbake");
    log_panel->set_child(log_view_);

    auto columns = std::make_shared<Horizontal>();
    columns->add_child(left_panel, LayoutParams::fixed(46));
    columns->add_child(log_panel, LayoutParams::stretch(1));

    panel_ = std::make_shared<Border>();
    panel_->set_border_style(BorderStyle::Rounded);
    panel_->set_color(kBorder);
    panel_->set_child(columns);
}

void BuildPanel::set_config(const YoctoConfig& cfg) {
    config_ = cfg;
    config_.apply_defaults();
    if (!building_) {
        target_input_->set_text(config_.image_recipe);
    }
}

void BuildPanel::start_build() {
    if (building_) {
        if (on_status_) on_status_("Erreur : un build est déjà en cours");
        return;
    }

    const std::string target = target_input_->text();
    if (target.empty()) {
        if (on_status_) on_status_("Erreur : cible bitbake vide");
        return;
    }

    // Vérifie que le lab a été préparé (onglet 1) avant de lancer.
    std::error_code ec;
    if (!std::filesystem::exists(config_.poky_dir(), ec)) {
        if (on_status_) on_status_("Erreur : poky introuvable — lancez d'abord la préparation (onglet 1)");
        return;
    }

    runner_.set_config(config_);
    log_buffer_.clear();
    log_view_->set_text("");
    reset_progress();
    append_log("=== bitbake " + target + " ===");

    building_ = true;
    cancelled_ = false;
    start_time_ = std::chrono::steady_clock::now();

    status_label_->set_text("Build en cours...");
    status_label_->set_foreground(kInfo);
    if (on_status_) on_status_("bitbake " + target + " démarré");

    if (!runner_.start(target)) {
        building_ = false;
        status_label_->set_text("✗ Impossible de lancer bitbake");
        status_label_->set_foreground(kError);
        if (on_status_) on_status_("Erreur : impossible de lancer bitbake");
    }
}

void BuildPanel::cancel_build() {
    if (!building_) return;
    cancelled_ = true;
    if (runner_.cancel()) {
        status_label_->set_text("Annulation demandée (SIGINT)...");
        if (on_status_) on_status_("Annulation du build demandée");
    }
}

void BuildPanel::tick() {
    if (!building_) return;
    const auto elapsed = std::chrono::steady_clock::now() - start_time_;
    elapsed_label_->set_text("Temps écoulé : " + format_duration(elapsed));
}

void BuildPanel::on_output_line(const std::string& line) {
    append_log(line);

    // "NOTE: Running task 5 of 42 (/path/meta-x/recette_1.2.bb:do_compile)"
    static const std::regex task_re(R"(NOTE: Running task ([0-9]+) of ([0-9]+) \(([^)]*)\))");
    std::smatch m;
    if (std::regex_search(line, m, task_re)) {
        completed_tasks_ = std::stoi(m[1]);
        total_tasks_ = std::stoi(m[2]);
        update_progress();

        std::string desc = m[3];
        const auto slash = desc.find_last_of('/');
        if (slash != std::string::npos) desc = desc.substr(slash + 1);
        const auto colon = desc.find(':');
        if (colon != std::string::npos) desc = desc.substr(0, colon);
        task_label_->set_text("Tâche courante : " + desc);
        return;
    }

    // "NOTE: Tasks Summary: Attempted 951 tasks..."
    static const std::regex summary_re(R"(NOTE: Tasks Summary: Attempted ([0-9]+) tasks)");
    if (std::regex_search(line, m, summary_re)) {
        completed_tasks_ = std::stoi(m[1]);
        if (total_tasks_ < completed_tasks_) total_tasks_ = completed_tasks_;
        update_progress();
    }
}

void BuildPanel::append_log(const std::string& line) {
    log_buffer_ += line + "\n";
    // Cap mémoire : on garde au plus ~256 Kio de journal.
    constexpr size_t kMaxChars = 256 * 1024;
    if (log_buffer_.size() > kMaxChars) {
        log_buffer_.erase(0, log_buffer_.size() - kMaxChars / 2);
        const auto nl = log_buffer_.find('\n');
        if (nl != std::string::npos) log_buffer_.erase(0, nl + 1);
    }
    log_view_->set_text(log_buffer_);
}

void BuildPanel::update_progress() {
    const double pct = total_tasks_ > 0
        ? static_cast<double>(completed_tasks_) / static_cast<double>(total_tasks_)
        : 0.0;
    progress_->set_progress(pct);
    std::ostringstream oss;
    oss << "Tâches : " << completed_tasks_ << " / " << total_tasks_;
    if (total_tasks_ > 0) oss << " (" << static_cast<int>(pct * 100.0) << " %)";
    progress_label_->set_text(oss.str());
}

void BuildPanel::reset_progress() {
    total_tasks_ = 0;
    completed_tasks_ = 0;
    progress_->set_progress(0.0);
    progress_label_->set_text("Tâches : 0 / 0");
    elapsed_label_->set_text("Temps écoulé : 0 s");
    task_label_->set_text("Tâche courante : -");
}

} // namespace yocto