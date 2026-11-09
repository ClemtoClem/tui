/* @file project/PlanApplier.cpp */

#include "project/PlanApplier.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <set>
#include <sstream>

namespace yocto {

namespace fs = std::filesystem;

namespace {

constexpr std::uintmax_t kMaxPreviewBytes = 1024 * 1024;

unsigned file_mode(const fs::path& p) {
    std::error_code ec;
    auto st = fs::status(p, ec);
    if (ec) return 0;
    return static_cast<unsigned>(st.permissions() & fs::perms::mask) & 0777u;
}

bool looks_binary(const std::string& s) {
    return s.find('\0') != std::string::npos;
}

std::string rel_display(const fs::path& p, const fs::path& base) {
    std::error_code ec;
    auto rel = fs::relative(p, base, ec);
    return ec || rel.empty() ? p.string() : rel.string();
}

/// Écrit `content` via un fichier temporaire puis rename : un fichier de
/// configuration n'est jamais laissé tronqué.
bool write_atomic(const fs::path& p, const std::string& content, unsigned mode, std::string& err) {
    std::error_code ec;
    fs::create_directories(p.parent_path(), ec);
    if (ec) { err = "création de " + p.parent_path().string() + " : " + ec.message(); return false; }
    fs::path tmp = p;
    tmp += ".yocto-tui.tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) { err = "écriture impossible : " + tmp.string(); return false; }
        out << content;
        if (!out) { err = "erreur d'écriture : " + tmp.string(); return false; }
    }
    fs::permissions(tmp, static_cast<fs::perms>(mode), fs::perm_options::replace, ec);
    fs::rename(tmp, p, ec);
    if (ec) { err = "renommage vers " + p.string() + " : " + ec.message(); return false; }
    return true;
}

/// Supprime les répertoires devenus vides entre `dir` et `stop` (exclu).
void prune_empty_dirs(fs::path dir, const fs::path& stop) {
    std::error_code ec;
    while (!dir.empty() && dir != stop && dir.string().size() > stop.string().size()) {
        if (!fs::is_directory(dir, ec) || !fs::is_empty(dir, ec)) break;
        fs::remove(dir, ec);
        dir = dir.parent_path();
    }
}

std::vector<std::string> split_lines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start < s.size()) {
        size_t nl = s.find('\n', start);
        if (nl == std::string::npos) { out.push_back(s.substr(start)); break; }
        out.push_back(s.substr(start, nl - start));
        start = nl + 1;
    }
    return out;
}

} // namespace

const char* status_icon(EntryStatus s) {
    switch (s) {
        case EntryStatus::New:       return "+";
        case EntryStatus::Modified:  return "~";
        case EntryStatus::Unchanged: return "=";
        case EntryStatus::Removed:   return "-";
        case EntryStatus::Missing:   return "!";
    }
    return "?";
}

const char* status_text(EntryStatus s) {
    switch (s) {
        case EntryStatus::New:       return "nouveau";
        case EntryStatus::Modified:  return "modifié";
        case EntryStatus::Unchanged: return "identique";
        case EntryStatus::Removed:   return "supprimé";
        case EntryStatus::Missing:   return "absent (build non initialisé)";
    }
    return "?";
}

