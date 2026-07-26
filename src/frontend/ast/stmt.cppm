module carven:frontend.ast.stmt;

import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.ids;
import :frontend.ast.region;
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

enum class ASTTestOperationKind {
    Check,
    Require,
    Fail,
};

struct ASTTestOperationStmt final {
    ASTTestOperationKind kind;
    Span keyword_span;
    std::vector<ASTExprID> arguments;
};

struct ASTForInitializer final {
    Span span;
    std::variant<std::monostate, ASTVariableDecl, ASTAssignment, ASTExprID> value;
};

struct ASTForStep final {
    Span span;
    std::variant<ASTAssignment, ASTUpdate, ASTExprID> value;
};

struct ASTHalfOpenRange final {
    ASTExprID begin;
    Span operator_span;
    ASTExprID end;
};

struct ASTRangeForHeader final {
    std::optional<Span> write_marker;
    ASTBindingTarget target;
    std::optional<ASTTypeID> type;
    std::variant<ASTExprID, ASTHalfOpenRange> iterable;
};

struct ASTCStyleForHeader final {
    ASTForInitializer initializer;
    std::optional<ASTExprID> condition;
    std::vector<ASTForStep> steps;
};

struct ASTForHeader final {
    Span span;
    std::variant<ASTRangeForHeader, ASTCStyleForHeader> value;
};

struct ASTWhileStmt final {
    Span keyword_span;
    ASTExprID condition;
    ASTBlockID body;
};

struct ASTForStmt final {
    Span keyword_span;
    ASTForHeader header;
    ASTBlockID body;
};

struct ASTStmt final {
    Span span;
    std::variant<
        ASTVariableDecl,
        ASTAssignment,
        ASTUpdate,
        ASTExprStatement,
        ASTTestOperationStmt,
        ASTControlTransfer,
        ASTWhileStmt,
        ASTForStmt,
        ASTIfForm,
        ASTMatchForm,
        ASTTryForm,
        CppRegion>
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
