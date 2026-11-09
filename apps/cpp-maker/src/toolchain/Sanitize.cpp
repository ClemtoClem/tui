/* @file toolchain/Sanitize.cpp */

#include "toolchain/Sanitize.hpp"

#include "core/Term.hpp"
#include "core/Util.hpp"
#include "toolchain/Toolchain.hpp"

#include <algorithm>
#include <map>
#include <set>

namespace sanitize {

const std::vector<Info>& catalog() {
    static const std::vector<Info> c = {
        {"address", "AddressSanitizer (ASan)",
         "Accès mémoire invalides : débordements de tas, de pile et de globales, use-after-free, "
         "double free ; inclut la détection de fuites sous Linux. Coût ~2x en temps, ~3x en mémoire.",
         false, true},
        {"thread", "ThreadSanitizer (TSan)",
         "Accès concurrents non synchronisés (data races) et certains interblocages. Coût 5 à 15x.",
         false, true},
        {"memory", "MemorySanitizer (MSan) — clang uniquement",
         "Lectures de mémoire non initialisée. Tout le code doit être instrumenté (libc++ comprise).",
         true, true},
        {"leak", "LeakSanitizer (LSan)",
         "Fuites mémoire en fin de programme, sans le surcoût d'ASan ; combinable avec address seulement.",
         false, true},
        {"hwaddress", "HWASan",
         "ASan assisté par le matériel (marquage des pointeurs). AArch64/Android.", false, true},
        {"safe-stack", "SafeStack — clang uniquement",
         "Sépare la pile en une pile sûre et une pile non sûre. Protection de production.", true, true},
        {"undefined", "UBSan (groupe général)",
         "shift, integer-divide-by-zero, null, signed-integer-overflow, bounds, alignment, vptr…",
         false, false},
        {"shift", "UBSan", "Décalages de bits invalides.", false, false},
        {"integer-divide-by-zero", "UBSan", "Divisions entières par zéro.", false, false},
        {"float-divide-by-zero", "UBSan", "Divisions flottantes par zéro (hors 'undefined').", false, false},
        {"null", "UBSan", "Déréférencement de pointeur nul.", false, false},
        {"signed-integer-overflow", "UBSan", "Dépassement des entiers signés.", false, false},
        {"unsigned-integer-overflow", "UBSan — clang uniquement",
         "Dépassement des entiers non signés (pas un UB : utile pour traquer des bugs logiques).", true, false},
        {"bounds", "UBSan", "Accès hors limites de tableaux de taille connue.", false, false},
        {"alignment", "UBSan", "Pointeurs mal alignés.", false, false},
        {"vptr", "UBSan — C++", "Type dynamique incorrect (appels virtuels, dynamic_cast).", false, false},
        {"cfi", "Control Flow Integrity — clang uniquement",
         "Vérifie les cibles des appels indirects ; ajoute -flto -fvisibility=hidden.", true, false},
        {"shadow-call-stack", "ShadowCallStack",
         "Adresse de retour dans une pile dédiée. AArch64 et RISC-V uniquement.", false, false},
    };
    return c;
}

const Info* find(std::string_view name) {
    for (auto& i : catalog())
        if (i.name == name) return &i;
    return nullptr;
}

std::string normalize(std::string_view raw) {
    std::string s(raw);
    for (std::string_view pre : {"-fsanitize=", "fsanitize="})
        if (s.starts_with(pre)) {
            s = s.substr(pre.size());
            break;
        }
    static const std::map<std::string, std::string> aliases = {
        {"asan", "address"}, {"tsan", "thread"}, {"msan", "memory"}, {"lsan", "leak"},
        {"ubsan", "undefined"}, {"hwasan", "hwaddress"}, {"safestack", "safe-stack"},
    };
    if (auto it = aliases.find(s); it != aliases.end()) s = it->second;
    return s;
}

namespace {
std::vector<std::string> parse_list(const std::vector<std::string>& raw, std::string_view opt, bool allow_all) {
    std::vector<std::string> r;
    for (auto& item : raw)
        for (auto& part : util::split_list(item)) {
            std::string n = normalize(part);
            if (n == "all" && !allow_all) util::fail("{} all : une passe par runtime ; non combinable ici", opt);
            if (n != "all" && !find(n))
                util::fail("{} : sanitizer inconnu '{}' (cpp-maker help sanitize)", opt, part);
            util::push_unique(r, n);
        }
    return r;
}
} // namespace

std::vector<std::string> from_mode(const model::ModeConfig& m) {
    std::vector<std::string> r;
    auto add = [&](std::string_view list) {
        for (auto& p : util::split_list(list)) {
            std::string n = normalize(p);
            if (!n.empty()) util::push_unique(r, n);
        }
    };
    add(m.sanitizer);
    for (auto& f : m.flags)
        if (f.starts_with("-fsanitize=")) add(std::string_view(f).substr(11));
    return r;
}

std::string check_compatible(const std::vector<std::string>& sans) {
    static const std::set<std::string> exclusive = {"address", "hwaddress", "thread", "memory", "safe-stack"};
    std::vector<std::string> ex;
    for (auto& s : sans)
        if (exclusive.count(s)) ex.push_back(s);
    if (ex.size() > 1)
        return std::format("sanitizers incompatibles : {} (un seul runtime parmi address, hwaddress, "
                           "thread, memory, safe-stack)", util::join(ex, " + "));
    bool leak = std::find(sans.begin(), sans.end(), "leak") != sans.end();
    if (leak && !ex.empty() && ex[0] != "address")
        return std::format("sanitizers incompatibles : leak + {} (leak ne se combine qu'avec address)", ex[0]);
    return {};
}

bool wants_all(const Request& r) {
    for (auto& item : r.sanitize)
        for (auto& part : util::split_list(item))
            if (normalize(part) == "all") return true;
    return false;
}

std::vector<Request> expand_all(const Request& req, const model::ModeConfig& base, const toolchain::Toolchain& tc) {
    if (!wants_all(req)) return {req};
    static const std::vector<std::vector<std::string>> runtimes = {
        {"address", "undefined"}, {"thread"}, {"leak"}, {"memory"}, {"hwaddress"}};
    std::vector<std::string> extra;
    for (auto& item : req.sanitize)
        for (auto& part : util::split_list(item))
            if (auto n = normalize(part); n != "all") util::push_unique(extra, n);

    bool clang = tc.family() == toolchain::Family::Clang;
    bool hw_arch = tc.arch.starts_with("aarch64") || tc.arch.starts_with("riscv");
    std::vector<Request> out;
    for (auto& rt : runtimes) {
        auto pass = rt;
        util::push_unique(pass, extra);
        std::string why;
        if (!clang)
            for (auto& s : pass)
                if (auto* i = find(s); i && i->clang_only) { why = s + " nécessite clang"; break; }
        if (why.empty() && rt[0] == "hwaddress" && !hw_arch) why = "architecture " + tc.arch;
        if (why.empty()) {
            auto all = from_mode(base);
            util::push_unique(all, pass);
            why = check_compatible(all);
        }
        if (!why.empty()) {
            term::warn("--sanitize all : passe {} ignorée ({})", rt[0], why);
            continue;
        }
        Request r = req;
        r.sanitize = {util::join(pass, ",")};
        out.push_back(std::move(r));
    }
    if (out.empty()) util::fail("--sanitize all : aucune passe possible avec la toolchain '{}'", tc.name);
    return out;
}

model::ModeConfig apply(const model::ModeConfig& base, const Request& r, const toolchain::Toolchain& tc) {
    model::ModeConfig m = base;
    auto extra = parse_list(r.sanitize, "--sanitize", false);
    auto trap = parse_list(r.trap, "--sanitize-trap", true);
    auto norec = parse_list(r.no_recover, "--no-sanitize-recover", true);

    auto all = from_mode(base);
    std::vector<std::string> added;
    for (auto& s : extra)
        if (util::push_unique(all, s)) added.push_back(s);
    if (auto err = check_compatible(all); !err.empty()) util::fail("mode '{}' : {}", base.name, err);

    if (tc.family() != toolchain::Family::Clang) {
        std::vector<std::string> clang_only;
        for (auto& s : all)
            if (auto* i = find(s); i && i->clang_only) clang_only.push_back(s);
        if (!r.ignorelist.empty()) clang_only.push_back("--sanitize-ignorelist");
        if (!clang_only.empty())
            util::fail("{} : nécessite clang (toolchain '{}') ; essayez -t clang", util::join(clang_only, ", "), tc.name);
    }
    if (!r.any()) return base;

    auto push = [](std::vector<std::string>& v, const std::string& f) { util::push_unique(v, f); };
    std::erase_if(m.flags, [](auto& f) { return f.starts_with("-fsanitize="); });
    std::erase_if(m.link_flags, [](auto& f) { return f.starts_with("-fsanitize="); });
    if (!all.empty()) {
        std::string list = "-fsanitize=" + util::join(all, ",");
        m.flags.push_back(list);
        m.link_flags.push_back(list);
        m.sanitizer = util::join(all, ",");
        push(m.flags, "-fno-omit-frame-pointer");
        if (std::none_of(m.flags.begin(), m.flags.end(), [](auto& f) { return f.starts_with("-g"); }))
            m.flags.push_back("-g");
    }
    auto has = [&](std::string_view s) { return std::find(all.begin(), all.end(), s) != all.end(); };
    if (has("memory")) push(m.flags, "-fsanitize-memory-track-origins=2");
    if (has("cfi")) {
        push(m.flags, "-flto");
        push(m.flags, "-fvisibility=hidden");
        push(m.link_flags, "-flto");
    }
    if (!trap.empty()) {
        m.flags.push_back("-fsanitize-trap=" + util::join(trap, ","));
        m.link_flags.push_back("-fsanitize-trap=" + util::join(trap, ","));
    }
    if (!norec.empty()) m.flags.push_back("-fno-sanitize-recover=" + util::join(norec, ","));
    if (!r.ignorelist.empty()) {
        fs::path p = fs::absolute(r.ignorelist);
        if (!fs::is_regular_file(p)) util::fail("--sanitize-ignorelist : fichier introuvable : {}", p.string());
        m.flags.push_back("-fsanitize-ignorelist=" + p.string());
    }
    for (auto& s : added) m.name += "+" + s;
    if (!trap.empty()) m.name += "+trap";
    if (!norec.empty()) m.name += "+norecover";
    m.builtin = false;
    return m;
}

} // namespace sanitize
