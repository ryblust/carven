export module zero.frontend.ast;

import zero.common.source;
import std;

export struct ParseError final {
    std::string message;
    Span span;
    SourceLocation location;
};

export auto format_parse_error(const ParseError& error) noexcept -> std::string {
    return std::format("error:{}:{}: {}", error.location.line, error.location.column, error.message);
}

export enum class UnaryOp {
    Plus, Neg, BitNot, LogicalNot, PreInc, PreDec, PostInc, PostDec, AddressOf, Deref,
};

export enum class BinOp {
    Add, Sub, Mul, Div, Mod, BitAnd, BitOr, BitXor, Shl, Shr, Eq, Ne, Lt, Le, Gt, Ge, LogicalAnd, LogicalOr,
};

export constexpr auto to_string(BinOp op) noexcept -> const char* {
    using enum BinOp;
    switch (op) {
        case Add: return "+";  case Sub:    return "-";  case Mul:   return "*";  case Div:    return "/";
        case Mod: return "%";  case BitAnd: return "&";  case BitOr: return "|";  case BitXor: return "^";
        case Shl: return "<<"; case Shr:    return ">>"; case Eq:    return "=="; case Ne:     return "!=";
        case Lt:  return "<";  case Le:     return "<="; case Gt:    return ">";  case Ge:     return ">=";
        case LogicalAnd: return "&&"; case LogicalOr: return "||"; default: return "?";
    }
}

export constexpr auto to_string(UnaryOp op) noexcept -> const char* {
    using enum UnaryOp;
    switch (op) {
        case Plus:       return "+";  case Neg:     return "-";  case BitNot: return "~";
        case LogicalNot: return "!";  case PreInc:  return "++"; case PreDec: return "--";
        case PostInc:    return "++"; case PostDec: return "--";
        case AddressOf:  return "&";  case Deref:   return "*";  default:     return "?";
    }
}

export struct Expr;
export struct Stmt;
export struct ForInit;

export struct LiteralExpr final {
    Span token;
};

export struct IdentExpr final {
    Span name;
};

export struct PrefixExpr final {
    UnaryOp op;
    Span span;
    Expr* rhs;
};

export struct PostfixExpr final {
    Expr* lhs;
    UnaryOp op;
    Span span;
};

export struct BinaryExpr final {
    Expr* lhs;
    BinOp op;
    Span span;
    Expr* rhs;
};

export struct CallExpr final {
    Expr* callee;
    Span lparen;
    std::vector<Expr*> args;
    Span rparen;
};

export struct IndexExpr final {
    Expr* lhs;
    Span lbracket;
    Expr* index;
    Span rbracket;
};

export struct FieldExpr final {
    Expr* lhs;
    Span dot;
    Span field;
};

export struct TernaryExpr final {
    Expr* condition;
    Span question;
    Expr* then_branch;
    Span colon;
    Expr* else_branch;
};

export struct GroupExpr final {
    Span lparen;
    Expr* inner;
    Span rparen;
};

export struct AssignExpr final {
    Expr* lhs;
    Span eq;
    Expr* rhs;
};

export struct CompoundAssignExpr final {
    Expr* lhs;
    BinOp op;
    Span span;
    Expr* rhs;
};

export struct CommaExpr final {
    Expr* lhs;
    Span comma;
    Expr* rhs;
};

export struct BlockStmt final {
    Span lbrace;
    std::vector<Stmt*> statements;
    Span rbrace;
};

export struct ExprStmt final {
    Expr* expr;
    Span semicolon;
};

export struct EmptyStmt final {
    Span semicolon;
};

export struct VarDecl final {
    Span keyword;
    Span name;
    Span type;
    Span eq;
    Expr* init;
    Span semicolon;
};

export struct ReturnStmt final {
    Span keyword;
    Expr* value;
    Span semicolon;
};

export struct WhileStmt final {
    Span keyword;
    Expr* condition;
    Stmt* body;
};

