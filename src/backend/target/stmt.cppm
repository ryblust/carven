module carven:backend.target.stmt;

import :backend.target.expr;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.symbol;
import :support.tree_value;
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

struct TargetCoReturnStmt final {
    TargetExpr expression;

    static constexpr auto native_symbol() noexcept -> TargetSymbol {
        return TargetSymbol::CoReturn;
    }
};

enum class TargetVariableBinding {
    MutableValue,
    MutableReference,
    RvalueReference,
    ConstValue,
    // A pattern binding owns a snapshot whose copy and cleanup are observable.
    ConstSnapshot,
    ConstReference,
};

struct TargetVariableStmt final {
    TargetVariableBinding binding;
    bool maybe_unused;
    TargetLocalID local;
    TargetTypeID type;
    TargetExpr initializer;
};

struct TargetBlockStmt final {
    std::vector<TargetStmt> statements;
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

    static constexpr auto native_symbol() noexcept -> TargetSymbol {
        return TargetSymbol::RuntimeUnreachable;
    }
};

enum class TargetRuntimeTrapReason {
    Bounds,
    Shift,
    UnicodeScalar,
    SourceContract,
};

struct TargetRuntimeTrapStmt final {
    TargetRuntimeTrapReason reason;

    static constexpr auto native_symbol() noexcept -> TargetSymbol {
        return TargetSymbol::StdAbort;
    }
};

// Realization resolves this transfer before sealing the native target unit.
struct TargetPendingExitStmt final {
    std::size_t identity;
};

struct TargetGotoStmt final {
    TargetIdentifier label;
};

struct TargetLabelStmt final {
    TargetIdentifier label;
};

struct TargetIfStmt final {
    TargetExpr condition;
    std::vector<TargetStmt> body;
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
    TargetLocalID local;
    TargetTypeID type;
    TargetExpr range;
    std::vector<TargetStmt> body;
};

struct TargetStatementCleanup;
using TargetStmtValue = TreeValue<
    TargetStatementCleanup,
    TargetExprStmt,
    TargetDiscardStmt,
    TargetReturnStmt,
    TargetCoReturnStmt,
    TargetVariableStmt,
    TargetBlockStmt,
    TargetAssignmentStmt,
    TargetUpdateStmt,
    TargetBreakStmt,
    TargetContinueStmt,
    TargetUnreachableStmt,
    TargetRuntimeTrapStmt,
    TargetPendingExitStmt,
    TargetGotoStmt,
    TargetLabelStmt,
    TargetIfStmt,
    TargetWhileStmt,
    TargetForStmt,
    TargetRangeForStmt>;

struct TargetStatementCleanup final {
    static auto clear(TargetStmtValue& value) noexcept -> void;
};

struct TargetStmt final {
    TargetStmtValue value;
    TargetAttribution attribution;
};

auto discard_statement(TargetExpr expression) noexcept -> TargetStmt;
