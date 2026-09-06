module carven:semantic.analysis.operations.impl;

import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.ast.storage;
import :frontend.literal;
import :semantic.analysis.operations;
import :semantic.semir.body;
import :semantic.semir.decl;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto builtin_type(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<BuiltinType> {
    const auto* concrete = std::get_if<TypeID>(&type);
    if (concrete == nullptr) {
        return std::nullopt;
    }
    const auto value = draft.type_copy(*concrete);
    const auto* builtin = std::get_if<BuiltinTypeValue>(&value.value);
    return builtin == nullptr ? std::nullopt : std::optional(builtin->kind);
}

auto operation_error(std::string_view message, DiagnosticCode code) noexcept
    -> std::unexpected<OperationDiagnostic> {
    return std::unexpected(OperationDiagnostic {.message = message, .code = code});
}

enum class ContextualOperandKind {
    None,
    NumericLiteral,
    EnumCase,
};

auto contextual_operand_kind(const ASTView& ast, ASTExprID id) noexcept -> ContextualOperandKind {
    return std::visit(
        [&]<typename Form>(const Form& form) noexcept -> ContextualOperandKind {
            if constexpr (std::same_as<Form, ASTLiteral>) {
                const auto unsuffixed = std::visit(
                    []<typename Value>(const Value& value) static noexcept {
                        if constexpr (std::same_as<Value, IntegerLiteralValue>
                                      || std::same_as<Value, FloatingLiteralValue>) {
                            return value.suffix == NumericSuffix::None;
                        } else if constexpr (std::same_as<Value, StringLiteralValue>
                                             || std::same_as<Value, CharacterLiteralValue>
                                             || std::same_as<Value, BooleanLiteralValue>) {
                            return false;
                        } else {
                            static_assert(
                                std::same_as<Value, void>,
                                "new literal form needs a contextual operand policy"
                            );
                        }
                    },
                    form.value
                );
                return unsuffixed ? ContextualOperandKind::NumericLiteral
                                  : ContextualOperandKind::None;
            } else if constexpr (std::same_as<Form, ASTContextualCaseExpr>) {
                return ContextualOperandKind::EnumCase;
            } else if constexpr (std::same_as<Form, ASTCallExpr>) {
                return std::holds_alternative<ASTContextualCaseExpr>(
                           ast.expression(form.callee).value
                       )
                    ? ContextualOperandKind::EnumCase
                    : ContextualOperandKind::None;
            } else if constexpr (std::same_as<Form, ASTCppNameExpr>
                                 || std::same_as<Form, ASTNameExpr>
                                 || std::same_as<Form, ASTGroupExpr>
                                 || std::same_as<Form, ASTArrayExpr>
                                 || std::same_as<Form, ASTConstructionExpr>
                                 || std::same_as<Form, ASTPrefixExpr>
                                 || std::same_as<Form, ASTAccessExpr>
                                 || std::same_as<Form, ASTBinaryExpr>
                                 || std::same_as<Form, ASTCastExpr>
                                 || std::same_as<Form, ASTIndexExpr>
                                 || std::same_as<Form, ASTMemberExpr>
                                 || std::same_as<Form, ASTLambdaExpr>
                                 || std::same_as<Form, ASTPropagationExpr>
                                 || std::same_as<Form, ASTIfForm>
                                 || std::same_as<Form, ASTMatchForm>
                                 || std::same_as<Form, ASTTryForm>) {
                return ContextualOperandKind::None;
            } else {
                static_assert(
                    std::same_as<Form, void>,
                    "new expression form needs a contextual operand policy"
                );
            }
        },
        ast.expression(id).value
    );
}

struct ArrayShape final {
    ConstructionTypeRef element;
    std::uint64_t extent;
};

struct CallableShape final {
    std::optional<TypeID> owning_type;
    std::vector<ConstructionCallableParameter> parameters;
    ConstructionTypeRef result;
};

auto array_shape(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<ArrayShape> {
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        const auto canonical = draft.type_copy(*concrete);
        const auto* array = std::get_if<ArrayTypeValue>(&canonical.value);
        return array == nullptr ? std::nullopt
                                : std::optional(
                                      ArrayShape {
                                          .element = array->element,
                                          .extent = array->extent,
                                      }
                                  );
    }
    const auto construction = draft.construction_type_copy(std::get<TypeTermID>(type));
    const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value);
    return array == nullptr ? std::nullopt
                            : std::optional(
                                  ArrayShape {
                                      .element = array->element,
                                      .extent = array->extent,
                                  }
                              );
}

