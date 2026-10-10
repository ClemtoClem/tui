#include "model/Formula.hpp"

#include <algorithm>
#include <cctype>

namespace sheetapp {

namespace {

enum class TokKind { Number, Ident, Plus, Minus, Star, Slash, LParen, RParen, Comma, Colon, End, Invalid };

struct Token {
    TokKind kind = TokKind::End;
    std::string text;
    double number = 0.0;
};

std::vector<Token> tokenize(std::string_view text) {
    std::vector<Token> out;
    size_t i = 0;
    while (i < text.size()) {
        char c = text[i];
        if (std::isspace(static_cast<unsigned char>(c))) { ++i; continue; }

        if (std::isdigit(static_cast<unsigned char>(c)) ||
            (c == '.' && i + 1 < text.size() && std::isdigit(static_cast<unsigned char>(text[i + 1])))) {
            size_t start = i;
            while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
            if (i < text.size() && text[i] == '.') {
                ++i;
                while (i < text.size() && std::isdigit(static_cast<unsigned char>(text[i]))) ++i;
            }
            Token t;
            t.kind = TokKind::Number;
            t.text = std::string(text.substr(start, i - start));
            try {
                t.number = std::stod(t.text);
            } catch (...) {
                t.number = 0.0;
            }
            out.push_back(std::move(t));
            continue;
        }

        if (std::isalpha(static_cast<unsigned char>(c))) {
            size_t start = i;
            while (i < text.size() && std::isalnum(static_cast<unsigned char>(text[i]))) ++i;
            Token t;
            t.kind = TokKind::Ident;
            t.text = std::string(text.substr(start, i - start));
            out.push_back(std::move(t));
            continue;
        }

        Token t;
        switch (c) {
            case '+': t.kind = TokKind::Plus; break;
            case '-': t.kind = TokKind::Minus; break;
            case '*': t.kind = TokKind::Star; break;
            case '/': t.kind = TokKind::Slash; break;
            case '(': t.kind = TokKind::LParen; break;
            case ')': t.kind = TokKind::RParen; break;
            case ',': t.kind = TokKind::Comma; break;
            case ':': t.kind = TokKind::Colon; break;
            default: t.kind = TokKind::Invalid; t.text = std::string(1, c); break;
        }
        ++i;
        out.push_back(std::move(t));
    }
    out.push_back(Token{TokKind::End, "", 0.0});
    return out;
}

class Parser {
public:
    explicit Parser(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}

    std::unique_ptr<Expr> parse_expr() {
        auto lhs = parse_term();
        while (ok_ && (check(TokKind::Plus) || check(TokKind::Minus))) {
            char op = check(TokKind::Plus) ? '+' : '-';
            advance();
            auto rhs = parse_term();
            lhs = make_binary(op, std::move(lhs), std::move(rhs));
        }
        return lhs;
    }

    [[nodiscard]] bool ok() const { return ok_; }
    [[nodiscard]] const std::string& error() const { return error_; }
    [[nodiscard]] bool at_end() const { return check(TokKind::End); }

private:
    [[nodiscard]] const Token& peek() const { return tokens_[pos_]; }
    [[nodiscard]] bool check(TokKind k) const { return peek().kind == k; }
    void advance() { if (pos_ + 1 < tokens_.size()) ++pos_; }
    bool match(TokKind k) {
        if (check(k)) { advance(); return true; }
        return false;
    }
    void fail(std::string msg) {
        if (ok_) { ok_ = false; error_ = std::move(msg); }
    }

    static std::unique_ptr<Expr> make_binary(char op, std::unique_ptr<Expr> lhs, std::unique_ptr<Expr> rhs) {
        auto node = std::make_unique<Expr>();
        node->kind = ExprKind::BinaryOp;
        node->op = op;
        node->args.push_back(std::move(lhs));
        node->args.push_back(std::move(rhs));
        return node;
    }

    std::unique_ptr<Expr> parse_term() {
        auto lhs = parse_factor();
        while (ok_ && (check(TokKind::Star) || check(TokKind::Slash))) {
            char op = check(TokKind::Star) ? '*' : '/';
            advance();
            auto rhs = parse_factor();
            lhs = make_binary(op, std::move(lhs), std::move(rhs));
        }
        return lhs;
    }

    std::unique_ptr<Expr> parse_factor() {
        if (match(TokKind::Minus)) {
            auto operand = parse_factor();
            auto node = std::make_unique<Expr>();
            node->kind = ExprKind::UnaryMinus;
            node->args.push_back(std::move(operand));
            return node;
        }
        return parse_primary();
    }

    std::unique_ptr<Expr> parse_primary() {
        if (check(TokKind::Number)) {
            auto node = std::make_unique<Expr>();
            node->kind = ExprKind::Number;
            node->number = peek().number;
            advance();
            return node;
        }
        if (match(TokKind::LParen)) {
            auto inner = parse_expr();
            if (!match(TokKind::RParen)) fail("parenthese fermante manquante");
            return inner;
        }
        if (check(TokKind::Ident)) {
            std::string name = peek().text;
            advance();
            if (check(TokKind::LParen)) return parse_call(name);
            auto pos = parse_cell_ref(name);
            if (!pos) {
                fail("reference de cellule invalide: " + name);
                return std::make_unique<Expr>();
            }
            auto node = std::make_unique<Expr>();
            node->kind = ExprKind::Ref;
            node->ref = *pos;
            return node;
        }
        fail("expression attendue");
        return std::make_unique<Expr>();
    }

