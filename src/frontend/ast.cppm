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
    Literal, Ident, Prefix, Postfix, Binary, Call, Index, Field, Group, Assign, CompoundAssign, Comma, If,
};

export enum class StmtKind : std::uint8_t {
    Block, ExprStmt, Empty, VarDecl, Return, While, For,
};

export struct Expr {
    ExprKind kind;
protected:
    constexpr Expr(ExprKind k) noexcept : kind(k) {}
};

export struct Stmt {
    StmtKind kind;
protected:
    constexpr Stmt(StmtKind k) noexcept : kind(k) {}
};

export struct LiteralExpr final : Expr {
    constexpr LiteralExpr(Span t) noexcept : Expr(ExprKind::Literal), token(t) {}
    Span token;
};

export struct IdentExpr final : Expr {
    constexpr IdentExpr(Span n) noexcept : Expr(ExprKind::Ident), name(n) {}
    Span name;
};

export struct PrefixExpr final : Expr {
    constexpr PrefixExpr(UnaryOp o, Span s, Expr* r) noexcept : Expr(ExprKind::Prefix), op(o), span(s), rhs(r) {}
    UnaryOp op;
    Span span;
    Expr* rhs;
};

export struct PostfixExpr final : Expr {
    constexpr PostfixExpr(Expr* l, UnaryOp o, Span s) noexcept : Expr(ExprKind::Postfix), lhs(l), op(o), span(s) {}
    Expr* lhs;
    UnaryOp op;
    Span span;
};

export struct BinaryExpr final : Expr {
    constexpr BinaryExpr(Expr* l, BinOp o, Span s, Expr* r) noexcept : Expr(ExprKind::Binary), lhs(l), op(o), span(s), rhs(r) {}
    Expr* lhs;
    BinOp op;
    Span span;
    Expr* rhs;
};

export struct CallExpr final : Expr {
    constexpr CallExpr(Expr* c, Span l, std::vector<Expr*> a, Span r) noexcept : Expr(ExprKind::Call), callee(c), lparen(l), args(std::move(a)), rparen(r) {}
    Expr* callee;
    Span lparen;
    std::vector<Expr*> args;
    Span rparen;
};

export struct IndexExpr final : Expr {
    constexpr IndexExpr(Expr* l, Span lb, Expr* i, Span rb) noexcept : Expr(ExprKind::Index), lhs(l), lbracket(lb), index(i), rbracket(rb) {}
    Expr* lhs;
    Span lbracket;
    Expr* index;
    Span rbracket;
};

export struct FieldExpr final : Expr {
    constexpr FieldExpr(Expr* l, Span d, Span f) noexcept : Expr(ExprKind::Field), lhs(l), dot(d), field(f) {}
    Expr* lhs;
    Span dot;
    Span field;
};

export struct GroupExpr final : Expr {
    constexpr GroupExpr(Span l, Expr* i, Span r) noexcept : Expr(ExprKind::Group), lparen(l), inner(i), rparen(r) {}
    Span lparen;
    Expr* inner;
    Span rparen;
};

export struct AssignExpr final : Expr {
    constexpr AssignExpr(Expr* l, Span e, Expr* r) noexcept : Expr(ExprKind::Assign), lhs(l), eq(e), rhs(r) {}
    Expr* lhs;
    Span eq;
    Expr* rhs;
};

export struct CompoundAssignExpr final : Expr {
    constexpr CompoundAssignExpr(Expr* l, BinOp o, Span s, Expr* r) noexcept : Expr(ExprKind::CompoundAssign), lhs(l), op(o), span(s), rhs(r) {}
    Expr* lhs;
    BinOp op;
    Span span;
    Expr* rhs;
};

export struct CommaExpr final : Expr {
    constexpr CommaExpr(Expr* l, Span c, Expr* r) noexcept : Expr(ExprKind::Comma), lhs(l), comma(c), rhs(r) {}
    Expr* lhs;
    Span comma;
    Expr* rhs;
};