auto callable_shape(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<CallableShape> {
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        const auto canonical = draft.type_copy(*concrete);
        auto callable = std::optional<CallableID>();
        if (const auto* function = std::get_if<FunctionTypeValue>(&canonical.value)) {
            callable = function->callable;
        } else if (const auto* closure = std::get_if<ClosureTypeValue>(&canonical.value)) {
            callable = closure->callable;
        }
        if (callable.has_value()) {
            const auto contract = draft.construction_callable_contract_copy(*callable);
            return CallableShape {
                .owning_type = *concrete,
                .parameters = contract.parameters,
                .result = contract.result,
            };
        }
        const auto* view = std::get_if<CallableViewTypeValue>(&canonical.value);
        if (view == nullptr) {
            return std::nullopt;
        }
        const auto signature = draft.callable_signature_copy(view->signature);
        auto parameters = std::vector<ConstructionCallableParameter>();
        parameters.reserve(signature.parameters.size());
        for (const auto& parameter : signature.parameters) {
            parameters.push_back({
                .access = parameter.access,
                .type = parameter.type,
            });
        }
        return CallableShape {
            .owning_type = std::nullopt,
            .parameters = std::move(parameters),
            .result = signature.result,
        };
    }

    const auto construction = draft.construction_type_copy(std::get<TypeTermID>(type));
    const auto* view = std::get_if<ConstructionCallableViewTypeValue>(&construction.value);
    return view == nullptr ? std::nullopt
                           : std::optional(
                                 CallableShape {
                                     .owning_type = std::nullopt,
                                     .parameters = view->parameters,
                                     .result = view->result,
                                 }
                             );
}

auto shapes_compatible(
    const ProgramDraft& draft,
    ConstructionTypeRef left,
    ConstructionTypeRef right,
    std::flat_set<std::pair<ConstructionTypeRef, ConstructionTypeRef>>& visited
) noexcept -> bool {
    if (left == right) {
        return true;
    }
    if (!visited.emplace(left, right).second) {
        return true;
    }

    const auto left_array = array_shape(draft, left);
    const auto right_array = array_shape(draft, right);
    if (left_array.has_value() || right_array.has_value()) {
        return left_array.has_value()
            && right_array.has_value()
            && left_array->extent == right_array->extent
            && shapes_compatible(draft, left_array->element, right_array->element, visited);
    }

    const auto left_callable = callable_shape(draft, left);
    const auto right_callable = callable_shape(draft, right);
    if (left_callable.has_value() || right_callable.has_value()) {
        if (!left_callable.has_value() || !right_callable.has_value()) {
            return false;
        }
        if (left_callable->owning_type.has_value() && right_callable->owning_type.has_value()) {
            return left_callable->owning_type == right_callable->owning_type;
        }
        if (left_callable->parameters.size() != right_callable->parameters.size()
            || !shapes_compatible(draft, left_callable->result, right_callable->result, visited)) {
            return false;
        }
        return std::ranges::equal(
            left_callable->parameters,
            right_callable->parameters,
            [&](const auto& left_parameter, const auto& right_parameter) noexcept {
                return left_parameter.access == right_parameter.access
                    && shapes_compatible(draft, left_parameter.type, right_parameter.type, visited);
            }
        );
    }

    const auto* left_concrete = std::get_if<TypeID>(&left);
    const auto* right_concrete = std::get_if<TypeID>(&right);
    if (left_concrete == nullptr || right_concrete == nullptr) {
        return false;
    }
    return draft.type_copy(*left_concrete) == draft.type_copy(*right_concrete);
}

} // namespace