export struct ForStmt final {
    Span keyword;
    Span lparen;
    ForInit* init;
    Span init_semi;
    Expr* condition;
    Span cond_semi;
    Expr* step;
    Span rparen;
    Stmt* body;
};

export struct IfExpr final {
    Span keyword;
    Expr* condition;
    BlockStmt then_branch;
    std::optional<Span> else_kw;
    std::optional<BlockStmt> else_branch;
};

export struct ForInit final : std::variant<VarDecl, ExprStmt> {
    using base = std::variant<VarDecl, ExprStmt>;
    using base::base;
};

export struct Expr final
    : std::variant<
        LiteralExpr, IdentExpr, PrefixExpr,
        PostfixExpr, BinaryExpr, CallExpr,
        IndexExpr, FieldExpr, TernaryExpr,
        GroupExpr, AssignExpr, CompoundAssignExpr, CommaExpr, IfExpr>
{
    using base = std::variant<
        LiteralExpr, IdentExpr, PrefixExpr,
        PostfixExpr, BinaryExpr, CallExpr,
        IndexExpr, FieldExpr, TernaryExpr,
        GroupExpr, AssignExpr, CompoundAssignExpr, CommaExpr, IfExpr>;
    using base::base;
};

export struct Stmt final
    : std::variant<
        BlockStmt, ExprStmt, EmptyStmt,
        VarDecl, ReturnStmt, WhileStmt, ForStmt>
{
    using base = std::variant<
        BlockStmt, ExprStmt, EmptyStmt,
        VarDecl, ReturnStmt, WhileStmt, ForStmt>;
    using base::base;
};

export struct FunctionParam final {
    Span name;
    Span type;
};

export struct FunctionItem final {
    Span name;
    std::vector<FunctionParam> params;
    Span return_type;
    BlockStmt* body;
};

export struct ImportItem final {
    Span module_name;
    std::vector<Span> using_decls;
    bool using_wildcard = false;
    bool is_std_module = false;
};

export struct EnumItem final {
    Span name;
    Span size;
    std::vector<Span> fields;
};

export struct StructField final {
    Span name;
    Span type;
};

export struct StructItem final {
    Span name;
    std::vector<StructField> fields;
};

export using TopLevelItem = std::variant<ImportItem, EnumItem, StructItem, FunctionItem>;

export struct ParseResult final {
    std::vector<TopLevelItem> items;
    std::vector<ParseError> errors;
    std::vector<std::unique_ptr<Expr>> exprs;
    std::vector<std::unique_ptr<Stmt>> stmts;
    std::vector<std::unique_ptr<ForInit>> for_inits;

    constexpr auto has_errors() const noexcept -> bool {
        return !errors.empty();
    }
};

export template<typename F>
constexpr auto walk_stmts(const std::vector<Stmt*>& statements, F&& visit) noexcept -> void {
    for (const auto* stmt : statements) visit(*stmt);
}

export template<typename F>
constexpr auto walk_body(const Stmt* body, F&& visit) noexcept -> void {
    if (const auto block = std::get_if<BlockStmt>(body)) {
        for (const auto* stmt : block->statements) visit(*stmt);
    } else {
        visit(*body);
    }
}

export template<typename F, typename G>
constexpr auto walk_for_init(const ForInit& init, F&& visit_var_decl, G&& visit_expr_stmt) noexcept -> void {
    std::visit(Overloaded {
        [&](const VarDecl& d) { visit_var_decl(d); },
        [&](const ExprStmt& es) { visit_expr_stmt(es); }
    }, init);
}

template<> struct std::formatter<BinOp> {
    constexpr auto parse(const auto& ctx) noexcept {
        return ctx.begin();
    }

    constexpr auto format(BinOp op, auto& ctx) const noexcept {
        return std::format_to(ctx.out(), "{}", to_string(op));
    }
};

template<> struct std::formatter<UnaryOp> {
    constexpr auto parse(const auto& ctx) noexcept {
        return ctx.begin();
    }

    constexpr auto format(UnaryOp op, auto& ctx) const noexcept {
        return std::format_to(ctx.out(), "{}", to_string(op));
    }
};

