/* @file vcs/Blame.cpp */

#include "vcs/Blame.hpp"

#include "core/Process.hpp"
#include "core/Util.hpp"

#include <cstdlib>
#include <ctime>
#include <map>
#include <vector>

namespace vcs {

namespace {

/// Vrai si `s` est 40 caractères hexadécimaux.
bool is_hex40(const std::string& s) {
    if (s.size() != 40) return false;
    for (char c : s)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    return true;
}

/// Premier champ d'un en-tête de hunk blame : `^?[0-9a-f]{40}`.
bool is_header_token(const std::string& t) {
    return (t.size() == 40 && is_hex40(t)) || (t.size() == 41 && t[0] == '^' && is_hex40(t.substr(1)));
}

/// Convertit un timestamp unix en « AAAA-MM-JJ » (fuseau local).
std::string format_date(long ts) {
    std::time_t t = static_cast<std::time_t>(ts);
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    return buf;
}

} // namespace

std::vector<BlameLine> blame(const fs::path& root, const std::string& file) {
    std::vector<BlameLine> out;
    std::vector<std::string> cmd = {"git", "-C", root.string(), "blame", "--porcelain", "--", file};
    auto r = proc::capture(cmd);
    if (r.exit_code != 0) return out;

    // `--porcelain` n'imprime les métadonnées (author, author-time) qu'à la
    // PREMIÈRE occurrence d'un commit ; on les mémorise par hash pour les
    // réutiliser quand le même commit réapparaît plus bas dans le fichier.
    struct Meta { std::string author, date; };
    std::map<std::string, Meta> meta;

    std::string hash, author, date;
    for (auto& line : util::split(r.output, '\n', true)) {
        if (line.empty()) continue;
        if (line[0] == '\t') {
            // Ligne de contenu : elle appartient au commit courant.
            BlameLine b;
            b.hash = hash.size() > 8 ? hash.substr(0, 8) : hash;
            b.author = author;
            b.date = date;
            b.content = line.substr(1);
            out.push_back(std::move(b));
            continue;
        }
        auto sp = line.find(' ');
        std::string key = sp == std::string::npos ? line : line.substr(0, sp);
        std::string val = sp == std::string::npos ? std::string() : line.substr(sp + 1);
        if (is_header_token(key)) {
            hash = (key[0] == '^') ? key.substr(1) : key;
            auto it = meta.find(hash);
            author = it == meta.end() ? std::string() : it->second.author;
            date = it == meta.end() ? std::string() : it->second.date;
        } else if (key == "author") {
            author = val;
            meta[hash].author = val;
        } else if (key == "author-time") {
            date = format_date(std::atol(val.c_str()));
            meta[hash].date = date;
        }
        // author-mail, committer*, summary, filename, boundary : ignorés.
    }
    return out;
}

} // namespace vcs