auto semantic_operator(ASTPrefixOperator op) noexcept -> UnaryOperator {
    switch (op) {
        case ASTPrefixOperator::LogicalNot: return UnaryOperator::LogicalNot;
        case ASTPrefixOperator::Negate:     return UnaryOperator::Negate;
        case ASTPrefixOperator::BitwiseNot: return UnaryOperator::BitwiseNot;
    }
    std::unreachable();
}

auto semantic_operator(ASTBinaryOperator op) noexcept -> std::optional<BinaryOperator> {
    switch (op) {
        case ASTBinaryOperator::LogicalOr:
        case ASTBinaryOperator::LogicalAnd:   return std::nullopt;
        case ASTBinaryOperator::BitwiseOr:    return BinaryOperator::BitwiseOr;
        case ASTBinaryOperator::BitwiseXor:   return BinaryOperator::BitwiseXor;
        case ASTBinaryOperator::BitwiseAnd:   return BinaryOperator::BitwiseAnd;
        case ASTBinaryOperator::Equal:        return BinaryOperator::Equal;
        case ASTBinaryOperator::NotEqual:     return BinaryOperator::NotEqual;
        case ASTBinaryOperator::Less:         return BinaryOperator::Less;
        case ASTBinaryOperator::LessEqual:    return BinaryOperator::LessEqual;
        case ASTBinaryOperator::Greater:      return BinaryOperator::Greater;
        case ASTBinaryOperator::GreaterEqual: return BinaryOperator::GreaterEqual;
        case ASTBinaryOperator::LeftShift:    return BinaryOperator::LeftShift;
        case ASTBinaryOperator::RightShift:   return BinaryOperator::RightShift;
        case ASTBinaryOperator::Add:          return BinaryOperator::Add;
        case ASTBinaryOperator::Subtract:     return BinaryOperator::Subtract;
        case ASTBinaryOperator::Multiply:     return BinaryOperator::Multiply;
        case ASTBinaryOperator::Divide:       return BinaryOperator::Divide;
        case ASTBinaryOperator::Remainder:    return BinaryOperator::Remainder;
    }
    std::unreachable();
}

auto binary_operator_requires_equality(ASTBinaryOperator op) noexcept -> bool {
    return op == ASTBinaryOperator::Equal || op == ASTBinaryOperator::NotEqual;
}

auto binary_operand_plan(const ASTView& ast, const ASTBinaryExpr& expression) noexcept
    -> BinaryOperandPlan {
    const auto equality = binary_operator_requires_equality(expression.op);
    const auto left_kind = contextual_operand_kind(ast, expression.left);
    const auto right_kind = contextual_operand_kind(ast, expression.right);
    const auto left_case = left_kind == ContextualOperandKind::EnumCase;
    const auto right_case = right_kind == ContextualOperandKind::EnumCase;
    if (equality && left_case && !right_case) {
        return BinaryOperandPlan::LeftExpectedFromRight;
    }
    if (equality && right_case && !left_case) {
        return BinaryOperandPlan::RightExpectedFromLeft;
    }
    const auto left_numeric = left_kind == ContextualOperandKind::NumericLiteral;
    const auto right_numeric = right_kind == ContextualOperandKind::NumericLiteral;
    if (left_numeric && !right_numeric) {
        return BinaryOperandPlan::LeftExpectedFromRight;
    }
    if (right_numeric) {
        return BinaryOperandPlan::RightExpectedFromLeft;
    }
    return BinaryOperandPlan::Independent;
}

auto operator_result_builtin(OperatorResult result) noexcept -> std::optional<BuiltinType> {
    switch (result) {
        case OperatorResult::Operand: return std::nullopt;
        case OperatorResult::Boolean: return BuiltinType::Bool;
    }
    std::unreachable();
}

