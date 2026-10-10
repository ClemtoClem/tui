/* @file runners/SetupRunner.cpp */

#include "runners/SetupRunner.hpp"
#include "utils/Shell.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <unistd.h>

namespace yocto {

namespace fs = std::filesystem;

namespace {
constexpr const char* kLocalConfMarker = "# --- yocto-tui : configuration lab ---";
} // namespace

SetupRunner::SetupRunner(YoctoConfig config) : config_(std::move(config)) {
    config_.apply_defaults();

    steps_ = {
        {StepId::CheckPrereqs,     "Vérification des prérequis",     "root, sudo, OS, disque, RAM"},
        {StepId::InstallPackages,  "Installation des paquets hôte",  "apt-get install (build-essential, git, qemu...)"},
        {StepId::ConfigureLocale,  "Locale UTF-8",                   "en_US.UTF-8 via locale-gen"},
        {StepId::FixAppArmor,      "Restriction AppArmor",           "user namespaces (Ubuntu 24.04)"},
        {StepId::ConfigureGit,     "Configuration git",              "user.name / user.email si absents"},
        {StepId::CreateTree,       "Arborescence du lab",            "layers/, builds/, cache/"},
        {StepId::ClonePoky,        "Clone de poky",                  "git clone ou fetch --tags"},
        {StepId::CheckoutTag,      "Figeage du tag",                 "branche <branche>-lab"},
        {StepId::InitBuildDir,     "Répertoire de build",            "oe-init-build-env + local.conf"},
        {StepId::CreateEnvScript,  "Script env.sh",                  "source <lab>/env.sh"},
        {StepId::FirstBuild,       "Premier build",                  "bitbake <image> (long)"},
    };
    statuses_.assign(steps_.size(), StepStatus::Pending);
}

StepStatus SetupRunner::status(StepId id) const {
    for (size_t i = 0; i < steps_.size(); ++i) {
        if (steps_[i].id == id) return statuses_[i];
    }
    return StepStatus::Pending;
}

bool SetupRunner::is_enabled(StepId id) const {
    switch (id) {
        case StepId::InstallPackages: return config_.install_packages;
        case StepId::ConfigureLocale: return config_.configure_locale;
        case StepId::FixAppArmor:     return config_.fix_apparmor;
        case StepId::ConfigureGit:    return config_.configure_git;
        case StepId::FirstBuild:      return config_.first_build;
        default:                      return true;  // étapes structurelles toujours actives
    }
}

void SetupRunner::log(const std::string& line) {
    if (on_output_) on_output_(line);
}

void SetupRunner::emit_status(StepId id, StepStatus s, const std::string& msg) {
    for (size_t i = 0; i < steps_.size(); ++i) {
        if (steps_[i].id == id) statuses_[i] = s;
    }
    if (on_status_) on_status_(id, s, msg);
}

bool SetupRunner::exec(const std::string& cmd, bool root, std::string& err_out) {
    log("$ " + (root ? std::string("sudo -n ") : std::string()) + cmd);
    auto result = root ? run_shell_root(cmd, [this](const std::string& l) { log("  " + l); })
                       : run_shell(cmd,      [this](const std::string& l) { log("  " + l); });
    if (result.exit_code != 0) {
        err_out = "commande échouée (code " + std::to_string(result.exit_code) + ") : " + cmd;
        // Les dernières lignes aident au diagnostic
        std::istringstream iss(result.output);
        std::vector<std::string> lines;
        std::string l;
        while (std::getline(iss, l)) lines.push_back(l);
        size_t keep = std::min<size_t>(lines.size(), 3);
        for (size_t i = lines.size() - keep; i < lines.size(); ++i) log("  ! " + lines[i]);
        return false;
    }
    return true;
}

// --- Séquence principale -----------------------------------------------

void SetupRunner::run() {
    for (const auto& step : steps_) {
        if (!is_enabled(step.id)) {
            emit_status(step.id, StepStatus::Skipped, "désactivé par la configuration");
            continue;
        }
        if (!run_step(step.id)) break;  // arrêt à la première erreur
    }
}

bool SetupRunner::run_step(StepId id) {
    std::string msg;
    bool ok = false;

    emit_status(id, StepStatus::Running);
    log("");
    log("=== " + std::string([&] {
        for (auto& s : steps_) if (s.id == id) return s.label;
        return std::string("?");
    }()) + " ===");

    switch (id) {
        case StepId::CheckPrereqs:     ok = step_check_prereqs(msg);    break;
        case StepId::InstallPackages:  ok = step_install_packages(msg); break;
        case StepId::ConfigureLocale:  ok = step_configure_locale(msg); break;
        case StepId::FixAppArmor:     ok = step_fix_apparmor(msg);     break;
        case StepId::ConfigureGit:    ok = step_configure_git(msg);    break;
        case StepId::CreateTree:      ok = step_create_tree(msg);      break;
        case StepId::ClonePoky:       ok = step_clone_poky(msg);       break;
        case StepId::CheckoutTag:     ok = step_checkout_tag(msg);     break;
        case StepId::InitBuildDir:    ok = step_init_build_dir(msg);   break;
        case StepId::CreateEnvScript: ok = step_create_env_script(msg);break;
        case StepId::FirstBuild:      ok = step_first_build(msg);      break;
    }

    emit_status(id, ok ? StepStatus::Done : StepStatus::Failed, msg);
    return ok;
}

// --- Étape 1 : prérequis -----------------------------------------------

bool SetupRunner::step_check_prereqs(std::string& msg) {
    if (geteuid() == 0) {
        msg = "ne pas lancer yocto-tui en root (BitBake le refuse)";
        log("! " + msg);
        return false;
    }
    if (!command_exists("sudo")) {
        msg = "sudo introuvable dans le PATH";
        log("! " + msg);
        return false;
    }

    // OS : avertissement non bloquant
    {
        std::ifstream osr("/etc/os-release");
        std::string content((std::istreambuf_iterator<char>(osr)), std::istreambuf_iterator<char>());
        if (content.find("Ubuntu") == std::string::npos) {
            log("  avertissement : hôte non-Ubuntu, certaines étapes peuvent échouer");
        }
    }

    // Disque : avertissement non bloquant
    {
        std::error_code ec;
        auto si = fs::space(config_.workdir.empty() ? fs::current_path() : fs::path(config_.workdir), ec);
        if (!ec && si.available < static_cast<uintmax_t>(100) * 1024 * 1024 * 1024) {
            unsigned long long go = si.available / (1024ULL * 1024ULL * 1024ULL);
            log("  avertissement : " + std::to_string(go) + " Go libres sur " + config_.workdir +
                " (100 Go conseillés)");
        }
    }

    // Sudo non-interactif : bloquant, toutes les étapes root en dépendent
    if (!sudo_is_ready()) {
        msg = "droits sudo non rafraîchis — ouvrez l'onglet QEMU & Terminal et tapez : sudo -v";
        log("! " + msg);
        return false;
    }

    msg = "prérequis OK";
    return true;
}

// --- Étape 2 : paquets hôte --------------------------------------------

bool SetupRunner::step_install_packages(std::string& msg) {
    // Idempotence : test réel via dpkg (command_exists() cherche un
    // binaire dans le PATH, pas un paquet installé).
    {
        auto r = run_shell("dpkg -l python3-pexpect 2>/dev/null | grep -q '^ii'");
        if (r.exit_code == 0) { msg = "paquets déjà présents"; return true; }
    }

    const std::vector<std::string> pkgs = {
        "gawk", "wget", "git", "diffstat", "unzip", "texinfo", "gcc", "build-essential",
        "chrpath", "socat", "cpio", "python3", "python3-pip", "python3-pexpect", "xz-utils",
        "debianutils", "iputils-ping", "python3-git", "python3-jinja2", "python3-subunit",
        "zstd", "liblz4-tool", "file", "locales", "libacl1",
        "qemu-system-x86", "libsdl1.2-dev", "xterm", "tmux", "tree",
    };
    std::ostringstream cmd;
    cmd << "apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y";
    for (const auto& p : pkgs) cmd << " " << p;

    std::string err;
    if (!exec(cmd.str(), /*root=*/true, err)) { msg = err; return false; }
    msg = "paquets installés";
    return true;
}

// --- Étape 3 : locale --------------------------------------------------

bool SetupRunner::step_configure_locale(std::string& msg) {
    auto r = run_shell("locale -a 2>/dev/null | grep -qi '^en_US.utf8$'");
    if (r.exit_code == 0) { msg = "en_US.UTF-8 déjà disponible"; return true; }

    std::string err;
    if (!exec("locale-gen en_US.UTF-8", true, err)) { msg = err; return false; }
    if (!exec("update-locale LANG=en_US.UTF-8", true, err)) { msg = err; return false; }
    msg = "locale configurée";
    return true;
}

// --- Étape 4 : AppArmor ------------------------------------------------

bool SetupRunner::step_fix_apparmor(std::string& msg) {
    auto r = run_shell("sysctl -n kernel.apparmor_restrict_unprivileged_userns 2>/dev/null");
    if (r.exit_code != 0 || r.output.find_first_not_of(" \n\t") == std::string::npos) {
        msg = "paramètre absent sur ce noyau";
        return true;
    }
    if (r.output.find("0") != std::string::npos) {
        msg = "déjà désactivé";
        return true;
    }

    std::string err;
    if (!exec("sysctl -w kernel.apparmor_restrict_unprivileged_userns=0", true, err)) {
        msg = err; return false;
    }
    if (!exec("sh -c 'echo kernel.apparmor_restrict_unprivileged_userns=0 > /etc/sysctl.d/99-yocto.conf'",
              true, err)) {
        msg = err; return false;
    }
    msg = "restriction levée et persistée";
    return true;
}

// --- Étape 5 : git -----------------------------------------------------

bool SetupRunner::step_configure_git(std::string& msg) {
    auto email = run_shell("git config --global user.email 2>/dev/null");
    if (email.exit_code == 0 && email.output.find_first_not_of(" \n\t") != std::string::npos) {
        msg = "déjà configuré";
        return true;
    }
    std::string err;
    if (!exec("git config --global user.name " + shell_quote(config_.git_user_name), false, err)) {
        msg = err; return false;
    }
    if (!exec("git config --global user.email " + shell_quote(config_.git_user_email), false, err)) {
        msg = err; return false;
    }
    msg = "git configuré";
    return true;
}

// --- Étape 6 : arborescence --------------------------------------------

bool SetupRunner::step_create_tree(std::string& msg) {
    // Organisation du lab : layers/ et builds/ créés à la main, les
    // caches (downloads, sstate) partagés entre builds.
    std::error_code ec;
    fs::create_directories(config_.layers_dir(), ec);
    fs::create_directories(config_.builds_dir(), ec);
    fs::create_directories(config_.downloads_dir(), ec);
    fs::create_directories(config_.sstate_cache_dir(), ec);
    if (ec) { msg = "création impossible : " + ec.message(); return false; }
    msg = "arborescence créée sous " + config_.workdir;
    return true;
}

// --- Étape 7 : clone poky ----------------------------------------------

bool SetupRunner::step_clone_poky(std::string& msg) {
    auto poky = config_.poky_dir();
    std::string err;

    if (fs::exists(poky / ".git")) {
        if (!exec("git -C " + shell_quote(poky.string()) + " fetch --tags origin", false, err)) {
            msg = err; return false;
        }
        msg = "poky déjà cloné, références mises à jour";
        return true;
    }

    std::error_code ec;
    fs::create_directories(poky.parent_path(), ec);
    if (ec) { msg = "création du répertoire impossible : " + ec.message(); return false; }

    std::string cmd = "git clone " + shell_quote(config_.poky_url) +
                      " -b " + shell_quote(config_.poky_branch) +
                      " " + shell_quote(poky.string());
    if (!exec(cmd, false, err)) { msg = err; return false; }
    msg = "poky cloné dans " + poky.string();
    return true;
}

// --- Étape 8 : tag -----------------------------------------------------

bool SetupRunner::step_checkout_tag(std::string& msg) {
    auto poky = config_.poky_dir();
    const std::string poky_q = shell_quote(poky.string());
    std::string tag = config_.yocto_tag;
    std::string err;

    if (tag.empty()) {
        // Dernier tag yocto-* atteignable depuis la branche courante :
        // scarthgap -> yocto-5.0.x, styhead -> yocto-5.1.x, etc.
        // (reproductibilité : on fige une version précise, cf. lab I.2)
        auto r = run_shell("git -C " + poky_q +
                           " tag --merged HEAD -l 'yocto-*' | sort -V | tail -n 1");
        tag = r.output;
        while (!tag.empty() && (tag.back() == '\n' || tag.back() == '\r' || tag.back() == ' ')) tag.pop_back();
        if (tag.empty()) { msg = "aucun tag yocto-* trouvé sur cette branche"; return false; }
        log("  tag résolu automatiquement : " + tag);
    }

    auto verify = run_shell("git -C " + poky_q + " rev-parse -q --verify refs/tags/" + shell_quote(tag));
    if (verify.exit_code != 0) { msg = "tag inexistant : " + tag; return false; }

    // Branche de travail locale <branche>-lab (jamais en tête détachée,
    // pour pouvoir conserver d'éventuelles modifications locales).
    // Compatibilité : les versions précédentes créaient "scarthgap-formation".
    std::string branch = config_.poky_branch + "-lab";
    if (run_shell("git -C " + poky_q + " show-ref -q --verify refs/heads/scarthgap-formation").exit_code == 0) {
        branch = "scarthgap-formation";
    }

    std::string cmd;
    if (run_shell("git -C " + poky_q + " show-ref -q --verify refs/heads/" + branch).exit_code == 0) {
        cmd = "git -C " + poky_q + " checkout " + shell_quote(branch);
    } else {
        cmd = "git -C " + poky_q + " checkout -b " + shell_quote(branch) + " " + shell_quote(tag);
    }
    if (!exec(cmd, false, err)) { msg = err; return false; }
    msg = "positionné sur " + tag + " (branche " + branch + ")";
    return true;
}

// --- Étape 9 : répertoire de build + local.conf ------------------------

bool SetupRunner::step_init_build_dir(std::string& msg) {
    auto poky = config_.poky_dir();
    auto build_dir = config_.build_dir_path();
    auto local_conf = config_.local_conf_path();

    // Idempotence : si local.conf existe déjà, on ne réécrit rien (il a
    // pu être ajusté manuellement).
    if (fs::exists(local_conf)) {
        msg = "local.conf déjà présent";
        return true;
    }

    // Source oe-init-build-env en bash : crée le répertoire de build
    // s'il n'existe pas encore et génère conf/local.conf.
    std::ostringstream sh;
    sh << "cd " << shell_quote(poky.string()) << " && "
       << "set +u && source oe-init-build-env " << shell_quote(build_dir.string())
       << " >/dev/null && set -u";
    std::string err;
    if (!exec("bash -c " + shell_quote(sh.str()), false, err)) { msg = err; return false; }

    if (!fs::exists(local_conf)) {
        msg = "local.conf non généré par oe-init-build-env";
        return false;
    }

    // Ajoute la configuration du lab (MACHINE, parallélisme, caches
    // partagés entre builds).
    std::ofstream out(local_conf, std::ios::app);
    if (!out) { msg = "impossible d'écrire dans local.conf"; return false; }
    out << "\n" << kLocalConfMarker << "\n";
    out << "MACHINE = \"" << config_.machine << "\"\n";
    out << "BB_NUMBER_THREADS = \"" << config_.threads << "\"\n";
    out << "PARALLEL_MAKE = \"-j " << config_.threads << "\"\n";
    out << "DL_DIR = \"" << config_.dl_dir << "\"\n";
    out << "SSTATE_DIR = \"" << config_.sstate_dir << "\"\n";
    msg = "local.conf initialisé (" + std::to_string(config_.threads) + " threads)";
    return true;
}

// --- Étape 10 : env.sh -------------------------------------------------

bool SetupRunner::step_create_env_script(std::string& msg) {
    auto path = config_.env_script_path();
    std::ofstream out(path, std::ios::trunc);
    if (!out) { msg = "écriture impossible : " + path.string(); return false; }
    out << "#!/usr/bin/env bash\n"
        << "# Généré par yocto-tui à partir de config_yocto_" << config_.name << ".ini\n"
        << "export LANG=en_US.UTF-8\n"
        << "cd " << shell_quote(config_.poky_dir().string()) << " && "
        << "source oe-init-build-env " << shell_quote(config_.build_dir) << "\n";
    out.close();

    std::error_code ec;
    fs::permissions(path,
                    fs::perms::owner_all | fs::perms::group_read | fs::perms::group_exec |
                    fs::perms::others_read | fs::perms::others_exec,
                    fs::perm_options::replace, ec);
    if (ec) log("  avertissement : chmod sur env.sh : " + ec.message());

    msg = "env.sh écrit";
    return true;
}

// --- Étape 11 : premier build ------------------------------------------

bool SetupRunner::step_first_build(std::string& msg) {
    // Idempotence : si une image a déjà été produite, on ne relance pas
    // un build de plusieurs heures par surprise.
    std::error_code ec;
    auto tmp = config_.build_dir_path() / "tmp" / "deploy" / "images" / config_.machine;
    if (fs::exists(tmp, ec)) {
        for (auto& e : fs::directory_iterator(tmp, ec)) {
            if (e.path().filename().string().find(".rootfs.") != std::string::npos) {
                msg = "une image existe déjà dans " + tmp.string();
                return true;
            }
        }
    }

    log("  (build long : comptez 1 à 3 h selon la machine et le cache)");
    std::string err;
    std::string cmd = "bash -c " + shell_quote(
        "cd " + shell_quote(config_.poky_dir().string()) + " && " +
        "source oe-init-build-env " + shell_quote(config_.build_dir) + " >/dev/null && " +
        "bitbake " + shell_quote(config_.image_recipe));

    auto started = std::chrono::steady_clock::now();
    if (!exec(cmd, false, err)) { msg = err; return false; }
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - started).count();
    msg = "build terminé en " + std::to_string(elapsed / 60) + " min";
    return true;
}

} // namespace yocto