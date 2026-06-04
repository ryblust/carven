export module carven.frontend.ast;

import carven.common.source;
import std;

export enum class UnaryOp : std::uint32_t {
    Neg, BitNot, LogicalNot, PreInc, PreDec, PostInc, PostDec, AddressOf, Deref,
};

constexpr auto to_string(UnaryOp op) noexcept -> const char* {
    using enum UnaryOp;
    switch (op) {
        case Neg:        return "-";   case BitNot:     return "~";
        case LogicalNot: return "!";   case PreInc:     return "++";
        case PreDec:     return "--";  case PostInc:    return "++";
        case PostDec:    return "--";  case AddressOf:  return "&";
        case Deref:      return "*";   default:         return "?";
    }
}

template<> struct std::formatter<UnaryOp> final {
    constexpr auto parse(const auto& context) const noexcept { return context.begin(); }
    auto format(UnaryOp op, auto&& context) const noexcept {
        return std::format_to(context.out(), "{}", to_string(op));
    }
};

export enum class BinOp : std::uint32_t {
    Add, Sub, Mul, Div, Mod, BitAnd, BitOr, BitXor, Shl, Shr, Eq, Ne, Lt, Le, Gt, Ge, LogicalAnd, LogicalOr,
};

constexpr auto to_string(BinOp op) noexcept -> const char* {
    using enum BinOp;
    switch (op) {
        case Add: return "+";  case Sub:    return "-";  case Mul:   return "*";  case Div:    return "/";
        case Mod: return "%";  case BitAnd: return "&";  case BitOr: return "|";  case BitXor: return "^";
        case Shl: return "<<"; case Shr:    return ">>"; case Eq:    return "=="; case Ne:     return "!=";
        case Lt:  return "<";  case Le:     return "<="; case Gt:    return ">";  case Ge:     return ">=";
        case LogicalAnd: return "&&"; case LogicalOr: return "||"; default: return "?";
    }
}

template<> struct std::formatter<BinOp> final {
    constexpr auto parse(const auto& context) const noexcept { return context.begin(); }
    auto format(BinOp op, auto&& context) const noexcept {
        return std::format_to(context.out(), "{}", to_string(op));
    }
};

export enum class ExprKind : std::uint8_t {
    Literal, Ident, Prefix, Postfix, Binary, Call, Index, Field, Group, Assign, CompoundAssign, Comma, If, Array, Match,
};

export enum class StmtKind : std::uint8_t {
    Block, ExprStmt, Empty, VarDecl, Return, While, For,
};

export struct Expr {
    ExprKind kind;
};

export struct Stmt {
    StmtKind kind;
};

export struct LiteralExpr final : Expr {
    static constexpr auto kind = ExprKind::Literal;
    Span token;
};

export struct IdentExpr final : Expr {
    static constexpr auto kind = ExprKind::Ident;
    Span name;
};

export struct PrefixExpr final : Expr {
    static constexpr auto kind = ExprKind::Prefix;
    UnaryOp op;
    Span span;
    Expr* rhs;
};

export struct PostfixExpr final : Expr {
    static constexpr auto kind = ExprKind::Postfix;
    Expr* lhs;
    UnaryOp op;
    Span span;
};

export struct BinaryExpr final : Expr {
    static constexpr auto kind = ExprKind::Binary;
    Expr* lhs;
    BinOp op;
    Span span;
    Expr* rhs;
};

export struct CallExpr final : Expr {
    static constexpr auto kind = ExprKind::Call;
    Expr* callee;
    Span lparen;
    std::vector<Expr*> args;
    Span rparen;
};

export struct IndexExpr final : Expr {
    static constexpr auto kind = ExprKind::Index;
    Expr* lhs;
    Span lbracket;
    Expr* index;
    Span rbracket;
};

export struct FieldExpr final : Expr {
    static constexpr auto kind = ExprKind::Field;
    Expr* lhs;
    Span dot;
    Span field;
};

export struct GroupExpr final : Expr {
    static constexpr auto kind = ExprKind::Group;
    Span lparen;
    Expr* inner;
    Span rparen;
};