auto select_contextual_numeric_type(
    const ProgramDraft& draft,
    TypeID inferred,
    std::optional<ConstructionTypeRef> expected,
    NumericSuffix suffix
) noexcept -> TypeID {
    if (!expected.has_value() || suffix != NumericSuffix::None) {
        return inferred;
    }
    const auto* concrete = std::get_if<TypeID>(&*expected);
    if (concrete == nullptr) {
        return inferred;
    }
    const auto inferred_builtin = builtin_type(draft, ConstructionTypeRef {inferred});
    const auto expected_builtin = builtin_type(draft, ConstructionTypeRef {*concrete});
    if (!inferred_builtin.has_value() || !expected_builtin.has_value()) {
        return inferred;
    }
    const auto both_numeric =
        builtin_is_numeric(*inferred_builtin) && builtin_is_numeric(*expected_builtin);
    const auto same_category =
        builtin_is_integer(*inferred_builtin) == builtin_is_integer(*expected_builtin);
    return both_numeric && same_category ? *concrete : inferred;
}

auto builtin_type_supports_equality(BuiltinType type) noexcept -> bool {
    switch (type) {
        case BuiltinType::Bool:
        case BuiltinType::Char:
        case BuiltinType::I8:
        case BuiltinType::I16:
        case BuiltinType::I32:
        case BuiltinType::I64:
        case BuiltinType::U8:
        case BuiltinType::U16:
        case BuiltinType::U32:
        case BuiltinType::U64:
        case BuiltinType::Isize:
        case BuiltinType::Usize:
        case BuiltinType::F32:
        case BuiltinType::F64:
        case BuiltinType::Str:          return true;
        case BuiltinType::StrBytesView:
        case BuiltinType::StrCharsView:
        case BuiltinType::Void:
        case BuiltinType::EntryArgs:    return false;
    }
    std::unreachable();
}

auto type_shapes_compatible(
    const ProgramDraft& draft,
    ConstructionTypeRef left,
    ConstructionTypeRef right
) noexcept -> bool {
    auto visited = std::flat_set<std::pair<ConstructionTypeRef, ConstructionTypeRef>>();
    return shapes_compatible(draft, left, right, visited);
}

auto type_contains_callable_view(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> bool {
    if (const auto* term = std::get_if<TypeTermID>(&type)) {
        const auto construction = draft.construction_type_copy(*term);
        if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value)) {
            return type_contains_callable_view(draft, array->element);
        }
        return std::holds_alternative<ConstructionCallableViewTypeValue>(construction.value);
    }
    const auto concrete = draft.type_copy(std::get<TypeID>(type));
    if (const auto* array = std::get_if<ArrayTypeValue>(&concrete.value)) {
        return type_contains_callable_view(draft, ConstructionTypeRef {array->element});
    }
    return std::holds_alternative<CallableViewTypeValue>(concrete.value);
}

auto type_supports_equality(const ProgramDraft& draft, ConstructionTypeRef type) noexcept -> bool {
    const auto* concrete = std::get_if<TypeID>(&type);
    if (concrete == nullptr) {
        const auto construction = draft.construction_type_copy(std::get<TypeTermID>(type));
        if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value)) {
            return type_supports_equality(draft, array->element);
        }
        return false;
    }
    return std::visit(
        Overloaded {
            [](const BuiltinTypeValue& value) noexcept {
                return builtin_type_supports_equality(value.kind);
            },
            [&](const StructTypeValue& value) noexcept {
                return draft.construction_struct_declaration_copy(value.structure)
                    .capabilities.equality;
            },
            [&](const EnumTypeValue& value) noexcept {
                return draft.construction_enum_declaration_copy(value.enumeration)
                    .capabilities.equality;
            },
            [&](const ArrayTypeValue& value) noexcept {
                return type_supports_equality(draft, ConstructionTypeRef {value.element});
            },
            [](const FunctionTypeValue&) static noexcept { return false; },
            [](const ClosureTypeValue&) static noexcept { return false; },
            [](const CallableViewTypeValue&) static noexcept { return false; },
            [](const CppTypeValue&) static noexcept { return false; },
        },
        draft.type_copy(*concrete).value
    );
}

