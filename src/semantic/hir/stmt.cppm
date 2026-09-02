module carven:semantic.hir.stmt;

import :semantic.hir.access;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.place;
import std;

struct HIRReturnStmt final {
    std::optional<HIRExprID> value;
};

struct HIRBreakStmt final {};

struct HIRContinueStmt final {};

struct HIRThrowStmt final {
    HIRExprID value;
    HIRTypeID failure_type;
};

struct HIRRethrowStmt final {};

struct HIRExprStmt final {
    HIRExprID expression;
};

enum class HIRBindingKind {
    Let,
    Var,
};

struct HIRBindingStmt final {
    HIRBindingKind kind;
    HIRBindingTarget target;
    HIRTypeID type;
    HIRExprID initializer;
};

enum class HIRAssignmentOperator {
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

struct HIRAssignmentStmt final {
    HIRExprID target;
    HIRAssignmentOperator op;
    HIRExprID value;
};

enum class HIRUpdateOperator {
    Increment,
    Decrement,
};

struct HIRUpdateStmt final {
    HIRUpdateOperator op;
    HIRExprID target;
};

struct HIRIfStmt final {
    std::vector<HIRConditionalBranch> branches;
    std::optional<HIRBlockID> else_branch;
};

struct HIRMatchStmt final {
    HIRExprID subject;
    std::vector<HIRMatchArm> arms;
    HIRMatchCoverageFacts coverage;
};

struct HIRWhileStmt final {
    HIRExprID condition;
    HIRBlockID body;
};

struct HIRCStyleForStmt final {
    SemanticScopeID scope;
    std::optional<HIRStmtID> initializer;
    std::optional<HIRExprID> condition;
    std::vector<HIRStmtID> steps;
    HIRBlockID body;
};

struct HIRHalfOpenRange final {
    HIRExprID begin;
    HIRExprID end;
};

struct HIRRangeForStmt final {
    SemanticScopeID scope;
    HIRAccessMode access;
    HIRBindingTarget target;
    HIRTypeID type;
    std::variant<HIRExprID, HIRHalfOpenRange> iterable;
    HIRBlockID body;
};

struct HIRTestCheckStmt final {
    HIRExprID condition;
    std::optional<HIRExprID> message;
    ProgramSpellingID condition_source;
};

struct HIRTestRequireStmt final {
    HIRExprID condition;
    std::optional<HIRExprID> message;
    ProgramSpellingID condition_source;
};

struct HIRTestFailStmt final {
    std::optional<HIRExprID> message;
};

using HIRStmtValue = std::variant<
    HIRReturnStmt,
    HIRBreakStmt,
    HIRContinueStmt,
    HIRThrowStmt,
    HIRRethrowStmt,
    HIRExprStmt,
    HIRBindingStmt,
    HIRAssignmentStmt,
    HIRUpdateStmt,
    HIRIfStmt,
    HIRMatchStmt,
    HIRWhileStmt,
    HIRCStyleForStmt,
    HIRRangeForStmt,
    HIRTestCheckStmt,
    HIRTestRequireStmt,
    HIRTestFailStmt>;

struct HIRStmt final {
    ProgramOriginID origin;
    HIRStmtValue value;
};

struct HIRBlock final {
    ProgramOriginID origin;
    SemanticScopeID scope;
    std::vector<HIRStmtID> statements;
    std::optional<HIRExprID> result;
};

struct HIRBlockControl final {
    FailureSetID outward_failure_set;
    bool exits_test;
};
