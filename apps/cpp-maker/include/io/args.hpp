/* @file io/args.hpp
 *
 * Analyse des arguments d'une commande.
 *
 *   args::Arguments a;
 *   a.add("--jobs").set_value(0).flags("-j").metavar("N").help("Workers");
 *   a.add("--verbose").set_value(false).flags("-v");
 *   a.add("--sanitize").set_value(std::vector<std::string>{}).metavar("LISTE");
 *   a.parse({"build", "-vj8", "release", "--sanitize=address", "--", "x"});
 *   a.get<int>("--jobs");      // 8
 *   a.positionals();           // {"release"} (après la route consommée)
 *   a.trailing();              // {"x"}       (après « -- »)
 *
 *  - les positionnels peuvent apparaître avant, entre ou après les options ;
 *  - une option de type std::vector<std::string> est répétable et accepte
 *    les listes séparées par des virgules (« -m a,b -m c » → {a, b, c}) ;
 *  - « --opt=valeur », « --opt valeur », « -o valeur », « -ovaleur » ;
 *  - grappes d'options courtes booléennes (« -vk ») ;
 *  - tout ce qui suit « -- » est conservé tel quel (trailing()).
 */
#pragma once

#include <any>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <typeindex>
#include <vector>

namespace args {

enum class ErrorCode {
    UnknownOption,
    MissingValue,
    InvalidValue,
    DuplicatedOption,
    MissingRequiredOption,
    ArgumentNotRegistered,
    TypeMismatch,
};

class Exception : public std::runtime_error {
public:
    Exception(ErrorCode code, const std::string& msg) : std::runtime_error(msg), code_(code) {}
    [[nodiscard]] ErrorCode code() const noexcept { return code_; }

private:
    ErrorCode code_;
};

class ArgumentBuilder;

class Arguments {
public:
    struct Entry {
        std::string name;                 ///< « --jobs »
        std::vector<std::string> flags;   ///< alias (« -j », « --workers »)
        std::string help;
        std::string metavar;
        bool required = false;
        bool explicitly_set = false;
        bool hidden = false;
        std::type_index type{typeid(void)};
        std::any value;
        std::any default_value;
        std::function<bool(const std::string&, std::any&)> parser;

        [[nodiscard]] bool is_flag() const { return type == typeid(bool); }
        [[nodiscard]] bool is_list() const { return type == typeid(std::vector<std::string>); }
    };

    ArgumentBuilder add(const std::string& name);

    void parse(const std::vector<std::string>& argv);
    void parse(int argc, char* argv[]);

    template <typename T>
    [[nodiscard]] T get(const std::string& name) const {
        const Entry* e = find(name);
        if (!e) throw Exception(ErrorCode::ArgumentNotRegistered, "argument non déclaré : " + name);
        if (e->type != typeid(T)) throw Exception(ErrorCode::TypeMismatch, "type incorrect pour " + name);
        return std::any_cast<T>(e->value);
    }

    template <typename T>
    [[nodiscard]] std::optional<T> try_get(const std::string& name) const {
        const Entry* e = find(name);
        if (!e || e->type != typeid(T)) return std::nullopt;
        return std::any_cast<T>(e->value);
    }

    [[nodiscard]] bool has(const std::string& name) const { return find(name) != nullptr; }
    [[nodiscard]] bool is_set(const std::string& name) const;

    /// Tous les mots non-options, dans l'ordre (route de commande incluse).
    [[nodiscard]] const std::vector<std::string>& words() const { return words_; }
    /// Arguments situés après « -- ».
    [[nodiscard]] const std::vector<std::string>& trailing() const { return trailing_; }
    [[nodiscard]] bool has_double_dash() const { return double_dash_; }

    void set_dispatch_depth(std::size_t d) { dispatch_depth_ = d; }
    [[nodiscard]] std::size_t dispatch_depth() const { return dispatch_depth_; }
    /// Mots restant après la route de commande consommée par le dispatcher.
    [[nodiscard]] std::vector<std::string> positionals() const;

    /// Liste des options, alignée (pour l'aide d'une commande).
    [[nodiscard]] std::string help() const;
    [[nodiscard]] const std::vector<Entry>& entries() const { return entries_; }

private:
    friend class ArgumentBuilder;

    std::vector<Entry> entries_;
    std::vector<std::string> words_;
    std::vector<std::string> trailing_;
    std::size_t dispatch_depth_ = 0;
    bool double_dash_ = false;

    [[nodiscard]] const Entry* find(const std::string& name) const;
    Entry* find_by_flag(const std::string& flag);
    void apply_value(Entry& e, const std::string& flag, const std::string& value);
    [[nodiscard]] std::string unknown_option_message(const std::string& flag) const;
};

namespace detail {
bool parse_value(const std::string& s, bool& out);
bool parse_value(const std::string& s, int& out);
bool parse_value(const std::string& s, long long& out);
bool parse_value(const std::string& s, double& out);
bool parse_value(const std::string& s, std::string& out);
bool parse_value(const std::string& s, std::vector<std::string>& out);
} // namespace detail

class ArgumentBuilder {
public:
    ArgumentBuilder(Arguments& args, size_t index) : args_(&args), index_(index) {}

    ArgumentBuilder& set_value(bool v) { return typed<bool>(v); }
    ArgumentBuilder& set_value(int v) { return typed<int>(v); }
    ArgumentBuilder& set_value(long long v) { return typed<long long>(v); }
    ArgumentBuilder& set_value(double v) { return typed<double>(v); }
    ArgumentBuilder& set_value(const std::string& v) { return typed<std::string>(v); }
    ArgumentBuilder& set_value(const char* v) { return typed<std::string>(std::string(v)); }
    ArgumentBuilder& set_value(const std::vector<std::string>& v) { return typed<std::vector<std::string>>(v); }

    /// Alias : « -j », « -vk » (grappe → -v, -k), « --workers ».
    ArgumentBuilder& flags(const std::string& f);
    ArgumentBuilder& help(const std::string& h) { entry().help = h; return *this; }
    ArgumentBuilder& metavar(const std::string& m) { entry().metavar = m; return *this; }
    ArgumentBuilder& required(bool r = true) { entry().required = r; return *this; }
    ArgumentBuilder& hidden(bool h = true) { entry().hidden = h; return *this; }

private:
    Arguments::Entry& entry() { return args_->entries_[index_]; }

    template <typename T>
    ArgumentBuilder& typed(T def) {
        auto& e = entry();
        e.type = typeid(T);
        e.value = def;
        e.default_value = def;
        e.parser = [](const std::string& s, std::any& out) -> bool {
            T v{};
            if (!detail::parse_value(s, v)) return false;
            out = std::move(v);
            return true;
        };
        return *this;
    }

    Arguments* args_;
    size_t index_;
};

} // namespace args
