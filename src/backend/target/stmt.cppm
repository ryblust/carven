module carven:backend.target.stmt;

import :backend.target.expr;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.origin;
import std;

struct TargetExprStmt final {
    TargetExpr expression;
};

struct TargetDiscardStmt final {
    TargetExpr expression;
};

struct TargetReturnStmt final {
    std::optional<TargetExpr> expression;
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
    bool maybe_unused;
    TargetIdentifier name;
    TargetTypeID type;
    TargetExpr initializer;
};

struct TargetBlockStmt final {
    std::vector<TargetStmt> statements;
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
    TargetExpr target;
    TargetAssignmentOperator op;
    TargetExpr value;
};

enum class TargetUpdateOperator {
    Increment,
    Decrement,
};

struct TargetUpdateStmt final {
    TargetUpdateOperator op;
    TargetExpr target;
};

struct TargetBreakStmt final {};

struct TargetContinueStmt final {};

enum class TargetUnreachableReason {
    SemIRProof,
};

struct TargetUnreachableStmt final {
    TargetUnreachableReason reason;
};

enum class TargetRuntimeTrapReason {
    Bounds,
    Shift,
    UnicodeScalar,
    SourceContract,
};

struct TargetRuntimeTrapStmt final {
    TargetRuntimeTrapReason reason;
};

enum class TargetJumpRole {
    RegionExit,
    FailureTransfer,
    ForLoopContinue,
};

struct TargetGotoStmt final {
    TargetIdentifier label;
    TargetJumpRole role;
};

struct TargetLabelStmt final {
    TargetIdentifier label;
    TargetJumpRole role;
};

struct TargetIfBranch final {
    TargetExpr condition;
    std::vector<TargetStmt> body;
};

struct TargetIfStmt final {
    std::vector<TargetIfBranch> branches;
    std::optional<std::vector<TargetStmt>> else_body;
};

struct TargetWhileStmt final {
    TargetExpr condition;
    std::vector<TargetStmt> body;
};

using TargetForInitializerValue = std::variant<
    TargetExprStmt,
    TargetDiscardStmt,
    TargetVariableStmt,
    TargetAssignmentStmt,
    TargetUpdateStmt>;

struct TargetForInitializer final {
    TargetForInitializerValue value;
};

using TargetForStepValue =
    std::variant<TargetExprStmt, TargetDiscardStmt, TargetAssignmentStmt, TargetUpdateStmt>;

struct TargetForStep final {
    TargetForStepValue value;
};

struct TargetForStmt final {
    std::optional<TargetForInitializer> initializer;
    std::optional<TargetExpr> condition;
    std::vector<TargetForStep> steps;
    std::vector<TargetStmt> body;
};

struct TargetRangeForStmt final {
    TargetVariableBinding binding;
    bool maybe_unused;
    TargetIdentifier name;
    TargetTypeID type;
    TargetExpr range;
    std::vector<TargetStmt> body;
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
    TargetUnreachableStmt,
    TargetRuntimeTrapStmt,
    TargetGotoStmt,
    TargetLabelStmt,
    TargetIfStmt,
    TargetWhileStmt,
    TargetForStmt,
    TargetRangeForStmt>;

struct TargetStmt final {
    TargetStmtValue value;
    TargetAttribution attribution;
};