export struct BlockStmt;

export struct IfExpr final : Expr {
    constexpr IfExpr(Span kw, Expr* cond, BlockStmt* tb, Span ek = {}, BlockStmt* eb = nullptr) noexcept : Expr(ExprKind::If), keyword(kw), condition(cond), then_branch(tb), else_kw(ek), else_branch(eb) {}
    Span keyword;
    Expr* condition;
    BlockStmt* then_branch;
    Span else_kw;
    BlockStmt* else_branch;
};

export struct BlockStmt final : Stmt {
    constexpr BlockStmt(Span l, std::vector<Stmt*> ss, Span r) noexcept : Stmt(StmtKind::Block), lbrace(l), statements(std::move(ss)), rbrace(r) {}
    Span lbrace;
    std::vector<Stmt*> statements;
    Span rbrace;
};

export struct ExprStmt final : Stmt {
    constexpr ExprStmt(Expr* e, Span s) noexcept : Stmt(StmtKind::ExprStmt), expr(e), semicolon(s) {}
    Expr* expr;
    Span semicolon;
};

export struct EmptyStmt final : Stmt {
    constexpr EmptyStmt(Span s) noexcept : Stmt(StmtKind::Empty), semicolon(s) {}
    Span semicolon;
};

export struct VarDecl final : Stmt {
    constexpr VarDecl(Span kw, Span nm, Span tp, Span e, Span sc, Expr* i) noexcept : Stmt(StmtKind::VarDecl), keyword(kw), name(nm), type(tp), eq(e), semicolon(sc), init(i) {}
    Span keyword;
    Span name;
    Span type;
    Span eq;
    Span semicolon;
    Expr* init;
};

export struct ReturnStmt final : Stmt {
    constexpr ReturnStmt(Span kw, Span sc, Expr* v) noexcept : Stmt(StmtKind::Return), keyword(kw), semicolon(sc), value(v) {}
    Span keyword;
    Span semicolon;
    Expr* value;
};

export struct WhileStmt final : Stmt {
    constexpr WhileStmt(Span kw, Expr* c, Stmt* b) noexcept : Stmt(StmtKind::While), keyword(kw), condition(c), body(b) {}
    Span keyword;
    Expr* condition;
    Stmt* body;
};

export struct ForStmt final : Stmt {
    constexpr ForStmt(Span kw, Span lp, std::variant<VarDecl*, ExprStmt*> fi, Span is, Span cs, Expr* cond, Expr* step, Span rp, Stmt* b) noexcept : Stmt(StmtKind::For), keyword(kw), lparen(lp), init(fi), init_semi(is), cond_semi(cs), condition(cond), step(step), rparen(rp), body(b) {}
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
            auto* p = new T(std::forward<Args>(args)...);
            constexpr_nodes.emplace_back(p, [](void* vp) noexcept { delete static_cast<T*>(vp); });
            return p;
        } else {
            constexpr auto align = alignof(T);
            auto cur = (offset + align - 1) & ~(align - 1);
            if (cur + sizeof(T) > block_size) {
                blocks.emplace_back(std::make_unique<std::byte[]>(block_size));
                cur = 0;
            }
            const auto ptr = std::construct_at(reinterpret_cast<T*>(blocks.back().get() + cur), std::forward<Args>(args)...);
            offset = cur + sizeof(T);
            return ptr;
        }
    }

    constexpr ~Arena() noexcept {
        if consteval {
            for (auto& node : constexpr_nodes) {
                node.deleter(node.ptr);
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

    struct ConstexprNode final {
        void* ptr;
        void (*deleter)(void*) noexcept;
    };
    std::vector<ConstexprNode> constexpr_nodes;
};

static_assert(([] static noexcept {
    Arena arena;
    arena.alloc<LiteralExpr>(Span{});
    return true;
} (), true));
