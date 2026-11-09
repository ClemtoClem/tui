/* @file panels/ApplyPanel.cpp */

#include "panels/ApplyPanel.hpp"
#include "panels/project/Ui.hpp"

#include <algorithm>
#include <thread>

namespace yocto {

using tui::LayoutParams;

namespace {

const char* step_icon(StepStatus s) {
    switch (s) {
        case StepStatus::Pending: return "·";
        case StepStatus::Running: return ">";
        case StepStatus::Done:    return "✓";
        case StepStatus::Failed:  return "✗";
        case StepStatus::Skipped: return "—";
    }
    return "?";
}

} // namespace

ApplyPanel::ApplyPanel(tui::App& app, YoctoConfig config) : app_(app), config_(std::move(config)) {
    config_.apply_defaults();
    build_ui();
}

void ApplyPanel::build_ui() {
    using namespace ui;

    // --- Colonne de contrôle ---
    issues_list_ = list();
    dry_run_ = std::make_shared<tui::Checkbox>("Mode simulation (rien n'est écrit ni exécuté)");
    dry_run_->set_checked(true);

    const ProjectRunner probe(config_, true, nullptr);
    for (const auto& s : probe.steps()) {
        auto cb = std::make_shared<tui::Checkbox>(s.label + " — " + s.description);
        // La vérification et les patchs lancent BitBake : décochés par défaut.
        cb->set_checked(s.id != ProjectStepId::CheckLayers && s.id != ProjectStepId::ApplyPatches);
        step_checks_.emplace_back(s.id, cb);
    }
    build_after_ = std::make_shared<tui::Checkbox>("Puis lancer bitbake <image> (onglet Build)");

    steps_list_ = list();
    {
        std::vector<tui::ListItem> items;
        for (const auto& s : probe.steps()) items.push_back(tui::ListItem{s.label, "", true, step_icon(StepStatus::Pending)});
        steps_list_->set_items(std::move(items));
    }

    auto preview = button("Aperçu", [this] { refresh_preview(); });
    auto run_all = button("Exécuter les étapes cochées", [this] { run(true); });
    auto run_one = button("Étape sélectionnée", [this] { run(false); });
    auto stop = button("Arrêter", [this] {
        if (runner_ && running_) {
            runner_->request_stop();
            status("Arrêt demandé : la séquence s'arrêtera après l'étape en cours");
        }
    });

    auto left = std::make_shared<tui::Vertical>();
    left->add_child(muted("Problèmes détectés :"), LayoutParams::fixed(1));
    left->add_child(issues_list_, LayoutParams::fixed(6));
    left->add_child(dry_run_, LayoutParams::fixed(1));
    left->add_child(muted("Étapes :"), LayoutParams::fixed(1));
    for (const auto& [id, cb] : step_checks_) left->add_child(cb, LayoutParams::fixed(1));
    left->add_child(build_after_, LayoutParams::fixed(1));
    left->add_child(row({preview, run_all}), LayoutParams::fixed(1));
    left->add_child(row({run_one, stop}), LayoutParams::fixed(1));
    left->add_child(muted("État des étapes (sélectionnez-en une pour la relancer) :"), LayoutParams::fixed(1));
    left->add_child(steps_list_, LayoutParams::stretch(1));

    // --- Aperçu des fichiers ---
    summary_ = muted("", true);
    entries_list_ = list();
    entries_list_->set_on_select([this](size_t i) { show_entry(i); });
    auto files = std::make_shared<tui::Vertical>();
    files->add_child(summary_, LayoutParams::fixed(3));
    files->add_child(entries_list_, LayoutParams::stretch(1));
    files->add_child(muted("+ nouveau  ~ modifié  = identique  - supprimé  ! absent"), LayoutParams::fixed(1));

    // --- Contenu / diff ---
    mode_button_ = button("Afficher : diff", [this] {
        diff_mode_ = !diff_mode_;
        mode_button_->set_label(diff_mode_ ? "Afficher : diff" : "Afficher : contenu");
        show_entry(entries_list_->selected_index());
    });
    view_title_ = muted("");
    view_ = viewer();
    view_->set_style_hook([this](int line, int /*col*/) {
        tui::CharStyle st;
        if (!diff_mode_ || line < 0 || static_cast<size_t>(line) >= view_kinds_.size()) return st;
        switch (view_kinds_[static_cast<size_t>(line)]) {
            case '+': st.fg = kSuccess; break;
            case '-': st.fg = kError; break;
            case '.': st.fg = kMuted; break;
            default: break;
        }
        return st;
    });
    auto view_box = std::make_shared<tui::Vertical>();
    auto title_row = std::make_shared<tui::Horizontal>();
    title_row->add_child(mode_button_, LayoutParams{tui::LayoutMode::Auto, 0, tui::CrossAlign::Start});
    title_row->add_child(view_title_, LayoutParams::stretch(1));
    view_box->add_child(title_row, LayoutParams::fixed(1));
    view_box->add_child(view_, LayoutParams::stretch(1));

    log_ = viewer();

    auto right = std::make_shared<tui::Vertical>();
    right->add_child(box("Contenu", view_box), LayoutParams::stretch(3));
    right->add_child(box("Journal", log_), LayoutParams::stretch(2));

    auto columns = std::make_shared<tui::Horizontal>();
    columns->add_child(box("Générer & appliquer", left), LayoutParams::fixed(50));
    columns->add_child(box("Fichiers produits", files), LayoutParams::stretch(2));
    columns->add_child(right, LayoutParams::stretch(3));
    root_ = columns;
}

void ApplyPanel::set_config(const YoctoConfig& cfg) {
    config_ = cfg;
    config_.apply_defaults();
}

void ApplyPanel::refresh_preview() {
    const auto plan = LayerGenerator(config_, openssl_password_hasher).generate();
    issues_ = plan.issues;
    entries_ = preview_build_conf(plan);
    auto layer = preview_layer(plan);
    entries_.insert(entries_.end(), std::make_move_iterator(layer.begin()), std::make_move_iterator(layer.end()));

    // --- Problèmes ---
    std::vector<tui::ListItem> issue_items;
    for (const auto& i : issues_) {
        issue_items.push_back(tui::ListItem{i.where + " : " + i.message, "", true,
                                            i.level == Issue::Level::Error ? "✗" : "!"});
    }
    if (issue_items.empty()) issue_items.push_back(tui::ListItem{"aucun problème", "", true, "✓"});
    issues_list_->set_items(std::move(issue_items));

    // --- Fichiers ---
    int counts[5] = {0, 0, 0, 0, 0};
    std::vector<tui::ListItem> items;
    for (const auto& e : entries_) {
        ++counts[static_cast<int>(e.status)];
        const std::string label = e.in_layer ? e.display : "[build] " + e.display;
        items.push_back(tui::ListItem{label, status_text(e.status), true, status_icon(e.status)});
    }
    const size_t keep = entries_list_->selected_index();
    entries_list_->set_items(std::move(items));
    entries_list_->set_selected_index(std::min(keep, entries_.empty() ? 0 : entries_.size() - 1));
    show_entry(entries_list_->selected_index());

    summary_->set_text(plan.layer_dir.string() + "\n" +
                       std::to_string(counts[0]) + " nouveau(x), " + std::to_string(counts[1]) + " modifié(s), " +
                       std::to_string(counts[2]) + " identique(s), " + std::to_string(counts[3]) + " supprimé(s)" +
                       (counts[4] ? ", " + std::to_string(counts[4]) + " absent(s)" : std::string()));

    const bool errors = has_errors(issues_);
    status(errors ? "Erreur : le projet contient des erreurs (liste « Problèmes détectés »)"
                  : "Aperçu à jour : " + std::to_string(counts[0] + counts[1] + counts[3]) + " changement(s) à appliquer");
}

void ApplyPanel::show_entry(size_t index) {
    view_kinds_.clear();
    if (index >= entries_.size()) {
        view_title_->set_text("");
        view_->set_text("Cliquez sur « Aperçu » pour calculer les fichiers produits par le projet.");
        return;
    }
    const auto& e = entries_[index];
    view_title_->set_text("  " + e.path.string() + "  (" + status_text(e.status) + ", " + e.origin + ")");

    std::string text;
    if (e.binary) {
        text = e.new_content;
    } else if (!diff_mode_ || e.status == EntryStatus::New || e.status == EntryStatus::Missing) {
        text = e.status == EntryStatus::Removed ? e.old_content : e.new_content;
    } else if (e.status == EntryStatus::Removed) {
        text = format_diff(line_diff(e.old_content, ""));
    } else {
        text = format_diff(line_diff(e.old_content, e.new_content));
    }

    if (diff_mode_) {
        size_t start = 0;
        while (start <= text.size()) {
            const size_t nl = text.find('\n', start);
            const std::string line = text.substr(start, nl == std::string::npos ? std::string::npos : nl - start);
            const bool is_diff = e.status == EntryStatus::Modified || e.status == EntryStatus::Removed;
            char kind = ' ';
            if (is_diff && !line.empty()) kind = line.rfind("  ...", 0) == 0 ? '.' : line[0];
            else if (e.status == EntryStatus::New) kind = '+';
            view_kinds_.push_back(kind);
            if (nl == std::string::npos) break;
            start = nl + 1;
        }
    }
    view_->set_text(text);
}

std::vector<ProjectStepId> ApplyPanel::checked_steps() const {
    std::vector<ProjectStepId> out;
    for (const auto& [id, cb] : step_checks_) {
        if (cb->checked()) out.push_back(id);
    }
    return out;
}

void ApplyPanel::set_step_status(ProjectStepId id, StepStatus s, const std::string& msg) {
    auto items = steps_list_->items();
    for (size_t i = 0; i < step_checks_.size() && i < items.size(); ++i) {
        if (step_checks_[i].first != id) continue;
        items[i].prefix_icon = step_icon(s);
        items[i].detail = msg;
    }
    steps_list_->set_items(std::move(items));
}

void ApplyPanel::run(bool all_selected) {
    if (running_) {
        status("Erreur : une exécution est déjà en cours");
        return;
    }
    const bool dry = dry_run_->checked();
    std::vector<ProjectStepId> steps;
    if (all_selected) {
        steps = checked_steps();
    } else {
        const size_t i = steps_list_->selected_index();
        if (i < step_checks_.size()) steps.push_back(step_checks_[i].first);
    }
    if (steps.empty()) {
        status("Erreur : aucune étape sélectionnée");
        return;
    }

    refresh_preview();
    if (has_errors(issues_) && std::find(steps.begin(), steps.end(), ProjectStepId::WriteLayer) != steps.end()) {
        status("Erreur : corrigez les erreurs du projet avant d'écrire le layer");
        return;
    }

    {
        std::vector<tui::ListItem> items = steps_list_->items();
        for (auto& it : items) { it.prefix_icon = step_icon(StepStatus::Pending); it.detail.clear(); }
        steps_list_->set_items(std::move(items));
    }
    log_buffer_.clear();
    append_log(std::string("=== ") + (dry ? "Simulation" : "Application") + " du projet " +
               config_.project.layer_name + " (configuration " + config_.name + ") ===");

    runner_ = std::make_unique<ProjectRunner>(config_, dry, openssl_password_hasher);
    running_ = true;
    run_failed_ = false;
    status(dry ? "Simulation en cours..." : "Application en cours...");

    runner_->set_on_output([this](const std::string& line) {
        app_.post([this, line] { append_log(line); });
    });
    runner_->set_on_status([this](ProjectStepId id, StepStatus s, const std::string& msg) {
        app_.post([this, id, s, msg] {
            if (s == StepStatus::Failed) run_failed_ = true;
            set_step_status(id, s, msg);
        });
    });

    ProjectRunner* runner = runner_.get();
    const bool chain_build = all_selected && !dry && build_after_->checked();
    std::thread([this, runner, steps, all_selected, dry, chain_build] {
        if (all_selected) runner->run(steps);
        else runner->run_step(steps.front());
        app_.post([this, dry, chain_build] {
            running_ = false;
            refresh_preview();
            append_log("");
            if (run_failed_) {
                append_log("=== Échec : voir l'étape marquée ✗ ===");
                status("Erreur : une étape a échoué (voir le journal)");
                return;
            }
            append_log(dry ? "=== Simulation terminée : décochez « Mode simulation » pour appliquer ===" :
                             "=== Projet appliqué ===");
            status(dry ? "✓ Simulation terminée sans erreur" : "✓ Projet appliqué au lab");
            if (chain_build && on_request_build_) on_request_build_(config_.project.image.name);
        });
    }).detach();
}

void ApplyPanel::append_log(const std::string& line) {
    log_buffer_ += line + "\n";
    constexpr size_t kMaxChars = 256 * 1024;
    if (log_buffer_.size() > kMaxChars) {
        log_buffer_.erase(0, log_buffer_.size() - kMaxChars / 2);
        const auto nl = log_buffer_.find('\n');
        if (nl != std::string::npos) log_buffer_.erase(0, nl + 1);
    }
    log_->set_text(log_buffer_);
    log_->scroll_to_end();
}

} // namespace yocto