    std::unique_ptr<Expr> parse_call(const std::string& name) {
        advance(); // consomme '('
        auto node = std::make_unique<Expr>();
        node->kind = ExprKind::Call;
        node->func_name = name;
        if (!check(TokKind::RParen)) {
            node->args.push_back(parse_argument());
            while (ok_ && match(TokKind::Comma)) node->args.push_back(parse_argument());
        }
        if (!match(TokKind::RParen)) fail("parenthese fermante manquante apres les arguments");
        return node;
    }

    std::unique_ptr<Expr> parse_argument() {
        if (check(TokKind::Ident) && pos_ + 2 < tokens_.size() && tokens_[pos_ + 1].kind == TokKind::Colon &&
            tokens_[pos_ + 2].kind == TokKind::Ident) {
            auto a = parse_cell_ref(tokens_[pos_].text);
            auto b = parse_cell_ref(tokens_[pos_ + 2].text);
            if (a && b) {
                pos_ = std::min(pos_ + 3, tokens_.size() - 1);
                auto node = std::make_unique<Expr>();
                node->kind = ExprKind::Range;
                node->range = CellRange{
                    CellPos{std::min(a->col, b->col), std::min(a->row, b->row)},
                    CellPos{std::max(a->col, b->col), std::max(a->row, b->row)},
                };
                return node;
            }
        }
        return parse_expr();
    }

    std::vector<Token> tokens_;
    size_t pos_ = 0;
    bool ok_ = true;
    std::string error_;
};

std::vector<CellPos> range_cells(const CellRange& r) {
    std::vector<CellPos> cells;
    for (int row = r.start.row; row <= r.end.row; ++row) {
        for (int col = r.start.col; col <= r.end.col; ++col) cells.push_back(CellPos{col, row});
    }
    return cells;
}

// Aplati un argument de fonction en valeurs numeriques : une plage
// ignore ses cellules vides ; une expression normale (y compris une
// reference isolee) compte toujours, une cellule vide y valant 0
// (comme dans un calcul arithmetique classique).
bool collect_values(const Expr& arg, const EvalContext& ctx, std::vector<double>& out, FormulaResult& error_out) {
    if (arg.kind == ExprKind::Range) {
        for (CellPos p : range_cells(arg.range)) {
            if (ctx.is_empty && ctx.is_empty(p)) continue;
            FormulaResult r = ctx.resolve(p);
            if (r.error) { error_out = r; return false; }
            out.push_back(r.value);
        }
        return true;
    }
    FormulaResult r = evaluate_expr(arg, ctx);
    if (r.error) { error_out = r; return false; }
    out.push_back(r.value);
    return true;
}

std::string to_upper(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

} // namespace

ParseResult parse_formula(std::string_view text) {
    auto tokens = tokenize(text);
    for (const auto& t : tokens) {
        if (t.kind == TokKind::Invalid) return {nullptr, false, "caractere invalide: " + t.text};
    }
    Parser parser(std::move(tokens));
    auto expr = parser.parse_expr();
    if (!parser.ok()) return {nullptr, false, parser.error()};
    if (!parser.at_end()) return {nullptr, false, "caracteres inattendus en fin de formule"};
    return {std::move(expr), true, {}};
}

FormulaResult evaluate_expr(const Expr& e, const EvalContext& ctx) {
    switch (e.kind) {
        case ExprKind::Number:
            return FormulaResult::ok(e.number);
        case ExprKind::Ref:
            return ctx.resolve(e.ref);
        case ExprKind::Range:
            return FormulaResult::err("#ERR"); // une plage seule (hors argument de fonction) n'est pas une valeur
        case ExprKind::UnaryMinus: {
            auto r = evaluate_expr(*e.args[0], ctx);
            if (r.error) return r;
            return FormulaResult::ok(-r.value);
        }
        case ExprKind::BinaryOp: {
            auto lhs = evaluate_expr(*e.args[0], ctx);
            if (lhs.error) return lhs;
            auto rhs = evaluate_expr(*e.args[1], ctx);
            if (rhs.error) return rhs;
            switch (e.op) {
                case '+': return FormulaResult::ok(lhs.value + rhs.value);
                case '-': return FormulaResult::ok(lhs.value - rhs.value);
                case '*': return FormulaResult::ok(lhs.value * rhs.value);
                case '/':
                    if (rhs.value == 0.0) return FormulaResult::err("#DIV0");
                    return FormulaResult::ok(lhs.value / rhs.value);
                default: return FormulaResult::err("#ERR");
            }
        }
        case ExprKind::Call: {
            std::string name = to_upper(e.func_name);
            std::vector<double> values;
            for (const auto& arg : e.args) {
                FormulaResult err;
                if (!collect_values(*arg, ctx, values, err)) return err;
            }
            if (name == "SUM") {
                double s = 0.0;
                for (double v : values) s += v;
                return FormulaResult::ok(s);
            }
            if (name == "AVERAGE") {
                if (values.empty()) return FormulaResult::err("#DIV0");
                double s = 0.0;
                for (double v : values) s += v;
                return FormulaResult::ok(s / static_cast<double>(values.size()));
            }
            if (name == "MIN") {
                if (values.empty()) return FormulaResult::ok(0.0);
                return FormulaResult::ok(*std::min_element(values.begin(), values.end()));
            }
            if (name == "MAX") {
                if (values.empty()) return FormulaResult::ok(0.0);
                return FormulaResult::ok(*std::max_element(values.begin(), values.end()));
            }
            if (name == "COUNT") return FormulaResult::ok(static_cast<double>(values.size()));
            return FormulaResult::err("#NAME");
        }
    }
    return FormulaResult::err("#ERR");
}

} // namespace sheetapp
