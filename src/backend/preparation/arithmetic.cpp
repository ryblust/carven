module carven:backend.preparation.arithmetic.impl;

import :backend.preparation.arithmetic;
import :semantic.semir.evaluation;
import std;

namespace {

auto integer_width(const SemIRProgram& program, TypeID type) noexcept
    -> std::optional<std::uint8_t> {
    const auto* builtin = std::get_if<BuiltinTypeValue>(&program.types().type(type).value);
    return builtin == nullptr ? std::nullopt : builtin_integer_width(builtin->kind);
}

} // namespace

auto prepare_unary(const SemIRProgram& program, UnaryOperator operation, TypeID type) noexcept
    -> PreparedUnary {
    const auto width = integer_width(program, type);
    switch (operation) {
        case UnaryOperator::Negate:
            if (width) {
                return {
                    .operation = TargetSymbol::RuntimeIntegerNegate,
                    .result_type = type,
                    .restore_result_type = false
                };
            }
            return {
                .operation = TargetPrefixOperator::Negate,
                .result_type = type,
                .restore_result_type = false
            };
        case UnaryOperator::LogicalNot:
            return {
                .operation = TargetPrefixOperator::LogicalNot,
                .result_type = type,
                .restore_result_type = false
            };
        case UnaryOperator::BitwiseNot:
            return {
                .operation = TargetPrefixOperator::BitwiseNot,
                .result_type = type,
                .restore_result_type = width && *width < std::numeric_limits<unsigned int>::digits
            };
    }
    std::unreachable();
}

auto prepare_binary(
    const SemIRProgram& program,
    BinaryOperator operation,
    TypeID type,
    std::optional<ConstantID> right
) noexcept -> PreparedBinary {
    const auto width = integer_width(program, type);
    const auto* builtin = std::get_if<BuiltinTypeValue>(&program.types().type(type).value);
    const auto unsigned_native = width
        && *width >= std::numeric_limits<unsigned int>::digits
        && !builtin_is_signed_integer(builtin->kind);
    const auto* divisor =
        right ? std::get_if<IntegerConstant>(&program.constants().constant(*right).value) : nullptr;
    const auto safe = !integer_operation_may_trap(program, operation, type, right);
    const auto native_division = width
        && safe
        && divisor != nullptr
        && (!builtin_is_signed_integer(builtin->kind)
            || !divisor->negative()
            || divisor->magnitude() != 1u);
    const auto runtime = [&](TargetSymbol symbol, bool typed) noexcept {
        return PreparedBinary {
            .operation = symbol,
            .result_type = type,
            .target_typed_operands = typed,
            .restore_result_type = false
        };
    };
    if (width) {
        switch (operation) {
            case BinaryOperator::Add:
                if (unsigned_native) {
                    break;
                }
                return runtime(TargetSymbol::RuntimeIntegerAdd, true);
            case BinaryOperator::Subtract:
                if (unsigned_native) {
                    break;
                }
                return runtime(TargetSymbol::RuntimeIntegerSubtract, true);
            case BinaryOperator::Multiply:
                if (unsigned_native) {
                    break;
                }
                return runtime(TargetSymbol::RuntimeIntegerMultiply, true);
            case BinaryOperator::Divide:
                if (native_division) {
                    break;
                }
                return runtime(TargetSymbol::RuntimeIntegerDivide, true);
            case BinaryOperator::Remainder:
                if (native_division) {
                    break;
                }
                return runtime(TargetSymbol::RuntimeIntegerRemainder, true);
            case BinaryOperator::LeftShift:
                if (safe && unsigned_native) {
                    break;
                }
                return runtime(TargetSymbol::RuntimeIntegerLeftShift, false);
            case BinaryOperator::RightShift:
                if (safe) {
                    break;
                }
                return runtime(TargetSymbol::RuntimeIntegerRightShift, false);
            default: break;
        }
    }
    const auto direct = [&](TargetBinaryOperator target, bool promoted = false) noexcept {
        return PreparedBinary {
            .operation = target,
            .result_type = type,
            .target_typed_operands = false,
            .restore_result_type =
                promoted && width && *width < std::numeric_limits<unsigned int>::digits
        };
    };
    switch (operation) {
        case BinaryOperator::BitwiseOr:    return direct(TargetBinaryOperator::BitwiseOr, true);
        case BinaryOperator::BitwiseXor:   return direct(TargetBinaryOperator::BitwiseXor, true);
        case BinaryOperator::BitwiseAnd:   return direct(TargetBinaryOperator::BitwiseAnd, true);
        case BinaryOperator::Equal:        return direct(TargetBinaryOperator::Equal);
        case BinaryOperator::NotEqual:     return direct(TargetBinaryOperator::NotEqual);
        case BinaryOperator::Less:         return direct(TargetBinaryOperator::Less);
        case BinaryOperator::LessEqual:    return direct(TargetBinaryOperator::LessEqual);
        case BinaryOperator::Greater:      return direct(TargetBinaryOperator::Greater);
        case BinaryOperator::GreaterEqual: return direct(TargetBinaryOperator::GreaterEqual);
        case BinaryOperator::LeftShift:    return direct(TargetBinaryOperator::LeftShift);
        case BinaryOperator::RightShift:   return direct(TargetBinaryOperator::RightShift, true);
        case BinaryOperator::Add:          return direct(TargetBinaryOperator::Add);
        case BinaryOperator::Subtract:     return direct(TargetBinaryOperator::Subtract);
        case BinaryOperator::Multiply:     return direct(TargetBinaryOperator::Multiply);
        case BinaryOperator::Divide:       return direct(TargetBinaryOperator::Divide, true);
        case BinaryOperator::Remainder:    return direct(TargetBinaryOperator::Remainder, true);
    }
    std::unreachable();
}
