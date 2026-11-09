/* @file io/args.cpp */

#include "io/args.hpp"

#include "core/Util.hpp"

#include <algorithm>
#include <cctype>
#include <format>

namespace args {

namespace detail {

bool parse_value(const std::string& s, bool& out) {
    std::string v = util::lower(s);
    if (v == "true" || v == "1" || v == "yes" || v == "on" || v == "oui") { out = true; return true; }
    if (v == "false" || v == "0" || v == "no" || v == "off" || v == "non") { out = false; return true; }
    return false;
}

bool parse_value(const std::string& s, int& out) {
    try {
        size_t p = 0;
        int v = std::stoi(s, &p);
        if (p != s.size()) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_value(const std::string& s, long long& out) {
    try {
        size_t p = 0;
        long long v = std::stoll(s, &p);
        if (p != s.size()) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_value(const std::string& s, double& out) {
    try {
        size_t p = 0;
        double v = std::stod(s, &p);
        if (p != s.size()) return false;
        out = v;
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_value(const std::string& s, std::string& out) {
    out = s;
    return true;
}

bool parse_value(const std::string& s, std::vector<std::string>& out) {
    out = util::split_list(s);
    return true;
}

} // namespace detail

// ============================================================================

ArgumentBuilder Arguments::add(const std::string& name) {
    Entry e;
    e.name = name.starts_with("-") ? name : "--" + name;
    entries_.push_back(std::move(e));
    return ArgumentBuilder(*this, entries_.size() - 1);
}

const Arguments::Entry* Arguments::find(const std::string& name) const {
    std::string n = name.starts_with("-") ? name : "--" + name;
    for (auto& e : entries_) {
        if (e.name == n) return &e;
        for (auto& f : e.flags)
            if (f == n) return &e;
    }
    return nullptr;
}

Arguments::Entry* Arguments::find_by_flag(const std::string& flag) {
    for (auto& e : entries_) {
        if (e.name == flag) return &e;
        for (auto& f : e.flags)
            if (f == flag) return &e;
    }
    return nullptr;
}

bool Arguments::is_set(const std::string& name) const {
    const Entry* e = find(name);
    return e && e->explicitly_set;
}

std::string Arguments::unknown_option_message(const std::string& flag) const {
    std::vector<std::string> names;
    for (auto& e : entries_) {
        if (e.hidden) continue;
        names.push_back(e.name);
        for (auto& f : e.flags) names.push_back(f);
    }
    std::string msg = "option inconnue : " + flag;
    if (auto s = util::suggest(flag, names); !s.empty()) msg += " (vouliez-vous dire " + s + " ?)";
    return msg;
}

void Arguments::apply_value(Entry& e, const std::string& flag, const std::string& value) {
    if (e.is_list()) {
        auto cur = std::any_cast<std::vector<std::string>>(e.explicitly_set ? e.value : std::any(std::vector<std::string>{}));
        for (auto& v : util::split_list(value)) cur.push_back(v);
        e.value = cur;
        e.explicitly_set = true;
        return;
    }
    if (e.explicitly_set && !e.is_flag())
        throw Exception(ErrorCode::DuplicatedOption, "option répétée : " + flag);
    std::any nv;
    if (!e.parser(value, nv))
        throw Exception(ErrorCode::InvalidValue, std::format("valeur invalide pour {} : '{}'", flag, value));
    e.value = std::move(nv);
    e.explicitly_set = true;
}

void Arguments::parse(int argc, char* argv[]) {
    std::vector<std::string> a;
    for (int i = 1; i < argc; ++i) a.emplace_back(argv[i]);
    parse(a);
}

void Arguments::parse(const std::vector<std::string>& argv) {
    for (size_t i = 0; i < argv.size(); ++i) {
        const std::string& a = argv[i];
        if (a == "--") {
            double_dash_ = true;
            trailing_.assign(argv.begin() + static_cast<long>(i) + 1, argv.end());
            break;
        }
        if (a.size() < 2 || a[0] != '-' || (std::isdigit(static_cast<unsigned char>(a[1])) && !find_by_flag(a.substr(0, 2)))) {
            words_.push_back(a);
            continue;
        }
        if (a[1] == '-') {
            size_t eq = a.find('=');
            std::string name = eq == std::string::npos ? a : a.substr(0, eq);
            Entry* e = find_by_flag(name);
            if (!e) throw Exception(ErrorCode::UnknownOption, unknown_option_message(name));
            if (eq != std::string::npos) {
                if (e->is_flag()) apply_value(*e, name, a.substr(eq + 1));
                else apply_value(*e, name, a.substr(eq + 1));
            } else if (e->is_flag()) {
                e->value = true;
                e->explicitly_set = true;
            } else {
                if (i + 1 >= argv.size())
                    throw Exception(ErrorCode::MissingValue, "valeur manquante pour " + name);
                apply_value(*e, name, argv[++i]);
            }
            continue;
        }
        // Grappe d'options courtes : -v, -vk, -j8, -j=8, -m core
        std::string rest = a.substr(1);
        for (size_t j = 0; j < rest.size(); ++j) {
            std::string flag = std::string("-") + rest[j];
            Entry* e = find_by_flag(flag);
            if (!e) throw Exception(ErrorCode::UnknownOption, unknown_option_message(flag));
            if (e->is_flag()) {
                e->value = true;
                e->explicitly_set = true;
                continue;
            }
            std::string value = rest.substr(j + 1);
            if (!value.empty() && value[0] == '=') value = value.substr(1);
            if (value.empty()) {
                if (i + 1 >= argv.size())
                    throw Exception(ErrorCode::MissingValue, "valeur manquante pour " + flag);
                value = argv[++i];
            }
            apply_value(*e, flag, value);
            break;
        }
    }
    for (auto& e : entries_)
        if (e.required && !e.explicitly_set)
            throw Exception(ErrorCode::MissingRequiredOption, "option obligatoire manquante : " + e.name);
}

std::vector<std::string> Arguments::positionals() const {
    if (dispatch_depth_ >= words_.size()) return {};
    return {words_.begin() + static_cast<long>(dispatch_depth_), words_.end()};
}

std::string Arguments::help() const {
    struct Row { std::string left, right; };
    std::vector<Row> rows;
    size_t width = 0;
    for (auto& e : entries_) {
        if (e.hidden) continue;
        std::vector<std::string> names;
        for (auto& f : e.flags)
            if (f.size() == 2) names.push_back(f);
        names.push_back(e.name);
        for (auto& f : e.flags)
            if (f.size() > 2) names.push_back(f);
        std::string left = util::join(names, ", ");
        if (!e.is_flag()) left += " " + (e.metavar.empty() ? std::string("VALEUR") : e.metavar);
        std::string right = e.help;
        if (e.required) right += " (obligatoire)";
        width = std::max(width, left.size());
        rows.push_back({left, right});
    }
    std::string out;
    for (auto& r : rows) {
        out += "  " + r.left + std::string(width - r.left.size() + 3, ' ') + r.right + "\n";
    }
    return out;
}

ArgumentBuilder& ArgumentBuilder::flags(const std::string& f) {
    auto& e = entry();
    for (auto& tok : util::split_list(util::replace_all(f, " ", ","))) {
        if (tok.starts_with("--")) e.flags.push_back(tok);
        else if (tok.starts_with("-") && tok.size() == 2) e.flags.push_back(tok);
        else if (tok.starts_with("-"))
            for (size_t k = 1; k < tok.size(); ++k) e.flags.push_back(std::string("-") + tok[k]);
        else
            for (char c : tok) e.flags.push_back(std::string("-") + c);
    }
    return *this;
}

} // namespace args
