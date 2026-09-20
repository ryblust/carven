module carven:semantic.semir.evaluation;

import :semantic.semir.constant;
import :semantic.semir.program;
import :semantic.semir.structured;
import std;

enum class EvaluationAction { None, Operands, ShortCircuit, Required };

// Borrowed operands describe executed
// children, not a second operation tree.
struct EvaluationRule final {
    EvaluationAction action;
    std::array<const SemanticExpression*, 2> operands;
};

auto known_boolean(const ConstantStore& constants, const SemanticExpression& expression) noexcept
    -> std::optional<bool>;

auto known_boolean(const SemIRProgram& semantic, const SemanticExpression& expression) noexcept
    -> std::optional<bool>;

// Integer checks are independent of the C++ implementation used to produce a value.
auto integer_operation_may_trap(
    const SemIRProgram& semantic,
    BinaryOperator operation,
    TypeID type,
    std::optional<ConstantID> right
) noexcept -> bool;

auto evaluation_rule(const SemIRProgram& semantic, const SemanticExpression& expression) noexcept
    -> EvaluationRule;