auto decide_unary_operator(
    const ProgramDraft& draft,
    UnaryOperator op,
    ConstructionTypeRef operand
) noexcept -> OperatorDecision {
    const auto builtin = builtin_type(draft, operand);
    switch (op) {
        case UnaryOperator::LogicalNot:
            if (builtin != BuiltinType::Bool) {
                return operation_error(
                    "logical negation requires a bool operand",
                    DiagnosticCode::TypePrefixBool
                );
            }
            return OperatorResult::Boolean;
        case UnaryOperator::Negate:
            if (!builtin.has_value() || !builtin_is_numeric(*builtin)) {
                return operation_error(
                    "arithmetic negation requires a numeric operand",
                    DiagnosticCode::TypePrefixNumeric
                );
            }
            return OperatorResult::Operand;
        case UnaryOperator::BitwiseNot:
            if (!builtin.has_value() || !builtin_is_integer(*builtin)) {
                return operation_error(
                    "bitwise negation requires an integer operand",
                    DiagnosticCode::TypePrefixInteger
                );
            }
            return OperatorResult::Operand;
    }
    std::unreachable();
}

auto decide_binary_operator(
    const ProgramDraft& draft,
    BinaryOperator op,
    ConstructionTypeRef left,
    ConstructionTypeRef right,
    bool operands_compatible,
    bool equality_capable
) noexcept -> OperatorDecision {
    if (!operands_compatible) {
        return operation_error(
            "binary operands have incompatible types",
            DiagnosticCode::TypeBinary
        );
    }
    const auto equality = op == BinaryOperator::Equal || op == BinaryOperator::NotEqual;
    if (equality && !equality_capable) {
        return operation_error(
            "operand type does not support structural equality",
            DiagnosticCode::TypeEqualityUnsupported
        );
    }
    const auto left_builtin = builtin_type(draft, left);
    const auto right_builtin = builtin_type(draft, right);
    switch (op) {
        case BinaryOperator::Add:
        case BinaryOperator::Subtract:
        case BinaryOperator::Multiply:
        case BinaryOperator::Divide:
            if (!left_builtin.has_value()
                || !right_builtin.has_value()
                || !builtin_is_numeric(*left_builtin)
                || !builtin_is_numeric(*right_builtin)) {
                return operation_error(
                    "arithmetic operands must have numeric types",
                    DiagnosticCode::TypeBinaryNumeric
                );
            }
            return OperatorResult::Operand;
        case BinaryOperator::Less:
        case BinaryOperator::LessEqual:
        case BinaryOperator::Greater:
        case BinaryOperator::GreaterEqual:
            if (!left_builtin.has_value()
                || !right_builtin.has_value()
                || !builtin_is_numeric(*left_builtin)
                || !builtin_is_numeric(*right_builtin)) {
                return operation_error(
                    "ordered comparison requires numeric operands",
                    DiagnosticCode::TypeBinaryOrdered
                );
            }
            return OperatorResult::Boolean;
        case BinaryOperator::BitwiseOr:
        case BinaryOperator::BitwiseXor:
        case BinaryOperator::BitwiseAnd:
        case BinaryOperator::LeftShift:
        case BinaryOperator::RightShift:
        case BinaryOperator::Remainder:
            if (!left_builtin.has_value()
                || !right_builtin.has_value()
                || !builtin_is_integer(*left_builtin)
                || !builtin_is_integer(*right_builtin)) {
                return operation_error(
                    "integer operator requires integer operands",
                    DiagnosticCode::TypeBinaryInteger
                );
            }
            return OperatorResult::Operand;
        case BinaryOperator::Equal:
        case BinaryOperator::NotEqual: return OperatorResult::Boolean;
    }
    std::unreachable();
}