export struct AssignExpr final : Expr {
    static constexpr auto kind = ExprKind::Assign;
    Expr* lhs;
    Span eq;
    Expr* rhs;
};

export struct CompoundAssignExpr final : Expr {
    static constexpr auto kind = ExprKind::CompoundAssign;
    Expr* lhs;
    BinOp op;
    Span span;
    Expr* rhs;
};

export struct CommaExpr final : Expr {
    static constexpr auto kind = ExprKind::Comma;
    Expr* lhs;
    Span comma;
    Expr* rhs;
};

export struct ArrayExpr final : Expr {
    static constexpr auto kind = ExprKind::Array;
    Span lbracket;
    std::vector<Expr*> elements;
    Span rbracket;
};

export struct BlockStmt;

export struct MatchArm final {
    std::vector<Expr*> patterns;
    bool is_wildcard;
    Span arrow;
    BlockStmt* body;
};

export struct MatchExpr final : Expr {
    static constexpr auto kind = ExprKind::Match;
    Span keyword;
    Expr* value;
    Span lbrace;
    std::vector<MatchArm> arms;
    Span rbrace;
};

export struct IfExpr final : Expr {
    static constexpr auto kind = ExprKind::If;
    Span keyword;
    Expr* condition;
    BlockStmt* then_branch;
    Span else_kw;
    BlockStmt* else_branch;
};

export struct BlockStmt final : Stmt {
    static constexpr auto kind = StmtKind::Block;
    Span lbrace;
    std::vector<Stmt*> statements;
    Span rbrace;
};

export struct ExprStmt final : Stmt {
    static constexpr auto kind = StmtKind::ExprStmt;
    Expr* expr;
    Span semicolon;
};

export struct EmptyStmt final : Stmt {
    static constexpr auto kind = StmtKind::Empty;
    Span semicolon;
};

export struct VarDecl final : Stmt {
    static constexpr auto kind = StmtKind::VarDecl;
    Span keyword;
    Span name;
    Span type;
    Span eq;
    Span semicolon;
    Expr* init;
};

export struct ReturnStmt final : Stmt {
    static constexpr auto kind = StmtKind::Return;
    Span keyword;
    Span semicolon;
    Expr* value;
};

export struct WhileStmt final : Stmt {
    static constexpr auto kind = StmtKind::While;
    Span keyword;
    Expr* condition;
    Stmt* body;
};

export struct ForStmt final : Stmt {
    static constexpr auto kind = StmtKind::For;
    Span keyword;
    Span lparen;
    std::variant<VarDecl*, ExprStmt*> init;
    Span init_semi;
    Span cond_semi;
    Expr* condition;
    Expr* step;
    Span rparen;
    Stmt* body;
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

export class Arena final {
public:
    template<typename T, typename... Args>
    constexpr auto alloc(Args&&... args) noexcept -> T* {
        if consteval {
            const auto p = new T { { T::kind }, std::forward<Args>(args)... };
            cleanup.emplace_back(p, [](void* vp) static noexcept { delete static_cast<T*>(vp); });
            return p;
        } else {
            constexpr auto align = alignof(T);
            auto cur = (offset + align - 1) & ~(align - 1);
            if (cur + sizeof(T) > block_size) {
                blocks.emplace_back(std::make_unique<std::byte[]>(block_size));
                cur = 0;
            }
            const auto ptr = new(reinterpret_cast<void*>(blocks.back().get() + cur)) T { { T::kind }, std::forward<Args>(args)... };
            offset = cur + sizeof(T);
            return ptr;
        }
    }

    constexpr ~Arena() noexcept {
        if consteval {
            for (const auto [ptr, deleter] : cleanup) {
                deleter(ptr);
            }
        }
    }

    constexpr Arena() noexcept = default;
    constexpr Arena(Arena&&) noexcept = default;
    constexpr auto operator=(Arena&&) noexcept -> Arena& = default;
private:
    static constexpr auto block_size = 65536uz;
    std::vector<std::unique_ptr<std::byte[]>> blocks;
    std::size_t offset = block_size;
    std::vector<std::pair<void*, void (*)(void*) noexcept>> cleanup;
};
