module carven:backend.realization.operation;

import :backend.lowering.context;
import :backend.target.expr;
import :semantic.semir;
import std;

// Prepared operands preserve their use contract's native type and value category.
// The body realizer must stabilize residual evaluations whose observable order
// would change in C++ (including declaration-order struct fields and unordered
// call arguments). Preparing an expression tree alone does not establish this.
// Operand owners remain alive through their source full-expression delivery.
// Binding storage, structured control and array adoption belong to the body
// realizer. Calls return the raw invocation/Outcome, without completion checks.
auto realize_operation(
    ModuleLowering& context,
    const SemanticExpression& source,
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

auto realize_callable_adaptation(
    ModuleLowering& context,
    TargetExpr input,
    TypeID from,
    TypeID to
) noexcept -> TargetExpr;
