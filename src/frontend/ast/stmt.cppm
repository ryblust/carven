module carven:frontend.ast.stmt;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.ids;
import :frontend.ast.type;
import :source.text;
import std;

enum class ASTBindingKind {
    Let,
    Var,
    Const,
};

enum class ASTAssignmentOperator {
    Assign,
    Add,
    Subtract,
    Multiply,
    Divide,
    Remainder,
    BitwiseAnd,
    BitwiseOr,
    BitwiseXor,
    LeftShift,
    RightShift,
};

enum class ASTUpdateOperator {
    Increment,
    Decrement,
};

struct ASTVariableDecl final {
    Span span;
    ASTBindingKind kind;
    Span keyword_span;
    std::optional<Span> async_span;
    ASTBindingTarget target;
    std::optional<ASTTypeID> type;
    std::optional<ASTExprID> initializer;
};

struct ASTAssignment final {
    Span span;
    ASTExprID target;
    ASTAssignmentOperator op;
    Span operator_span;
    ASTExprID value;
};

struct ASTUpdate final {
    Span span;
    ASTUpdateOperator op;
    Span operator_span;
    ASTExprID target;
};

struct ASTExprStatement final {
    ASTExprID expression;
};

struct ASTForInitializer final {
    Span span;
    std::variant<std::monostate, ASTVariableDecl, ASTAssignment, ASTExprID> value;
};

struct ASTForStep final {
    Span span;
    std::variant<ASTAssignment, ASTUpdate, ASTExprID> value;
};

struct ASTRangeForHeader final {
    std::optional<Span> write_marker;
    ASTBindingTarget target;
    std::optional<ASTTypeID> type;
    ASTExprID iterable;
};

struct ASTCStyleForHeader final {
    ASTForInitializer initializer;
    ASTExprID condition;
    std::vector<ASTForStep> steps;
};

struct ASTForHeader final {
    Span span;
    std::variant<ASTRangeForHeader, ASTCStyleForHeader> value;
};

struct ASTWhileStmt final {
    Span keyword_span;
    // Absent for unconditional `while { }`.
    std::optional<ASTExprID> condition;
    ASTBlockID body;
};

struct ASTForStmt final {
    // Present for `const for`, which expands an integer range at compilation.
    std::optional<Span> const_span;
    Span keyword_span;
    ASTForHeader header;
    ASTBlockID body;
};

struct ASTStmt final {
    Span span;
    std::variant<
        ASTConstBlock,
        ASTVariableDecl,
        ASTAssignment,
        ASTUpdate,
        ASTExprStatement,
        ASTControlTransfer,
        ASTWhileStmt,
        ASTForStmt,
        ASTIfForm,
        ASTMatchForm,
        ASTTryForm>
        value;
};

struct ASTBlock final {
    Span span;
    std::vector<ASTStmtID> statements;
};

struct ASTBranchBlock final {
    Span span;
    std::vector<ASTStmtID> statements;
    std::optional<ASTExprID> result;
};
