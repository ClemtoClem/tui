#pragma once

// Feuille de calcul : stockage eparse des cellules (grille extensible,
// pas de pre-allocation), evaluation memoisee des formules avec
// detection de cycle ("#CIRC").

#include "CellRef.hpp"
#include "Formula.hpp"

#include <cstdio>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace sheetapp {

class Sheet {
public:
    void set_cell(CellPos p, std::string raw) {
        if (raw.empty()) {
            cells_.erase(p);
        } else {
            cells_[p] = std::move(raw);
        }
        cache_.clear(); // strategie simple : recalcul paresseux a la prochaine lecture
    }

    [[nodiscard]] std::string raw(CellPos p) const {
        auto it = cells_.find(p);
        return it == cells_.end() ? std::string{} : it->second;
    }

    [[nodiscard]] bool empty(CellPos p) const { return cells_.find(p) == cells_.end(); }

    void clear() {
        cells_.clear();
        cache_.clear();
    }

    [[nodiscard]] std::vector<std::pair<CellPos, std::string>> entries() const {
        std::vector<std::pair<CellPos, std::string>> out(cells_.begin(), cells_.end());
        return out;
    }

    // Valeur formatee pour l'affichage dans la grille.
    [[nodiscard]] std::string display(CellPos p) const {
        auto it = cells_.find(p);
        if (it == cells_.end()) return {};
        const std::string& text = it->second;
        if (!text.empty() && text[0] == '=') {
            FormulaResult r = evaluate(p);
            if (r.error) return r.error_text;
            return format_number(r.value);
        }
        return text;
    }

    // Valeur numerique + statut d'erreur d'une cellule (formule evaluee,
    // litteral numerique parse, ou 0 si texte/vide) ; utilisee comme
    // resolveur par le moteur de formules pour les references croisees.
    [[nodiscard]] FormulaResult evaluate(CellPos p) const {
        auto cached = cache_.find(p);
        if (cached != cache_.end()) return cached->second;

        auto it = cells_.find(p);
        if (it == cells_.end()) return FormulaResult::ok(0.0);
        const std::string& text = it->second;

        if (text.empty() || text[0] != '=') {
            double v = 0.0;
            bool numeric = parse_number(text, v);
            FormulaResult r = FormulaResult::ok(numeric ? v : 0.0);
            cache_.emplace(p, r);
            return r;
        }

        if (evaluating_.count(p) != 0) {
            return FormulaResult::err("#CIRC");
        }
        evaluating_.insert(p);

        ParseResult parsed = parse_formula(std::string_view(text).substr(1));
        FormulaResult result;
        if (!parsed.ok) {
            result = FormulaResult::err("#ERR");
        } else {
            EvalContext ctx;
            ctx.resolve = [this](CellPos ref) { return evaluate(ref); };
            ctx.is_empty = [this](CellPos ref) { return empty(ref); };
            result = evaluate_expr(*parsed.expr, ctx);
        }

        evaluating_.erase(p);
        cache_.emplace(p, result);
        return result;
    }

private:
    static bool parse_number(const std::string& s, double& out) {
        if (s.empty()) return false;
        try {
            size_t consumed = 0;
            double v = std::stod(s, &consumed);
            if (consumed != s.size()) return false;
            out = v;
            return true;
        } catch (...) {
            return false;
        }
    }

    static std::string format_number(double v) {
        if (v == static_cast<double>(static_cast<long long>(v))) {
            return std::to_string(static_cast<long long>(v));
        }
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.4g", v);
        return buf;
    }

    std::unordered_map<CellPos, std::string, CellPosHash> cells_;
    mutable std::unordered_map<CellPos, FormulaResult, CellPosHash> cache_;
    mutable std::unordered_set<CellPos, CellPosHash> evaluating_;
};

} // namespace sheetapp