std::string read_file(const fs::path& p, bool* exists) {
    std::ifstream in(p, std::ios::binary);
    if (exists) *exists = static_cast<bool>(in);
    if (!in) return "";
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::vector<std::string> read_manifest(const fs::path& layer_dir) {
    std::vector<std::string> out;
    std::istringstream in(read_file(layer_dir / kManifestName));
    std::string line;
    while (std::getline(in, line)) {
        // Garde-fou : jamais de chemin absolu ni de remontée hors du layer.
        if (line.empty() || line.front() == '#' || line.front() == '/' ||
            line.find("..") != std::string::npos) {
            continue;
        }
        out.push_back(line);
    }
    return out;
}

// --- Aperçu ----------------------------------------------------------------

std::vector<PreviewEntry> preview_layer(const GenerationPlan& plan) {
    std::vector<PreviewEntry> out;
    std::set<std::string> produced;

    for (const auto& f : plan.files) {
        PreviewEntry e;
        e.path = f.path;
        e.display = rel_display(f.path, plan.layer_dir);
        e.new_content = f.content;
        e.mode = f.mode;
        e.origin = f.origin;
        bool exists = false;
        e.old_content = read_file(f.path, &exists);
        if (!exists) e.status = EntryStatus::New;
        else if (e.old_content != e.new_content || file_mode(f.path) != (f.mode & 0777u)) e.status = EntryStatus::Modified;
        else e.status = EntryStatus::Unchanged;
        produced.insert(e.display);
        out.push_back(std::move(e));
    }

    for (const auto& c : plan.copies) {
        std::error_code ec;
        if (!fs::is_directory(c.from_dir, ec)) continue;
        fs::recursive_directory_iterator it(c.from_dir, fs::directory_options::skip_permission_denied, ec);
        for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
            const auto& entry = *it;
            const std::string name = entry.path().filename().string();
            if (entry.is_directory() && (name == ".git" || name == "build")) {
                it.disable_recursion_pending();
                continue;
            }
            if (!entry.is_regular_file()) continue;

            PreviewEntry e;
            e.copy_from = entry.path();
            e.path = c.to_dir / fs::relative(entry.path(), c.from_dir);
            e.display = rel_display(e.path, plan.layer_dir);
            e.mode = file_mode(entry.path());
            e.origin = c.origin + " (copie de " + c.from_dir.string() + ")";
            const auto size = entry.file_size();
            if (size > kMaxPreviewBytes) {
                e.binary = true;
            } else {
                e.new_content = read_file(entry.path());
                e.binary = looks_binary(e.new_content);
            }
            bool exists = false;
            if (e.binary) {
                std::error_code ec2;
                exists = fs::exists(e.path, ec2);
                const bool same = exists && fs::file_size(e.path, ec2) == size &&
                                  read_file(e.path) == read_file(entry.path());
                e.status = !exists ? EntryStatus::New : (same ? EntryStatus::Unchanged : EntryStatus::Modified);
                e.new_content = "(fichier binaire ou volumineux, " + std::to_string(size) + " octets)";
            } else {
                e.old_content = read_file(e.path, &exists);
                if (!exists) e.status = EntryStatus::New;
                else if (e.old_content != e.new_content || file_mode(e.path) != e.mode) e.status = EntryStatus::Modified;
                else e.status = EntryStatus::Unchanged;
            }
            produced.insert(e.display);
            out.push_back(std::move(e));
        }
    }

    for (const auto& rel : read_manifest(plan.layer_dir)) {
        if (produced.count(rel)) continue;
        const fs::path p = plan.layer_dir / rel;
        std::error_code ec;
        if (!fs::exists(p, ec)) continue;
        PreviewEntry e;
        e.path = p;
        e.display = rel;
        e.status = EntryStatus::Removed;
        e.old_content = read_file(p);
        e.origin = "n'est plus produit par le projet";
        out.push_back(std::move(e));
    }

    std::sort(out.begin(), out.end(), [](const PreviewEntry& a, const PreviewEntry& b) {
        return a.display < b.display;
    });
    return out;
}

std::vector<PreviewEntry> preview_build_conf(const GenerationPlan& plan) {
    std::vector<PreviewEntry> out;
    for (const auto& edit : plan.edits) {
        PreviewEntry e;
        e.path = edit.path;
        e.display = edit.path.filename().string();
        e.origin = edit.origin;
        e.in_layer = false;
        bool exists = false;
        e.old_content = read_file(edit.path, &exists);
        e.new_content = render_managed_edit(edit, e.old_content);
        e.mode = exists ? file_mode(edit.path) : 0644;
        if (!exists) e.status = EntryStatus::Missing;
        else e.status = e.old_content == e.new_content ? EntryStatus::Unchanged : EntryStatus::Modified;
        out.push_back(std::move(e));
    }
    return out;
}

// --- Application -----------------------------------------------------------

ApplyReport apply_layer(const GenerationPlan& plan, bool dry_run, const LogFn& log) {
    ApplyReport rep;
    const auto entries = preview_layer(plan);
    const char* prefix = dry_run ? "  [simulation] " : "  ";

    std::vector<std::string> manifest;
    for (const auto& e : entries) {
        std::string err;
        switch (e.status) {
            case EntryStatus::Unchanged:
                ++rep.unchanged;
                manifest.push_back(e.display);
                break;
            case EntryStatus::New:
            case EntryStatus::Modified: {
                manifest.push_back(e.display);
                if (log) log(std::string(prefix) + status_icon(e.status) + " " + e.display);
                ++rep.written;
                if (dry_run) break;
                bool ok = true;
                if (!e.copy_from.empty()) {
                    std::error_code ec;
                    fs::create_directories(e.path.parent_path(), ec);
                    fs::copy_file(e.copy_from, e.path, fs::copy_options::overwrite_existing, ec);
                    if (ec) { err = "copie " + e.copy_from.string() + " : " + ec.message(); ok = false; }
                    else fs::permissions(e.path, static_cast<fs::perms>(e.mode), fs::perm_options::replace, ec);
                } else {
                    ok = write_atomic(e.path, e.new_content, e.mode, err);
                }
                if (!ok) { rep.errors.push_back(err); --rep.written; }
                break;
            }
            case EntryStatus::Removed: {
                if (log) log(std::string(prefix) + "- " + e.display);
                ++rep.removed;
                if (dry_run) break;
                std::error_code ec;
                fs::remove(e.path, ec);
                if (ec) rep.errors.push_back("suppression de " + e.display + " : " + ec.message());
                else prune_empty_dirs(e.path.parent_path(), plan.layer_dir);
                break;
            }
            case EntryStatus::Missing:
                break;
        }
    }

    if (!dry_run) {
        std::sort(manifest.begin(), manifest.end());
        std::string content = "# Fichiers générés par yocto-tui (supprimés s'ils ne sont plus produits)\n";
        for (const auto& m : manifest) content += m + "\n";
        std::string err;
        if (!write_atomic(plan.layer_dir / kManifestName, content, 0644, err)) rep.errors.push_back(err);
    }
    return rep;
}

