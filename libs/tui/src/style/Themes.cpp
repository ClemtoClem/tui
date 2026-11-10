/* @file style/Themes.cpp — palettes intégrées et génération des variables. */

#include "tui/style/Themes.hpp"

#include "tui/style/ColorUtil.hpp"

#include <algorithm>
#include <mutex>

namespace tui::themes {

namespace {

Color hex(const char* s) { return color::parse(s).value_or(Color::Default()); }

ThemeSpec make(const char* name, bool dark, const char* primary, const char* secondary, const char* accent,
               const char* warning, const char* error, const char* success, const char* foreground,
               const char* background, const char* surface, const char* panel) {
    ThemeSpec t;
    t.name = name;
    t.dark = dark;
    t.primary = hex(primary);
    auto opt = [](const char* s) -> std::optional<Color> {
        if (!s) return std::nullopt;
        return hex(s);
    };
    t.secondary = opt(secondary);
    t.accent = opt(accent);
    t.warning = opt(warning);
    t.error = opt(error);
    t.success = opt(success);
    t.foreground = opt(foreground);
    t.background = opt(background);
    t.surface = opt(surface);
    t.panel = opt(panel);
    return t;
}

std::vector<ThemeSpec> build_builtins() {
    std::vector<ThemeSpec> v;
    //            nom                        sombre | primary    | secondary  | accent     | warning    | error      | success    | foreground | background | surface    | panel
    v.push_back(make("textual-dark",         true,    "#0178D4", "#004578", "#ffa62b", "#ffa62b", "#ba3c5b", "#4EBF71", "#e0e0e0", nullptr,     nullptr,     nullptr));
    v.push_back(make("textual-light",        false,   "#004578", "#0178D4", "#ffa62b", "#ffa62b", "#ba3c5b", "#4EBF71", nullptr,     "#E0E0E0", "#D8D8D8", "#D0D0D0"));
    v.push_back(make("nord",                 true,    "#88C0D0", "#81A1C1", "#B48EAD", "#EBCB8B", "#BF616A", "#A3BE8C", "#D8DEE9", "#2E3440", "#3B4252", "#434C5E"));
    v.push_back(make("gruvbox",              true,    "#85A598", "#A89A85", "#fabd2f", "#fe8019", "#fb4934", "#b8bb26", "#fbf1c7", "#282828", "#3c3836", "#504945"));
    v.push_back(make("catppuccin-mocha",     true,    "#F5C2E7", "#cba6f7", "#fab387", "#FAE3B0", "#F28FAD", "#ABE9B3", "#cdd6f4", "#181825", "#313244", "#45475a"));
    v.push_back(make("catppuccin-macchiato", true,    "#C6A0F6", "#F5A97F", "#F5BDE6", "#EED49F", "#ED8796", "#A6DA95", "#CAD3F5", "#24273A", "#363A4F", "#494D64"));
    v.push_back(make("catppuccin-frappe",    true,    "#CA9EE6", "#EF9F76", "#F4B8E4", "#E5C890", "#E78284", "#A6D189", "#C6D0F5", "#303446", "#414559", "#51576D"));
    v.push_back(make("catppuccin-latte",     false,   "#8839EF", "#DC8A78", "#FE640B", "#DF8E1D", "#D20F39", "#40A02B", "#4C4F69", "#EFF1F5", "#E6E9EF", "#CCD0DA"));
    v.push_back(make("dracula",              true,    "#BD93F9", "#6272A4", "#FF79C6", "#FFB86C", "#FF5555", "#50FA7B", "#F8F8F2", "#282A36", "#2B2E3B", "#313442"));
    v.push_back(make("tokyo-night",          true,    "#BB9AF7", "#7AA2F7", "#FF9E64", "#E0AF68", "#F7768E", "#9ECE6A", "#a9b1d6", "#1A1B26", "#24283B", "#414868"));
    v.push_back(make("monokai",              true,    "#AE81FF", "#F92672", "#66D9EF", "#FD971F", "#F92672", "#A6E22E", "#d6d6d6", "#272822", "#2e2e2e", "#3E3D32"));
    v.push_back(make("flexoki",              true,    "#205EA6", "#24837B", "#9B76C8", "#AD8301", "#AF3029", "#66800B", "#FFFCF0", "#100F0F", "#1C1B1A", "#282726"));
    v.push_back(make("solarized-dark",       true,    "#268bd2", "#2aa198", "#6c71c4", "#cb4b16", "#dc322f", "#859900", "#839496", "#002b36", "#073642", "#073642"));
    v.push_back(make("solarized-light",      false,   "#268bd2", "#2aa198", "#6c71c4", "#cb4b16", "#dc322f", "#859900", "#586e75", "#fdf6e3", "#eee8d5", "#eee8d5"));
    v.push_back(make("rose-pine",            true,    "#c4a7e7", "#31748f", "#ebbcba", "#f6c177", "#eb6f92", "#9ccfd8", "#e0def4", "#191724", "#1f1d2e", "#26233a"));
    v.push_back(make("rose-pine-moon",       true,    "#c4a7e7", "#3e8fb0", "#ea9a97", "#f6c177", "#eb6f92", "#9ccfd8", "#e0def4", "#232136", "#2a273f", "#393552"));
    v.push_back(make("rose-pine-dawn",       false,   "#907aa9", "#286983", "#d7827e", "#ea9d34", "#b4637a", "#56949f", "#575279", "#faf4ed", "#fffaf3", "#f2e9e1"));
    v.push_back(make("atom-one-dark",        true,    "#61AFEF", "#C678DD", "#A378C2", "#DEB25B", "#F06262", "#62F062", "#ABB2BF", "#282C34", "#3B414D", "#4F5666"));
    v.push_back(make("atom-one-light",       false,   "#4078F2", "#A626A4", "#bf9232", "#D8D938", "#F23F3F", "#6CF23F", "#383A42", "#FAFAFA", "#E0E0E0", "#CCCCCC"));
    v.push_back(make("planor",               true,    "#5C54E0", "#7C6FF0", "#D957BE", "#E0B45A", "#E05A5A", "#6EC88C", "#E1E1E8", "#0D0C12", "#16141F", "#1F1C2C"));
    return v;
}

struct Registry {
    std::mutex m;
    std::vector<ThemeSpec> themes = build_builtins();
};

Registry& reg() {
    static Registry r;
    return r;
}

} // namespace

const std::vector<ThemeSpec>& builtin() { return reg().themes; }

std::vector<std::string> names() {
    std::lock_guard lock(reg().m);
    std::vector<std::string> out;
    for (auto& t : reg().themes) out.push_back(t.name);
    return out;
}

const ThemeSpec* find(const std::string& name) {
    std::lock_guard lock(reg().m);
    for (auto& t : reg().themes)
        if (t.name == name) return &t;
    return nullptr;
}

void register_theme(ThemeSpec spec) {
    std::lock_guard lock(reg().m);
    for (auto& t : reg().themes)
        if (t.name == spec.name) {
            t = std::move(spec);
            return;
        }
    reg().themes.push_back(std::move(spec));
}

std::map<std::string, std::string> generate_variables(const ThemeSpec& t) {
    using color::blend;
    using color::lighten;
    using color::to_hex;
    std::map<std::string, std::string> v;

    Color primary = t.primary;
    Color secondary = t.secondary.value_or(primary);
    Color warning = t.warning.value_or(primary);
    Color error = t.error.value_or(secondary);
    Color success = t.success.value_or(secondary);
    Color accent = t.accent.value_or(primary);
    Color background = t.background.value_or(t.dark ? hex("#121212") : hex("#efefef"));
    Color surface = t.surface.value_or(t.dark ? hex("#1e1e1e") : hex("#f5f5f5"));
    Color foreground = t.foreground.value_or(Color{static_cast<uint8_t>(255 - background.r),
                                                   static_cast<uint8_t>(255 - background.g),
                                                   static_cast<uint8_t>(255 - background.b)});
    Color contrast = color::luminance(background) > 0.5 ? Color{0, 0, 0} : Color{255, 255, 255};
    Color boost = t.boost.value_or(blend(background, contrast, 0.04));
    Color panel;
    if (t.panel) panel = *t.panel;
    else {
        panel = blend(surface, primary, 0.1);
        if (t.dark) panel = blend(panel, contrast, 0.04);
    }

    auto tint = [&](Color c) { return to_hex(blend(contrast, c, 0.66)); };
    v["text-primary"] = tint(primary);
    v["text-secondary"] = tint(secondary);
    v["text-warning"] = tint(warning);
    v["text-error"] = tint(error);
    v["text-success"] = tint(success);
    v["text-accent"] = tint(accent);

    const std::pair<const char*, Color> base[] = {
        {"primary", primary},       {"secondary", secondary}, {"primary-background", primary},
        {"secondary-background", secondary}, {"background", background}, {"foreground", foreground},
        {"panel", panel},           {"boost", boost},         {"surface", surface},
        {"warning", warning},       {"error", error},         {"success", success},
        {"accent", accent},
    };
    double step = t.luminosity_spread / 2;
    for (auto& [name, c] : base) {
        bool dark_shade = t.dark && (std::string(name) == "primary-background" || std::string(name) == "secondary-background");
        for (int n = -3; n <= 3; ++n) {
            std::string key = name;
            if (n < 0) key += "-darken-" + std::to_string(-n);
            if (n > 0) key += "-lighten-" + std::to_string(n);
            Color shade = dark_shade ? lighten(blend(Color{0, 0, 0}, c, 0.3), n * step) : lighten(c, n * step);
            v[key] = to_hex(shade);
        }
    }

    // Texte : Textual utilise « auto 87% » ; ici des couleurs fixes équivalentes
    // sur le fond du thème (les widgets peuvent toujours écrire « color: auto »).
    v["text"] = to_hex(foreground);
    v["text-muted"] = to_hex(blend(background, foreground, 0.6));
    v["text-disabled"] = to_hex(blend(background, foreground, 0.38));
    v["foreground-muted"] = to_hex(blend(background, foreground, 0.6));
    v["foreground-disabled"] = to_hex(blend(background, foreground, 0.38));
    v["background-muted"] = to_hex(blend(background, foreground, 0.1));

    for (auto& [name, c] : std::initializer_list<std::pair<const char*, Color>>{
             {"primary", primary}, {"secondary", secondary}, {"accent", accent},
             {"warning", warning}, {"error", error},         {"success", success}})
        v[std::string(name) + "-muted"] = to_hex(blend(c, background, 0.7));

    v["block-cursor-foreground"] = to_hex(color::contrast_text(primary));
    v["block-cursor-background"] = to_hex(primary);
    v["block-cursor-text-style"] = "bold";
    v["block-cursor-blurred-foreground"] = to_hex(foreground);
    v["block-cursor-blurred-background"] = to_hex(blend(background, primary, 0.3));
    v["block-cursor-blurred-text-style"] = "none";
    v["block-hover-background"] = to_hex(blend(background, boost == background ? foreground : boost, 0.1));

    v["border"] = to_hex(primary);
    v["border-blurred"] = to_hex(lighten(surface, -0.025));

    v["surface-active"] = to_hex(lighten(surface, t.dark ? 0.05 : -0.05));
    v["scrollbar"] = to_hex(blend(lighten(background, -0.025), primary, 0.4));
    v["scrollbar-hover"] = to_hex(blend(lighten(background, -0.025), primary, 0.5));
    v["scrollbar-active"] = to_hex(primary);
    v["scrollbar-background"] = to_hex(lighten(background, -0.025));
    v["scrollbar-background-hover"] = v["scrollbar-background"];
    v["scrollbar-background-active"] = v["scrollbar-background"];
    v["scrollbar-corner-color"] = v["scrollbar-background"];

    v["link-background"] = "transparent";
    v["link-background-hover"] = to_hex(primary);
    v["link-color"] = v["text"];
    v["link-style"] = "underline";
    v["link-color-hover"] = to_hex(color::contrast_text(primary));
    v["link-style-hover"] = "bold";

    v["footer-foreground"] = to_hex(foreground);
    v["footer-background"] = to_hex(panel);
    v["footer-key-foreground"] = to_hex(accent);
    v["footer-key-background"] = "transparent";
    v["footer-description-foreground"] = to_hex(foreground);
    v["footer-description-background"] = "transparent";
    v["footer-item-background"] = "transparent";

    v["input-cursor-background"] = to_hex(foreground);
    v["input-cursor-foreground"] = to_hex(background);
    v["input-cursor-text-style"] = "none";
    v["input-selection-background"] = to_hex(blend(background, lighten(primary, step), 0.4));

    v["button-foreground"] = to_hex(foreground);
    v["button-color-foreground"] = to_hex(color::contrast_text(primary));
    v["button-focus-text-style"] = "bold reverse";

    v["screen-selection-background"] = to_hex(blend(background, primary, 0.4));
    v["screen-selection-foreground"] = to_hex(foreground);

    for (auto& [k, val] : t.variables) v[k] = val;
    return v;
}

Theme to_role_theme(const ThemeSpec& t) {
    auto vars = generate_variables(t);
    auto c = [&](const char* name) { return color::parse(vars[name]).value_or(Color::Default()); };
    Theme th;
    th.set(ColorRole::Background, c("background"));
    th.set(ColorRole::Foreground, c("foreground"));
    th.set(ColorRole::Border, c("border-blurred"));
    th.set(ColorRole::BorderFocused, c("border"));
    th.set(ColorRole::Selection, c("block-cursor-background"));
    th.set(ColorRole::SelectionText, c("block-cursor-foreground"));
    th.set(ColorRole::Accent, c("accent"));
    th.set(ColorRole::Error, c("error"));
    th.set(ColorRole::Warning, c("warning"));
    th.set(ColorRole::Success, c("success"));
    th.set(ColorRole::Info, c("secondary"));
    th.set(ColorRole::Disabled, c("text-disabled"));
    th.set(ColorRole::Muted, c("text-muted"));
    th.set_focus_style(TextStyle::Bold);
    return th;
}

} // namespace tui::themes
