/* @file vcs/Result.hpp
 *
 * Résultat commun des opérations git : `ok` faux + `error` non vide en cas
 * d'échec. Partagé par Operations, Backup, etc.
 */

#pragma once

#include <string>

namespace vcs {

struct OpResult {
    bool ok = true;
    std::string error;
    [[nodiscard]] explicit operator bool() const { return ok; }
};

} // namespace vcs
