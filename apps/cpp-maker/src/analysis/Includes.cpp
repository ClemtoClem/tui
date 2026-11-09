/* @file analysis/Includes.cpp */

#include "analysis/Includes.hpp"

#include "build/Planner.hpp"
#include "core/Util.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace analysis {

std::vector<std::string> scan_includes(const fs::path& file) {
    std::vector<std::string> out;
    std::string content;
    if (!util::read_file(file, content)) return out;
    size_t i = 0;
    while ((i = content.find("#include", i)) != std::string::npos) {
        size_t j = i + 8;
        while (j < content.size() && (content[j] == ' ' || content[j] == '\t')) ++j;
        if (j >= content.size()) break;
        char open = content[j];
        if (open != '"' && open != '<') {
            i += 8;
            continue;
        }
        char close = open == '"' ? '"' : '>';
        auto end = content.find(close, j + 1);
        if (end == std::string::npos || end > content.find('\n', j)) {
            i = j + 1;
            continue;
        }
        out.push_back(content.substr(j + 1, end - j - 1));
        i = end + 1;
    }
    return out;
}

namespace {

bool is_header(const fs::path& p) {
    static const std::set<std::string> ext = {".h", ".hpp", ".hh", ".hxx", ".h++", ".ipp", ".tpp", ".inl"};
    return ext.count(p.extension().string()) > 0;
}

} // namespace

std::vector<MissingDep> detect_missing_deps(const model::Workspace& ws) {
    // En-tête public (chemin relatif à son dossier d'inclusion) → module.
    std::map<std::string, std::string> index;
    std::error_code ec;
    for (auto& t : ws.targets) {
        if (!t.is_module()) continue;
        for (auto& inc : t.public_includes) {
            fs::path root = util::under(ws.abs(t), inc);
            if (!fs::is_directory(root, ec)) continue;
            for (auto& e : fs::recursive_directory_iterator(root, fs::directory_options::skip_permission_denied, ec)) {
                if (ec) break;
                if (e.is_regular_file(ec)) index.emplace(e.path().lexically_relative(root).generic_string(), t.id);
            }
        }
    }
    std::vector<MissingDep> out;
    std::set<std::pair<std::string, std::string>> seen;
    for (auto& t : ws.targets) {
        if (t.from_git) continue;
        std::set<std::string> reachable;
        try {
            for (auto& d : ws.closure(t.id)) reachable.insert(d);
        } catch (const util::Error&) {
            continue;   // cycle : signalé par la validation
        }
        std::vector<fs::path> files;
        for (auto& s : build::discover_sources(ws, t, t.source_dirs)) files.push_back(s.path);
        std::vector<std::string> hdr_dirs = t.public_includes;
        hdr_dirs.insert(hdr_dirs.end(), t.private_includes.begin(), t.private_includes.end());
        for (auto& d : hdr_dirs) {
            fs::path dir = util::under(ws.abs(t), d);
            if (!fs::is_directory(dir, ec)) continue;
            for (auto& e : fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec)) {
                if (ec) break;
                if (e.is_regular_file(ec) && is_header(e.path())) files.push_back(e.path());
            }
        }
        std::sort(files.begin(), files.end());
        files.erase(std::unique(files.begin(), files.end()), files.end());
        for (auto& f : files) {
            for (auto& inc : scan_includes(f)) {
                auto it = index.find(inc);
                if (it == index.end() || it->second == t.id || reachable.count(it->second)) continue;
                if (!seen.insert({t.id, it->second}).second) continue;
                out.push_back({t.id, it->second, inc, f});
            }
        }
    }
    return out;
}

} // namespace analysis
