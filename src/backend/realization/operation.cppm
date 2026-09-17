module carven:backend.realization.operation;

import :backend.lowering.context;
import :backend.preparation;
import :backend.target.expr;
import :backend.target.stmt;
import :semantic.semir.ids;
import :semantic.semir.operation;
import :semantic.semir.structured;
import std;

// Prepared operands preserve their use contract's native type and value category.
// The body realizer must stabilize residual evaluations whose observable order
// would change in C++ (including declaration-order struct fields and unordered
// call arguments). Preparing an expression tree alone does not establish this.
// Operand owners remain alive through their source full-expression delivery.
// Binding storage and structured control belong to the body realizer.
// Calls return the raw invocation/Outcome, without completion checks.
auto realize_operation(
    ModuleLowering& context,
    const SemanticExpression& source,
    const OperationPreparation* preparation,
    std::vector<TargetExpr> operands
) noexcept -> TargetExpr;

auto realize_unary(
    ModuleLowering& context,
    UnaryOperator operation,
    TargetExpr operand,
    TypeID type
) noexcept -> TargetExpr;

auto realize_binary(
    ModuleLowering& context,
    TargetExpr left,
    BinaryOperator operation,
    TargetExpr right,
    TypeID type
) noexcept -> TargetExpr;

// Scalar and array adaptation share the source callable policy. Array inputs
// must already be stabilized by the body realizer.
auto realize_callable_adaptation(
    ModuleLowering& context,
    TargetExpr input,
    TypeID from,
    TypeID to
) noexcept -> TargetExpr;

// Execution and operand sequencing are already established by the caller.
auto discarded_operation(
    const ModuleLowering& context,
    const SemanticExpression& source,
    TargetExpr expression
) noexcept -> TargetStmt;
