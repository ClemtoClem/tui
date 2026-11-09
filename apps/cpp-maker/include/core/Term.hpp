/* @file core/Term.hpp
 *
 * Sortie console : couleurs ANSI (désactivées hors terminal, ou avec
 * NO_COLOR ; forcées avec CLICOLOR_FORCE=1) et messages normalisés.
 *
 *   term::info("...")    [cpp-maker] message
 *   term::ok("...")      ✓ message (vert)
 *   term::warn("...")    attention : message (stderr, jaune)
 *   term::error("...")   erreur : message (stderr, rouge)
 */
#pragma once

#include <format>
#include <iostream>
#include <string>
#include <string_view>

namespace term {

bool color_enabled();
void set_color(bool on);

/// Retire les séquences ANSI d'une chaîne.
std::string strip_ansi(std::string_view s);

/// Habillage d'un texte par une couleur ; neutre si les couleurs sont coupées.
std::string paint(std::string_view code, std::string_view text);
inline std::string bold(std::string_view t)    { return paint("1", t); }
inline std::string dim(std::string_view t)     { return paint("2", t); }
inline std::string red(std::string_view t)     { return paint("1;31", t); }
inline std::string green(std::string_view t)   { return paint("1;32", t); }
inline std::string yellow(std::string_view t)  { return paint("1;33", t); }
inline std::string blue(std::string_view t)    { return paint("1;34", t); }
inline std::string magenta(std::string_view t) { return paint("1;35", t); }
inline std::string cyan(std::string_view t)    { return paint("1;36", t); }

void write_out(std::string_view s);
void write_err(std::string_view s);

template <class... Args>
void out(std::format_string<Args...> f, Args&&... a) {
    write_out(std::format(f, std::forward<Args>(a)...));
}
template <class... Args>
void outln(std::format_string<Args...> f, Args&&... a) {
    write_out(std::format(f, std::forward<Args>(a)...) + "\n");
}
inline void outln() { write_out("\n"); }

template <class... Args>
void errln(std::format_string<Args...> f, Args&&... a) {
    write_err(std::format(f, std::forward<Args>(a)...) + "\n");
}
template <class... Args>
void info(std::format_string<Args...> f, Args&&... a) {
    write_out(blue("[cpp-maker]") + " " + std::format(f, std::forward<Args>(a)...) + "\n");
}
template <class... Args>
void ok(std::format_string<Args...> f, Args&&... a) {
    write_out(green("✓") + " " + std::format(f, std::forward<Args>(a)...) + "\n");
}
template <class... Args>
void warn(std::format_string<Args...> f, Args&&... a) {
    write_err(yellow("attention :") + " " + std::format(f, std::forward<Args>(a)...) + "\n");
}
template <class... Args>
void error(std::format_string<Args...> f, Args&&... a) {
    write_err(red("erreur :") + " " + std::format(f, std::forward<Args>(a)...) + "\n");
}

/// Question oui/non sur stdin ; `assume_yes` court-circuite la question.
bool confirm(const std::string& question, bool assume_yes);

} // namespace term
