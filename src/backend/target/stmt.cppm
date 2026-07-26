module carven:backend.target.stmt;

import :backend.target.ids;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.raw;
import std;

struct TargetExprStmt final {
    TargetExprID expression;
};

struct TargetDiscardStmt final {
    TargetExprID expression;
};

struct TargetReturnStmt final {
    std::optional<TargetExprID> expression;
};

enum class TargetVariableBinding {
    MutableValue,
    MutableReference,
    RvalueReference,
    ConstValue,
    ConstReference,
};

struct TargetVariableStmt final {
    TargetVariableBinding binding;
    TargetIdentifier name;
    TargetTypeID type;
    TargetExprID initializer;
    bool maybe_unused;
};

struct TargetBlockStmt final {
    std::vector<TargetStmtID> statements;
    bool scoped;
};

enum class TargetAssignmentOperator {
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

struct TargetAssignmentStmt final {
    TargetExprID target;
    TargetAssignmentOperator op;
    TargetExprID value;
};

enum class TargetUpdateOperator {
    Increment,
    Decrement,
};

struct TargetUpdateStmt final {
    TargetUpdateOperator op;
    TargetExprID target;
};

struct TargetBreakStmt final {};

struct TargetContinueStmt final {};

enum class TargetSyntheticControlKind {
    StatementTryFailureForward,
    NormalizedForContinue,
};

struct TargetGotoStmt final {
    TargetIdentifier label;
    TargetSyntheticControlKind kind;
};

struct TargetLabelStmt final {
    TargetIdentifier label;
    TargetSyntheticControlKind kind;
};

struct TargetIfBranch final {
    TargetExprID condition;
    std::vector<TargetStmtID> body;
};

struct TargetIfStmt final {
    std::vector<TargetIfBranch> branches;
    std::optional<std::vector<TargetStmtID>> else_body;
};

struct TargetWhileStmt final {
    TargetExprID condition;
    std::vector<TargetStmtID> body;
};

struct TargetForStmt final {
    std::optional<TargetStmtID> initializer;
    std::optional<TargetExprID> condition;
    std::vector<TargetStmtID> steps;
    std::vector<TargetStmtID> body;
};

enum class TargetRangeBindingMode {
    ReadValue,
    ReadReference,
    MutableReference,
};

struct TargetRangeForStmt final {
    TargetRangeBindingMode binding_mode;
    TargetIdentifier name;
    TargetTypeID type;
    TargetExprID iterable;
    std::vector<TargetStmtID> body;
    bool maybe_unused;
};

using TargetStmtValue = std::variant<
    TargetExprStmt,
    TargetDiscardStmt,
    TargetReturnStmt,
    TargetVariableStmt,
    TargetBlockStmt,
    TargetAssignmentStmt,
    TargetUpdateStmt,
    TargetBreakStmt,
    TargetContinueStmt,
    TargetGotoStmt,
    TargetLabelStmt,
    TargetIfStmt,
    TargetWhileStmt,
    TargetForStmt,
    TargetRangeForStmt,
    TargetRawFragment>;

struct TargetStmt final {
    TargetStmtValue value;
    TargetAttribution attribution;
};