ApplyReport apply_build_conf(const GenerationPlan& plan, bool dry_run, const LogFn& log) {
    ApplyReport rep;
    const char* prefix = dry_run ? "  [simulation] " : "  ";
    for (const auto& e : preview_build_conf(plan)) {
        switch (e.status) {
            case EntryStatus::Missing:
                if (dry_run) {
                    // En simulation on continue : le bloc sera ajouté une
                    // fois le build initialisé.
                    if (log) log(std::string(prefix) + "! " + e.path.string() + " absent (build à initialiser)");
                    break;
                }
                rep.errors.push_back(e.path.string() + " absent : initialisez le répertoire de build "
                                     "(onglet 1, étape « Répertoire de build »)");
                break;
            case EntryStatus::Unchanged:
                ++rep.unchanged;
                if (log) log(std::string(prefix) + "= " + e.path.string());
                break;
            default: {
                if (log) log(std::string(prefix) + "~ " + e.path.string());
                ++rep.written;
                if (dry_run) break;
                std::string err;
                if (!write_atomic(e.path, e.new_content, e.mode ? e.mode : 0644, err)) {
                    rep.errors.push_back(err);
                    --rep.written;
                }
                break;
            }
        }
    }
    return rep;
}

// --- Diff --------------------------------------------------------------------

std::vector<DiffLine> line_diff(const std::string& a, const std::string& b, size_t max_lines) {
    const auto la = split_lines(a);
    const auto lb = split_lines(b);
    std::vector<DiffLine> out;

    size_t pre = 0;
    while (pre < la.size() && pre < lb.size() && la[pre] == lb[pre]) ++pre;
    size_t suf = 0;
    while (suf < la.size() - pre && suf < lb.size() - pre &&
           la[la.size() - 1 - suf] == lb[lb.size() - 1 - suf]) {
        ++suf;
    }

    for (size_t i = 0; i < pre; ++i) out.push_back({' ', la[i]});

    const size_t n = la.size() - pre - suf;
    const size_t m = lb.size() - pre - suf;
    if (n > max_lines || m > max_lines) {
        for (size_t i = 0; i < n; ++i) out.push_back({'-', la[pre + i]});
        for (size_t j = 0; j < m; ++j) out.push_back({'+', lb[pre + j]});
    } else {
        // Table LCS (n+1)×(m+1), remplie depuis la fin.
        std::vector<std::uint16_t> t((n + 1) * (m + 1), 0);
        auto at = [&](size_t i, size_t j) -> std::uint16_t& { return t[i * (m + 1) + j]; };
        for (size_t i = n; i-- > 0;) {
            for (size_t j = m; j-- > 0;) {
                at(i, j) = la[pre + i] == lb[pre + j]
                    ? static_cast<std::uint16_t>(at(i + 1, j + 1) + 1)
                    : std::max(at(i + 1, j), at(i, j + 1));
            }
        }
        size_t i = 0;
        size_t j = 0;
        while (i < n && j < m) {
            if (la[pre + i] == lb[pre + j]) { out.push_back({' ', la[pre + i]}); ++i; ++j; }
            else if (at(i + 1, j) >= at(i, j + 1)) { out.push_back({'-', la[pre + i]}); ++i; }
            else { out.push_back({'+', lb[pre + j]}); ++j; }
        }
        for (; i < n; ++i) out.push_back({'-', la[pre + i]});
        for (; j < m; ++j) out.push_back({'+', lb[pre + j]});
    }

    for (size_t k = la.size() - suf; k < la.size(); ++k) out.push_back({' ', la[k]});
    return out;
}

std::string format_diff(const std::vector<DiffLine>& diff, size_t context) {
    std::vector<bool> keep(diff.size(), false);
    bool any = false;
    for (size_t i = 0; i < diff.size(); ++i) {
        if (diff[i].op == ' ') continue;
        any = true;
        const size_t lo = i >= context ? i - context : 0;
        const size_t hi = std::min(diff.size(), i + context + 1);
        for (size_t k = lo; k < hi; ++k) keep[k] = true;
    }
    if (!any) return "(aucune différence)\n";

    std::string out;
    bool skipped = false;
    for (size_t i = 0; i < diff.size(); ++i) {
        if (!keep[i]) { skipped = true; continue; }
        if (skipped) { out += "  ...\n"; skipped = false; }
        out += diff[i].op;
        out += ' ';
        out += diff[i].text;
        out += '\n';
    }
    if (skipped) out += "  ...\n";
    return out;
}

} // namespace yocto