auto decide_binary_operator(
    const ProgramDraft& draft,
    ASTBinaryOperator op,
    ConstructionTypeRef left,
    ConstructionTypeRef right,
    bool operands_compatible,
    bool equality_capable
) noexcept -> OperatorDecision {
    if (!operands_compatible) {
        return operation_error(
            "binary operands have incompatible types",
            DiagnosticCode::TypeBinary
        );
    }
    if (op == ASTBinaryOperator::LogicalOr || op == ASTBinaryOperator::LogicalAnd) {
        if (builtin_type(draft, left) != BuiltinType::Bool
            || builtin_type(draft, right) != BuiltinType::Bool) {
            return operation_error(
                "logical operands must have type bool",
                DiagnosticCode::TypeLogicalBool
            );
        }
        return OperatorResult::Boolean;
    }
    const auto semantic = semantic_operator(op);
    if (!semantic.has_value()) {
        invariant_violation("non-logical binary operator has no SemIR operation");
    }
    return decide_binary_operator(draft, *semantic, left, right, true, equality_capable);
}

auto decide_cast(
    const ProgramDraft& draft,
    ConstructionTypeRef source,
    ConstructionTypeRef target,
    bool source_is_numeric_enum
) noexcept -> CastDecision {
    if (source == target) {
        return CastKind::Identity;
    }
    const auto source_builtin = builtin_type(draft, source);
    const auto target_builtin = builtin_type(draft, target);
    const auto source_integer = source_builtin.has_value() && builtin_is_integer(*source_builtin);
    const auto target_integer = target_builtin.has_value() && builtin_is_integer(*target_builtin);
    auto kind = std::optional<CastKind>();
    if (source_integer && target_integer) {
        kind = CastKind::IntegerToInteger;
    } else if (source_integer && target_builtin == BuiltinType::Bool) {
        kind = CastKind::IntegerToBool;
    } else if (source_builtin == BuiltinType::Bool && target_integer) {
        kind = CastKind::BoolToInteger;
    } else if (source_integer
               && (target_builtin == BuiltinType::F32 || target_builtin == BuiltinType::F64)) {
        kind = CastKind::IntegerToFloating;
    } else if (source_builtin == BuiltinType::F32 && target_builtin == BuiltinType::F64) {
        kind = CastKind::FloatingWiden;
    } else if (source_is_numeric_enum && target_integer) {
        kind = CastKind::EnumToInteger;
    }
    if (kind.has_value()) {
        return *kind;
    }
    return operation_error("invalid 'as' conversion", DiagnosticCode::TypeCast);
}

auto decide_text_method(
    const ProgramDraft& draft,
    ConstructionTypeRef operand,
    std::string_view name,
    std::size_t argument_count
) noexcept -> TextMethodDecision {
    auto intrinsic = TextIntrinsic::Len;
    if (name == "len") {
        intrinsic = TextIntrinsic::Len;
    } else if (name == "is_empty") {
        intrinsic = TextIntrinsic::IsEmpty;
    } else {
        return operation_error(
            name == "bytes" || name == "chars" ? "str bytes/chars are properties, not functions"
                                               : "str has no such method",
            DiagnosticCode::TypeStrMethod
        );
    }
    if (argument_count != 0) {
        return operation_error(
            "str text methods take no arguments",
            DiagnosticCode::TypeStrMethodArity
        );
    }
    if (builtin_type(draft, operand) != BuiltinType::Str) {
        return std::optional<TextIntrinsic>();
    }
    return std::optional(intrinsic);
}

auto decide_text_property(std::string_view name) noexcept -> TextIntrinsicDecision {
    if (name == "bytes") {
        return TextIntrinsic::Bytes;
    }
    if (name == "chars") {
        return TextIntrinsic::Chars;
    }
    return operation_error(
        name == "len" || name == "is_empty" ? "str len/is_empty must be called"
                                            : "str has no such property",
        DiagnosticCode::TypeStrProperty
    );
}

auto text_intrinsic_result(TextIntrinsic intrinsic) noexcept -> BuiltinType {
    switch (intrinsic) {
        case TextIntrinsic::Len:     return BuiltinType::Usize;
        case TextIntrinsic::IsEmpty: return BuiltinType::Bool;
        case TextIntrinsic::Bytes:   return BuiltinType::StrBytesView;
        case TextIntrinsic::Chars:   return BuiltinType::StrCharsView;
    }
    std::unreachable();
}
