/* @file toolchain/Sanitize.hpp
 *
 * Catalogue des sanitizers et construction des modes dérivés
 * (« debug+address+undefined ») demandés par --sanitize. Un mode dérivé a
 * son propre dossier de build : les objets du mode d'origine sont conservés.
 */
#pragma once

#include "model/Model.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace toolchain { struct Toolchain; }

namespace sanitize {

struct Info {
    std::string_view name;     ///< valeur de -fsanitize=
    std::string_view family;   ///< famille / restriction
    std::string_view detail;
    bool clang_only;
    bool runtime;              ///< runtime complet (exclusif, sauf leak)
};

/// Options --sanitize* d'une invocation.
struct Request {
    std::vector<std::string> sanitize, trap, no_recover;
    std::string ignorelist;
    [[nodiscard]] bool any() const {
        return !sanitize.empty() || !trap.empty() || !no_recover.empty() || !ignorelist.empty();
    }
};

const std::vector<Info>& catalog();
const Info* find(std::string_view name);
/// « -fsanitize=cfi » → « cfi », « asan » → « address »…
std::string normalize(std::string_view raw);
/// Sanitizers actifs d'un mode (champ sanitizer + options -fsanitize=).
std::vector<std::string> from_mode(const model::ModeConfig& m);
/// Message d'erreur si la combinaison est impossible, vide sinon.
std::string check_compatible(const std::vector<std::string>& sans);

/// « --sanitize all » : une requête par runtime possible pour la toolchain.
bool wants_all(const Request& r);
std::vector<Request> expand_all(const Request& r, const model::ModeConfig& base, const toolchain::Toolchain& tc);

/// Mode dérivé : `base` + options demandées ; vérifie la compatibilité.
model::ModeConfig apply(const model::ModeConfig& base, const Request& r, const toolchain::Toolchain& tc);

} // namespace sanitize
