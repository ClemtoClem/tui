/* @file model/Editor.cpp */

#include "model/Editor.hpp"

#include "core/Util.hpp"

#include <chrono>
#include <format>

namespace model {

Transaction::~Transaction() {
    if (!done_) rollback();
}

toml::Document& Transaction::doc(const fs::path& file) {
    auto it = docs_.find(file);
    if (it != docs_.end()) return it->second;
    std::error_code ec;
    if (!originals_.count(file)) {
        std::string text;
        if (fs::exists(file, ec) && util::read_file(file, text)) originals_[file] = text;
        else originals_[file] = std::nullopt;
    }
    auto& orig = originals_[file];
    auto d = toml::Document::from_text(orig ? *orig : std::string{}, file);
    return docs_.emplace(file, std::move(d)).first->second;
}

void Transaction::create_file(const fs::path& p, const std::string& content, bool overwrite) {
    std::error_code ec;
    if (fs::exists(p, ec) && !overwrite) util::fail("le fichier existe déjà : {}", util::display(p, root_));
    if (!originals_.count(p)) {
        std::string text;
        if (fs::exists(p, ec) && util::read_file(p, text)) originals_[p] = text;
        else originals_[p] = std::nullopt;
    }
    if (p.has_parent_path()) create_dir(p.parent_path());
    util::write_file(p, content);
}

void Transaction::create_dir(const fs::path& p) {
    std::error_code ec;
    if (fs::exists(p, ec)) return;
    // Mémorise le plus haut dossier créé pour pouvoir l'effacer.
    fs::path top = p;
    while (top.has_parent_path() && !fs::exists(top.parent_path(), ec)) top = top.parent_path();
    fs::create_directories(p, ec);
    if (ec) util::fail("création impossible : {} ({})", p.string(), ec.message());
    undo_.push_back([top] {
        std::error_code e;
        fs::remove_all(top, e);
    });
}

void Transaction::rename(const fs::path& from, const fs::path& to) {
    std::error_code ec;
    if (fs::exists(to, ec)) util::fail("la destination existe déjà : {}", util::display(to, root_));
    if (to.has_parent_path()) fs::create_directories(to.parent_path(), ec);
    fs::rename(from, to, ec);
    if (ec) util::fail("renommage impossible : {} → {} ({})", from.string(), to.string(), ec.message());
    // Les documents ouverts (et leurs originaux) suivent le renommage : à
    // l'annulation, ils sont restaurés au nouvel emplacement puis le dossier
    // reprend son ancien nom.
    auto inside = [&](const fs::path& p, fs::path& np) {
        auto rel = p.lexically_relative(from);
        if (rel.empty() || rel.native().starts_with("..")) return false;
        np = (to / rel).lexically_normal();
        return true;
    };
    std::map<fs::path, toml::Document> moved;
    for (auto it = docs_.begin(); it != docs_.end();) {
        fs::path np;
        if (inside(it->first, np)) {
            auto d = std::move(it->second);
            it = docs_.erase(it);
            moved.emplace(np, std::move(d));
        } else {
            ++it;
        }
    }
    for (auto& [p, d] : moved) docs_.emplace(p, std::move(d));
    std::map<fs::path, std::optional<std::string>> moved_orig;
    for (auto it = originals_.begin(); it != originals_.end();) {
        fs::path np;
        if (inside(it->first, np)) {
            moved_orig[np] = std::move(it->second);
            it = originals_.erase(it);
        } else {
            ++it;
        }
    }
    for (auto& [p, o] : moved_orig) originals_[p] = std::move(o);
    undo_.push_back([from, to] {
        std::error_code e;
        fs::rename(to, from, e);
    });
}

void Transaction::remove(const fs::path& p, bool purge) {
    std::error_code ec;
    if (!fs::exists(p, ec)) return;
    if (purge) {
        // Différé : une suppression définitive ne s'annule pas.
        purge_later_.push_back(p);
        trashed_.clear();
        return;
    }
    auto stamp = std::chrono::duration_cast<std::chrono::seconds>(
                     std::chrono::system_clock::now().time_since_epoch()).count();
    fs::path dest = root_ / ".cpp-maker-trash" / std::format("{}-{}", p.filename().string(), stamp);
    rename(p, dest);
    trashed_ = dest;
}

std::vector<fs::path> Transaction::touched_files() const {
    std::vector<fs::path> r;
    for (auto& [p, d] : docs_)
        if (d.modified()) r.push_back(p);
    return r;
}

Workspace Transaction::commit(bool validate) {
    try {
        for (auto& [p, d] : docs_)
            if (d.modified()) d.save(p);
        Workspace ws = Workspace::load_root(root_, {.resolve_git = false, .update_git = false, .validate = validate});
        for (auto& p : purge_later_) {
            std::error_code ec;
            fs::remove_all(p, ec);
        }
        done_ = true;
        return ws;
    } catch (...) {
        rollback();
        throw;
    }
}

void Transaction::rollback() {
    if (done_) return;
    done_ = true;
    for (auto& [p, orig] : originals_) {
        std::error_code ec;
        if (orig) util::write_file(p, *orig);
        else fs::remove(p, ec);
    }
    for (auto it = undo_.rbegin(); it != undo_.rend(); ++it) (*it)();
}

std::vector<std::string> get_list(const toml::Document& d, const std::vector<std::string>& table, const std::string& key) {
    auto* v = d.get(table, key);
    if (!v) return {};
    bool ok = false;
    auto r = v->as_strings(&ok);
    return ok ? r : std::vector<std::string>{};
}

} // namespace model
