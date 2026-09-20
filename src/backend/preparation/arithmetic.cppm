module carven:backend.preparation.arithmetic;

import :backend.target.expr;
import :backend.target.symbol;
import :semantic.semir.operation;
import :semantic.semir.program;
import std;

struct PreparedUnary final {
    std::variant<TargetPrefixOperator, TargetSymbol> operation;
    TypeID result_type;
    bool restore_result_type;
};

struct PreparedBinary final {
    std::variant<TargetBinaryOperator, TargetSymbol> operation;
    TypeID result_type;
    // The selected implementation fixes both operands' native types. Shift
    // counts retain their own type and therefore do not use this context.
    bool target_typed_operands;
    bool restore_result_type;
};

auto prepare_unary(const SemIRProgram& program, UnaryOperator operation, TypeID type) noexcept
    -> PreparedUnary;
auto prepare_binary(
    const SemIRProgram& program,
    BinaryOperator operation,
    TypeID type,
    std::optional<ConstantID> right = std::nullopt
) noexcept -> PreparedBinary;
