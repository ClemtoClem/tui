#pragma once

// Moteur de formules minimal : lexer + parseur descente-recursive vers
// un AST, puis evaluateur. Grammaire :
//   expr   := term (('+'|'-') term)*
//   term   := factor (('*'|'/') factor)*
//   factor := '-' factor | primary
//   primary:= NUMBER | CELLREF | FUNC '(' args ')' | '(' expr ')'
//   args   := arg (',' arg)*  (vide autorise)
//   arg    := CELLREF ':' CELLREF   (plage, uniquement en argument de fonction)
//           | expr
// Fonctions : SUM, AVERAGE, MIN, MAX, COUNT.

#include "CellRef.hpp"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace sheetapp {

enum class ExprKind { Number, Ref, Range, BinaryOp, UnaryMinus, Call };

struct Expr {
    ExprKind kind;
    double number = 0.0;
    CellPos ref{};
    CellRange range{};
    char op = 0; // BinaryOp uniquement : '+','-','*','/'
    std::string func_name; // Call uniquement
    std::vector<std::unique_ptr<Expr>> args; // BinaryOp:[lhs,rhs] UnaryMinus:[x] Call:[args...]
};

struct ParseResult {
    std::unique_ptr<Expr> expr;
    bool ok = true;
    std::string error;
};

// `text` = la formule sans le '=' initial.
ParseResult parse_formula(std::string_view text);

struct FormulaResult {
    bool error = false;
    double value = 0.0;
    std::string error_text; // "#DIV0", "#NAME", "#CIRC", "#ERR" ...

    static FormulaResult ok(double v) { return FormulaResult{false, v, {}}; }
    static FormulaResult err(std::string text) { return FormulaResult{true, 0.0, std::move(text)}; }
};

struct EvalContext {
    std::function<FormulaResult(CellPos)> resolve; // valeur d'une autre cellule (memoise/cycle-garde cote Sheet)
    std::function<bool(CellPos)> is_empty; // pour COUNT : ignorer les cellules vides d'une plage
};

FormulaResult evaluate_expr(const Expr& e, const EvalContext& ctx);

} // namespace sheetapp
