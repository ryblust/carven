module carven:semantic.analysis.operations.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.ast.storage;
import :frontend.literal;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.generic;
import :semantic.semir.vector;
import :semantic.semir.simd;
import :semantic.semir.slice;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto builtin_type(const CanonicalTypeStore& types, TypeID type) noexcept
    -> std::optional<BuiltinType> {
    const auto* value = std::get_if<BuiltinTypeValue>(&types.type(type).value);
    return value == nullptr ? std::nullopt : std::optional(value->kind);
}

auto builtin_type(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<BuiltinType> {
    type = unwrap_type_alias(draft, type);
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
    NullPointer,
};

auto contextual_operand_kind(const ASTView& ast, ASTExprID id) noexcept -> ContextualOperandKind {
    return ast.expression(id).value.visit(
        [&]<typename Form>(const Form& form) noexcept -> ContextualOperandKind {
            if constexpr (std::same_as<Form, ASTLiteral>) {
                if (std::holds_alternative<NullPointerLiteralValue>(form.value)) {
                    return ContextualOperandKind::NullPointer;
                }
                const auto unsuffixed =
                    form.value.visit([]<typename Value>(const Value& value) static noexcept {
                        if constexpr (std::same_as<Value, IntegerLiteralValue>
                                      || std::same_as<Value, FloatingLiteralValue>) {
                            return value.suffix == NumericSuffix::None;
                        } else if constexpr (std::same_as<Value, StringLiteralValue>
                                             || std::same_as<Value, CStringLiteralValue>
                                             || std::same_as<Value, CharacterLiteralValue>
                                             || std::same_as<Value, BooleanLiteralValue>
                                             || std::same_as<Value, NullPointerLiteralValue>) {
                            return false;
                        } else {
                            static_assert(
                                std::same_as<Value, void>,
                                "new literal form needs a contextual operand policy"
                            );
                        }
                    });
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
            } else if constexpr (std::same_as<Form, ASTGroupExpr>) {
                const auto inner = contextual_operand_kind(ast, form.expression);
                return inner == ContextualOperandKind::NullPointer ? inner
                                                                   : ContextualOperandKind::None;
            } else if constexpr (std::same_as<Form, ASTInterpolationExpr>
                                 || std::same_as<Form, ASTCppNameExpr>
                                 || std::same_as<Form, ASTNameExpr>
                                 || std::same_as<Form, ASTTypeApplicationExpr>
                                 || std::same_as<Form, ASTArrayExpr>
                                 || std::same_as<Form, ASTTupleExpr>
                                 || std::same_as<Form, ASTTupleElementExpr>
                                 || std::same_as<Form, ASTArrayRepeatExpr>
                                 || std::same_as<Form, ASTConstructionExpr>
                                 || std::same_as<Form, ASTPrefixExpr>
                                 || std::same_as<Form, ASTAccessExpr>
                                 || std::same_as<Form, ASTRangeExpr>
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
        }
    );
}

struct ArrayShape final {
    ConstructionTypeRef element;
    std::uint64_t extent;
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
    if (const auto* generic = std::get_if<GenericTypeID>(&type)) {
        const auto expression = draft.generic_type_copy(*generic);
        if (const auto* concrete = std::get_if<TypeID>(&expression)) {
            return array_shape(draft, ConstructionTypeRef(*concrete));
        }
        if (const auto* array = std::get_if<GenericArrayType>(&expression)) {
            return ArrayShape {.element = array->element, .extent = array->extent};
        }
        return std::nullopt;
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

    const auto unwrapped_left = unwrap_type_alias(draft, left);
    const auto unwrapped_right = unwrap_type_alias(draft, right);
    if (unwrapped_left != left || unwrapped_right != right) {
        return shapes_compatible(draft, unwrapped_left, unwrapped_right, visited);
    }
    const auto operation_success =
        [&](ConstructionTypeRef type) noexcept -> std::optional<ConstructionTypeRef> {
        if (const auto* concrete = std::get_if<TypeID>(&type)) {
            const auto canonical = draft.type_copy(*concrete);
            if (const auto* operation = std::get_if<OperationTypeValue>(&canonical.value)) {
                return operation->success;
            }
        } else if (const auto* term = std::get_if<TypeTermID>(&type)) {
            const auto construction = draft.construction_type_copy(*term);
            if (const auto* operation =
                    std::get_if<ConstructionOperationTypeValue>(&construction.value)) {
                return operation->success;
            }
        }
        if (const auto* generic = std::get_if<GenericTypeID>(&type)) {
            const auto expression = draft.generic_type_copy(*generic);
            if (const auto* operation = std::get_if<GenericOperationType>(&expression)) {
                return operation->success;
            }
        }
        return std::nullopt;
    };
    const auto left_operation = operation_success(left);
    const auto right_operation = operation_success(right);
    if (left_operation || right_operation) {
        return left_operation
            && right_operation
            && shapes_compatible(draft, *left_operation, *right_operation, visited);
    }
    const auto left_array = array_shape(draft, left);
    const auto right_array = array_shape(draft, right);
    if (left_array.has_value() || right_array.has_value()) {
        return left_array.has_value()
            && right_array.has_value()
            && left_array->extent == right_array->extent
            && shapes_compatible(draft, left_array->element, right_array->element, visited);
    }

    const auto left_tuple = tuple_elements(draft, left);
    const auto right_tuple = tuple_elements(draft, right);
    if (left_tuple || right_tuple) {
        return left_tuple
            && right_tuple
            && std::ranges::equal(
                   *left_tuple,
                   *right_tuple,
                   [&](ConstructionTypeRef a, ConstructionTypeRef b) noexcept {
                       return shapes_compatible(draft, a, b, visited);
                   }
            );
    }

    const auto left_slice = slice_element(draft, left);
    const auto right_slice = slice_element(draft, right);
    if (left_slice || right_slice) {
        return left_slice
            && right_slice
            && shapes_compatible(draft, *left_slice, *right_slice, visited);
    }

    const auto left_pointer = pointer_shape(draft, left);
    const auto right_pointer = pointer_shape(draft, right);
    if (left_pointer || right_pointer) {
        return left_pointer
            && right_pointer
            && left_pointer->access == right_pointer->access
            && shapes_compatible(draft, left_pointer->target, right_pointer->target, visited);
    }

    const auto left_callable = draft.callable_shape(left);
    const auto right_callable = draft.callable_shape(right);
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
                return left_parameter.stage == right_parameter.stage
                    && left_parameter.access == right_parameter.access
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
        case ASTPrefixOperator::Await:
            invariant_violation("await is not an ordinary unary operator");
        case ASTPrefixOperator::Dereference:
            invariant_violation("pointer dereference is not a scalar unary operator");
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

auto numeric_operand_plan(const ASTView& ast, ASTExprID left, ASTExprID right) noexcept
    -> BinaryOperandPlan {
    const auto left_numeric =
        contextual_operand_kind(ast, left) == ContextualOperandKind::NumericLiteral;
    const auto right_numeric =
        contextual_operand_kind(ast, right) == ContextualOperandKind::NumericLiteral;
    if (left_numeric && !right_numeric) {
        return BinaryOperandPlan::LeftExpectedFromRight;
    }
    if (right_numeric) {
        return BinaryOperandPlan::RightExpectedFromLeft;
    }
    return BinaryOperandPlan::Independent;
}

auto binary_operand_plan(const ASTView& ast, const ASTBinaryExpr& expression) noexcept
    -> BinaryOperandPlan {
    const auto equality = binary_operator_requires_equality(expression.op);
    const auto left_kind = contextual_operand_kind(ast, expression.left);
    const auto right_kind = contextual_operand_kind(ast, expression.right);
    if (equality
        && left_kind == ContextualOperandKind::NullPointer
        && right_kind != ContextualOperandKind::NullPointer) {
        return BinaryOperandPlan::LeftExpectedFromRight;
    }
    if (equality
        && right_kind == ContextualOperandKind::NullPointer
        && left_kind != ContextualOperandKind::NullPointer) {
        return BinaryOperandPlan::RightExpectedFromLeft;
    }
    const auto left_case = left_kind == ContextualOperandKind::EnumCase;
    const auto right_case = right_kind == ContextualOperandKind::EnumCase;
    if (equality && left_case && !right_case) {
        return BinaryOperandPlan::LeftExpectedFromRight;
    }
    if (equality && right_case && !left_case) {
        return BinaryOperandPlan::RightExpectedFromLeft;
    }
    return numeric_operand_plan(ast, expression.left, expression.right);
}

auto operator_result_builtin(OperatorResult result, std::optional<BuiltinType> operand) noexcept
    -> std::optional<BuiltinType> {
    switch (result) {
        case OperatorResult::Operand: return operand;
        case OperatorResult::Boolean: return BuiltinType::Bool;
        case OperatorResult::Mask:
            if (const auto layout = operand ? simd_layout(*operand) : std::nullopt) {
                return layout->mask;
            }
            invariant_violation("mask result requires a SIMD operand");
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
        case BuiltinType::String:
        case BuiltinType::Str:
        case BuiltinType::Void:         return true;
        case BuiltinType::U8x32:
        case BuiltinType::F32x8:
        case BuiltinType::Mask8:
        case BuiltinType::Mask32:
        case BuiltinType::U8x16:
        case BuiltinType::F32x4:
        case BuiltinType::Mask4:
        case BuiltinType::Mask16:
        case BuiltinType::StrCharsView: return false;
    }
    std::unreachable();
}

auto unwrap_type_alias(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> ConstructionTypeRef {
    if (const auto* generic = std::get_if<GenericTypeID>(&type)) {
        const auto expression = draft.generic_type_copy(*generic);
        if (const auto* concrete = std::get_if<TypeID>(&expression)) {
            return *concrete;
        }
    }
    return type;
}

auto pointer_shape(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<PointerShape> {
    type = unwrap_type_alias(draft, type);
    if (const auto* id = std::get_if<TypeID>(&type)) {
        const auto canonical = draft.type_copy(*id);
        if (const auto* pointer = std::get_if<PointerTypeValue>(&canonical.value)) {
            return PointerShape {pointer->target, pointer->access};
        }
    }
    if (const auto* generic = std::get_if<GenericTypeID>(&type)) {
        const auto expression = draft.generic_type_copy(*generic);
        if (const auto* pointer = std::get_if<GenericPointerType>(&expression)) {
            return PointerShape {pointer->target, pointer->access};
        }
    }
    return std::nullopt;
}

auto type_formation_status(
    const ProgramDraft& draft,
    TypeFormationRequirementKind requirement,
    ConstructionTypeRef type
) noexcept -> TypeFormationStatus {
    type = unwrap_type_alias(draft, type);
    switch (requirement) {
        case TypeFormationRequirementKind::NonVoidObject: {
            if (const auto* concrete = std::get_if<TypeID>(&type)) {
                const auto canonical = draft.type_copy(*concrete);
                if (const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value);
                    builtin && builtin->kind == BuiltinType::Void) {
                    return TypeFormationStatus::Rejected;
                }
                return std::holds_alternative<CppTypeValue>(canonical.value)
                    ? TypeFormationStatus::NativeDelegated
                    : TypeFormationStatus::Supported;
            }
            if (const auto* generic = std::get_if<GenericTypeID>(&type)) {
                const auto expression = draft.generic_type_copy(*generic);
                if (std::holds_alternative<GenericTypeParameter>(expression)) {
                    return TypeFormationStatus::Dependent;
                }

                if (std::holds_alternative<GenericCppNamedType>(expression)
                    || std::holds_alternative<GenericCppQueryType>(expression)) {
                    return TypeFormationStatus::NativeDelegated;
                }
                return TypeFormationStatus::Supported;
            }
            // Every deferred construction describes an object constructor;
            // its element or completion effects do not change that outer shape.
            static_cast<void>(draft.construction_type_copy(std::get<TypeTermID>(type)));
            return TypeFormationStatus::Supported;
        }
    }
    std::unreachable();
}

auto pointer_narrows(
    const ProgramDraft& draft,
    ConstructionTypeRef source,
    ConstructionTypeRef target
) noexcept -> bool {
    const auto from = pointer_shape(draft, source);
    const auto to = pointer_shape(draft, target);
    return from
        && to
        && from->access == PointerAccess::Write
        && to->access == PointerAccess::Read
        && unwrap_type_alias(draft, from->target) == unwrap_type_alias(draft, to->target);
}

auto type_shapes_compatible(
    const ProgramDraft& draft,
    ConstructionTypeRef left,
    ConstructionTypeRef right
) noexcept -> bool {
    auto visited = std::flat_set<std::pair<ConstructionTypeRef, ConstructionTypeRef>>();
    return shapes_compatible(draft, left, right, visited);
}

auto make_array_type(
    ProgramDraft& draft,
    ConstructionTypeRef element,
    std::uint64_t extent
) noexcept -> ConstructionTypeRef {
    element = unwrap_type_alias(draft, element);
    if (const auto* concrete = std::get_if<TypeID>(&element)) {
        return draft.intern_type(
            {.value = ArrayTypeValue {.element = *concrete, .extent = extent}}
        );
    }
    if (const auto* generic = std::get_if<GenericTypeID>(&element)) {
        return draft.intern_generic_type(GenericArrayType {.element = *generic, .extent = extent});
    }
    return draft.append_construction_type(
        {.value = ConstructionArrayTypeValue {.element = element, .extent = extent}}
    );
}

auto make_slice_type(ProgramDraft& draft, ConstructionTypeRef element) noexcept
    -> ConstructionTypeRef {
    element = unwrap_type_alias(draft, element);
    if (const auto* concrete = std::get_if<TypeID>(&element)) {
        return draft.intern_type({.value = SliceTypeValue {.element = *concrete}});
    }
    if (const auto* generic = std::get_if<GenericTypeID>(&element)) {
        return draft.intern_generic_type(GenericSliceType {.element = *generic});
    }
    return draft.append_construction_type(
        {.value = ConstructionSliceTypeValue {.element = element}}
    );
}

auto make_pointer_type(
    ProgramDraft& draft,
    ConstructionTypeRef target,
    PointerAccess access,
    ProgramOriginID origin
) noexcept -> AnalysisResult<ConstructionTypeRef> {
    target = unwrap_type_alias(draft, target);
    if (std::holds_alternative<TypeTermID>(target)) {
        const auto concrete = draft.try_canonicalize_declared_type(target);
        if (!concrete) {
            return std::unexpected(draft.diagnostics().error(
                DiagnosticBuilder(
                    DiagnosticCode::TypeUnresolved,
                    "addressof target requires a determined type and failure contract"
                )
                    .primary(draft.source_span(origin))
                    .build()
            ));
        }
        target = *concrete;
    }
    if (const auto* concrete = std::get_if<TypeID>(&target)) {
        return ConstructionTypeRef(
            draft.intern_type({.value = PointerTypeValue {*concrete, access}})
        );
    }
    return ConstructionTypeRef(draft.intern_generic_type(GenericPointerType {target, access}));
}

auto operation_shape(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<ConstructionOperationTypeValue> {
    type = unwrap_type_alias(draft, type);
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        const auto canonical = draft.type_copy(*concrete);
        if (const auto* operation = std::get_if<OperationTypeValue>(&canonical.value)) {
            return ConstructionOperationTypeValue {operation->success, operation->failures};
        }
    } else if (const auto* generic = std::get_if<GenericTypeID>(&type)) {
        const auto expression = draft.generic_type_copy(*generic);
        if (const auto* operation = std::get_if<GenericOperationType>(&expression)) {
            return ConstructionOperationTypeValue {
                unwrap_type_alias(draft, operation->success),
                operation->failures
            };
        }
    } else {
        const auto construction = draft.construction_type_copy(std::get<TypeTermID>(type));
        if (const auto* operation =
                std::get_if<ConstructionOperationTypeValue>(&construction.value)) {
            return *operation;
        }
    }
    return std::nullopt;
}

auto make_operation_type(
    ProgramDraft& draft,
    ConstructionTypeRef success,
    ConstructionFailureRef failures
) noexcept -> ConstructionTypeRef {
    success = unwrap_type_alias(draft, success);
    if (std::holds_alternative<GenericTypeID>(success)
        || std::holds_alternative<GenericFailureID>(failures)) {
        return draft.intern_generic_type(
            GenericOperationType {.success = success, .failures = failures}
        );
    }
    if (const auto* concrete = std::get_if<TypeID>(&success)) {
        if (const auto* known = std::get_if<FailureSetID>(&failures)) {
            return draft.intern_type({.value = OperationTypeValue {*concrete, *known}});
        }
    }
    return draft.append_construction_type(
        {.value = ConstructionOperationTypeValue {.success = success, .failures = failures}}
    );
}

auto make_tuple_type(ProgramDraft& draft, std::vector<ConstructionTypeRef> elements) noexcept
    -> ConstructionTypeRef {
    for (auto& element : elements) {
        element = unwrap_type_alias(draft, element);
    }
    if (elements.empty()) {
        return draft.builtin_type(BuiltinType::Void);
    }
    auto canonical = std::vector<TypeID>();
    for (const auto& element : elements) {
        if (const auto* id = std::get_if<TypeID>(&element)) {
            canonical.push_back(*id);
        }
    }
    if (canonical.size() == elements.size()) {
        return draft.intern_type({.value = TupleTypeValue {.elements = std::move(canonical)}});
    }
    if (std::ranges::any_of(elements, [](const auto& element) static noexcept {
            return std::holds_alternative<GenericTypeID>(element);
        })) {
        return draft.intern_generic_type(GenericTupleType {.elements = std::move(elements)});
    }
    return draft.append_construction_type(
        {.value = ConstructionTupleTypeValue {.elements = std::move(elements)}}
    );
}

auto tuple_elements(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<std::vector<ConstructionTypeRef>> {
    if (const auto* term = std::get_if<TypeTermID>(&type)) {
        const auto construction = draft.construction_type_copy(*term);
        if (const auto* tuple = std::get_if<ConstructionTupleTypeValue>(&construction.value)) {
            return tuple->elements;
        }
        return std::nullopt;
    }
    if (const auto* generic = std::get_if<GenericTypeID>(&type)) {
        const auto expression = draft.generic_type_copy(*generic);
        if (const auto* concrete = std::get_if<TypeID>(&expression)) {
            return tuple_elements(draft, ConstructionTypeRef(*concrete));
        }
        if (const auto* tuple = std::get_if<GenericTupleType>(&expression)) {
            return tuple->elements;
        }
        return std::nullopt;
    }
    const auto canonical = draft.type_copy(std::get<TypeID>(type));
    if (const auto* tuple = std::get_if<TupleTypeValue>(&canonical.value)) {
        return tuple->elements | std::views::transform([](TypeID element) static noexcept {
                   return ConstructionTypeRef {element};
               })
            | std::ranges::to<std::vector>();
    }
    if (canonical.value == CanonicalTypeValue {BuiltinTypeValue {.kind = BuiltinType::Void}}) {
        return std::vector<ConstructionTypeRef>();
    }
    return std::nullopt;
}

auto type_contains_callable_view(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> bool {
    if (const auto element = slice_element(draft, type)) {
        return type_contains_callable_view(draft, *element);
    }
    if (const auto elements = tuple_elements(draft, type)) {
        return std::ranges::any_of(*elements, [&](ConstructionTypeRef element) noexcept {
            return type_contains_callable_view(draft, element);
        });
    }
    if (const auto* term = std::get_if<TypeTermID>(&type)) {
        const auto construction = draft.construction_type_copy(*term);
        if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value)) {
            return type_contains_callable_view(draft, array->element);
        }
        return std::holds_alternative<ConstructionCallableViewTypeValue>(construction.value);
    }
    if (const auto* generic = std::get_if<GenericTypeID>(&type)) {
        const auto expression = draft.generic_type_copy(*generic);
        if (const auto* concrete = std::get_if<TypeID>(&expression)) {
            return type_contains_callable_view(draft, ConstructionTypeRef(*concrete));
        }
        if (const auto* array = std::get_if<GenericArrayType>(&expression)) {
            return type_contains_callable_view(draft, ConstructionTypeRef(array->element));
        }
        if (const auto* sequence = std::get_if<GenericVectorType>(&expression)) {
            return type_contains_callable_view(draft, ConstructionTypeRef(sequence->element));
        }
        if (const auto* native = std::get_if<GenericCppNamedType>(&expression)) {
            return std::ranges::any_of(
                native->arguments,
                [&](ConstructionTypeRef argument) noexcept {
                    return type_contains_callable_view(draft, argument);
                }
            );
        }
        return false;
    }
    const auto concrete = draft.type_copy(std::get<TypeID>(type));
    if (const auto* array = std::get_if<ArrayTypeValue>(&concrete.value)) {
        return type_contains_callable_view(draft, ConstructionTypeRef {array->element});
    }
    if (const auto* sequence = std::get_if<VectorTypeValue>(&concrete.value)) {
        return type_contains_callable_view(draft, ConstructionTypeRef {sequence->element});
    }
    if (const auto* native = std::get_if<CppTypeValue>(&concrete.value)) {
        if (const auto* named = std::get_if<CppNamedType>(&native->form)) {
            return std::ranges::any_of(named->arguments, [&](TypeID argument) noexcept {
                return type_contains_callable_view(draft, ConstructionTypeRef {argument});
            });
        }
    }
    return std::holds_alternative<CallableViewTypeValue>(concrete.value);
}

namespace {

template<typename EnumCapability, typename ElementCapability>
auto supports_equality(
    const CanonicalType& canonical,
    EnumCapability enumeration,
    ElementCapability element
) noexcept -> bool {
    return canonical.value.visit(
        Overloaded {
            [](const BuiltinTypeValue& value) static noexcept {
                return builtin_type_supports_equality(value.kind);
            },
            [](const StructTypeValue&) static noexcept { return false; },
            [&](const EnumTypeValue& value) noexcept { return enumeration(value.enumeration); },
            [&](const ArrayTypeValue& value) noexcept { return element(value.element); },
            [&](const TupleTypeValue& value) noexcept {
                return std::ranges::all_of(value.elements, element);
            },
            [](const FunctionTypeValue&) static noexcept { return false; },
            [](const ClosureTypeValue&) static noexcept { return false; },
            [](const CallableViewTypeValue&) static noexcept { return false; },
            [](const OperationTypeValue&) static noexcept { return false; },
            [](const CppTypeValue&) static noexcept { return false; },
            [](const PointerTypeValue&) static noexcept { return true; },
            [](const RangeTypeValue&) static noexcept { return false; },
            [](const SliceTypeValue&) static noexcept { return false; },
            [](const VectorTypeValue&) static noexcept { return false; },
        }
    );
}

} // namespace

auto type_supports_equality(const ProgramDraft& draft, ConstructionTypeRef type) noexcept -> bool {
    return draft.equality_support(std::array {type}).front();
}

auto type_supports_equality(
    const CanonicalTypeStore& types,
    const DeclarationStore& declarations,
    TypeID type
) noexcept -> bool {
    return supports_equality(
        types.type(type),
        [&](EnumID id) noexcept { return declarations.enumeration(id).supports_equality; },
        [&](TypeID id) noexcept { return type_supports_equality(types, declarations, id); }
    );
}

namespace {

auto decide_unary_builtin(UnaryOperator op, std::optional<BuiltinType> builtin) noexcept
    -> OperatorDecision {
    if (builtin) {
        if (const auto shape = simd_layout(*builtin)) {
            if ((shape->mask == *builtin || shape->element == BuiltinType::U8)
                && op == UnaryOperator::BitwiseNot) {
                return OperatorResult::Operand;
            }
            if (shape->vector == *builtin
                && shape->element == BuiltinType::F32
                && op == UnaryOperator::Negate) {
                return OperatorResult::Operand;
            }
        }
    }
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

auto decide_binary_builtins(
    BinaryOperator op,
    std::optional<BuiltinType> left_builtin,
    std::optional<BuiltinType> right_builtin,
    bool operands_compatible,
    bool equality_capable
) noexcept -> OperatorDecision {
    const auto bitwise = op == BinaryOperator::BitwiseOr
        || op == BinaryOperator::BitwiseAnd
        || op == BinaryOperator::BitwiseXor;
    const auto left_shape = left_builtin ? simd_layout(*left_builtin) : std::nullopt;
    const auto right_shape = right_builtin ? simd_layout(*right_builtin) : std::nullopt;
    const auto shape = left_shape ? left_shape : right_shape;
    if (shape) {
        const auto floating = shape->element == BuiltinType::F32;
        if (left_builtin == shape->mask && right_builtin == shape->mask) {
            if (bitwise) {
                return OperatorResult::Operand;
            }
            return operation_error(
                "masks support bitwise composition; use any/all for scalar predicates",
                DiagnosticCode::TypeBinary
            );
        }
        const auto vector_operands =
            (left_builtin == shape->vector
             && (right_builtin == shape->vector || right_builtin == shape->element))
            || (right_builtin == shape->vector && left_builtin == shape->element);
        if (vector_operands) {
            if (op == BinaryOperator::Add
                || op == BinaryOperator::Subtract
                || (floating && (op == BinaryOperator::Multiply || op == BinaryOperator::Divide))
                || (!floating && bitwise)) {
                return OperatorResult::Operand;
            }
            if (op == BinaryOperator::Equal
                || op == BinaryOperator::NotEqual
                || op == BinaryOperator::Less
                || op == BinaryOperator::LessEqual
                || op == BinaryOperator::Greater
                || op == BinaryOperator::GreaterEqual) {
                return OperatorResult::Mask;
            }
            return operation_error("unsupported SIMD operator", DiagnosticCode::TypeBinary);
        }
    }
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

auto decide_builtin_cast(
    bool identical,
    std::optional<BuiltinType> source_builtin,
    std::optional<BuiltinType> target_builtin,
    bool source_is_numeric_enum
) noexcept -> CastDecision {
    if (identical) {
        return CastKind::Identity;
    }
    const auto source_integer = source_builtin.has_value() && builtin_is_integer(*source_builtin);
    const auto target_integer = target_builtin.has_value() && builtin_is_integer(*target_builtin);
    auto kind = std::optional<CastKind>();
    if (source_builtin == BuiltinType::Char && target_builtin == BuiltinType::U32) {
        kind = CastKind::CharToU32;
    } else if (source_integer && target_integer) {
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

} // namespace

auto decide_unary_operator(
    const ProgramDraft& facts,
    UnaryOperator op,
    ConstructionTypeRef operand
) noexcept -> OperatorDecision {
    return decide_unary_builtin(op, builtin_type(facts, operand));
}

auto decide_binary_operator(
    const ProgramDraft& facts,
    BinaryOperator op,
    ConstructionTypeRef left,
    ConstructionTypeRef right,
    bool compatible,
    bool equality
) noexcept -> OperatorDecision {
    return decide_binary_builtins(
        op,
        builtin_type(facts, left),
        builtin_type(facts, right),
        compatible,
        equality
    );
}

auto decide_cast(
    const ProgramDraft& facts,
    ConstructionTypeRef source,
    ConstructionTypeRef target,
    bool numeric_enum
) noexcept -> CastDecision {
    if (pointer_narrows(facts, source, target)) {
        return CastKind::PointerRead;
    }
    return decide_builtin_cast(
        source == target,
        builtin_type(facts, source),
        builtin_type(facts, target),
        numeric_enum
    );
}

auto decide_unary_operator(
    const CanonicalTypeStore& facts,
    UnaryOperator op,
    TypeID operand
) noexcept -> OperatorDecision {
    return decide_unary_builtin(op, builtin_type(facts, operand));
}

auto decide_binary_operator(
    const CanonicalTypeStore& facts,
    BinaryOperator op,
    TypeID left,
    TypeID right,
    bool compatible,
    bool equality
) noexcept -> OperatorDecision {
    return decide_binary_builtins(
        op,
        builtin_type(facts, left),
        builtin_type(facts, right),
        compatible,
        equality
    );
}

auto decide_cast(
    const CanonicalTypeStore& facts,
    TypeID source,
    TypeID target,
    bool numeric_enum
) noexcept -> CastDecision {
    const auto* from = std::get_if<PointerTypeValue>(&facts.type(source).value);
    const auto* to = std::get_if<PointerTypeValue>(&facts.type(target).value);
    if (from
        && to
        && from->target == to->target
        && from->access == PointerAccess::Write
        && to->access == PointerAccess::Read) {
        return CastKind::PointerRead;
    }
    return decide_builtin_cast(
        source == target,
        builtin_type(facts, source),
        builtin_type(facts, target),
        numeric_enum
    );
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

auto array_element(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<ConstructionTypeRef> {
    const auto array = array_shape(draft, type);
    return array ? std::optional(array->element) : std::nullopt;
}

auto array_literal_element(
    const ProgramDraft& draft,
    std::optional<ConstructionTypeRef> expected,
    std::size_t extent
) noexcept -> std::expected<std::optional<ConstructionTypeRef>, OperationDiagnostic> {
    auto element = std::optional<ConstructionTypeRef>();
    if (expected) {
        if (const auto array = array_shape(draft, *expected)) {
            if (array->extent != extent) {
                return operation_error(
                    "array literal length differs from its expected type",
                    DiagnosticCode::TypeMismatch
                );
            }
            element = array->element;
        } else if (const auto shape = sequence_shape(draft, *expected)) {
            element = shape->element;
        }
    }
    if (extent == 0uz && !element) {
        return operation_error(
            "empty array literal requires an expected array type",
            DiagnosticCode::TypeEmptyArray
        );
    }
    return element;
}

auto slice_element(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<ConstructionTypeRef> {
    if (const auto* generic = std::get_if<GenericTypeID>(&type)) {
        const auto expression = draft.generic_type_copy(*generic);
        if (const auto* concrete = std::get_if<TypeID>(&expression)) {
            return slice_element(draft, ConstructionTypeRef(*concrete));
        }
        if (const auto* slice = std::get_if<GenericSliceType>(&expression)) {
            return slice->element;
        }
        return std::nullopt;
    }
    if (const auto* id = std::get_if<TypeID>(&type)) {
        const auto value = draft.type_copy(*id).value;
        if (const auto* slice = std::get_if<SliceTypeValue>(&value)) {
            return slice->element;
        }
    } else {
        const auto value = draft.construction_type_copy(std::get<TypeTermID>(type)).value;
        if (const auto* slice = std::get_if<ConstructionSliceTypeValue>(&value)) {
            return slice->element;
        }
    }
    return std::nullopt;
}

auto decide_slice_method(
    const ProgramDraft& draft,
    ConstructionTypeRef operand,
    std::string_view name,
    std::size_t arguments
) noexcept -> std::expected<std::optional<SliceIntrinsic>, OperationDiagnostic> {
    const auto array = array_shape(draft, operand).has_value();
    const auto view = slice_element(draft, operand).has_value();
    const auto shape = sequence_shape(draft, operand);
    const auto vector = shape && shape->kind == SequenceShapeKind::Vector;
    if ((!array && !view && !vector) || (vector && name != "slice")) {
        return std::optional<SliceIntrinsic>();
    }
    auto operation = std::optional<SliceIntrinsic>();
    if (name == "length") {
        operation = SliceIntrinsic::Length;
    }
    if (name == "is_empty") {
        operation = SliceIntrinsic::IsEmpty;
    }
    if (name == "slice") {
        operation = arguments == 0 ? SliceIntrinsic::View : SliceIntrinsic::Slice;
    }
    if (!operation) {
        return operation_error("sequence has no such method", DiagnosticCode::TypeMethodCall);
    }
    const auto contract = slice_intrinsic_contract(*operation);
    const auto receiver = array ? SliceIntrinsicShape::Array
        : vector                ? SliceIntrinsicShape::Vector
                                : SliceIntrinsicShape::Slice;
    if (*operation != SliceIntrinsic::Slice
        && !slice_intrinsic_accepts_receiver(contract.receiver, receiver)) {
        return operation_error("sequence has no such method", DiagnosticCode::TypeMethodCall);
    }
    if (arguments != contract.arguments.size()) {
        return operation_error(
            "sequence method argument count does not match",
            DiagnosticCode::TypeMethodCallArity
        );
    }
    return operation;
}

auto decide_vector_method(
    const ProgramDraft& draft,
    ConstructionTypeRef operand,
    std::string_view name,
    std::size_t arguments
) noexcept -> std::expected<std::optional<VectorIntrinsic>, OperationDiagnostic> {
    const auto shape = sequence_shape(draft, operand);
    if (!shape || shape->kind != SequenceShapeKind::Vector) {
        return std::optional<VectorIntrinsic>();
    }
    if (name == "slice") {
        return std::optional<VectorIntrinsic>();
    }
    auto intrinsic = VectorIntrinsic::Length;
    if (name == "length") {
        intrinsic = VectorIntrinsic::Length;
    } else if (name == "is_empty") {
        intrinsic = VectorIntrinsic::IsEmpty;
    } else if (name == "push") {
        intrinsic = VectorIntrinsic::Push;
    } else if (name == "remove") {
        intrinsic = VectorIntrinsic::Remove;
    } else if (name == "capacity") {
        intrinsic = VectorIntrinsic::Capacity;
    } else if (name == "reserve") {
        intrinsic = VectorIntrinsic::Reserve;
    } else if (name == "clear") {
        intrinsic = VectorIntrinsic::Clear;
    } else {
        return operation_error("Vector has no such method", DiagnosticCode::TypeMethodCall);
    }
    if (arguments != (vector_intrinsic_contract(intrinsic).argument ? 1uz : 0uz)) {
        return operation_error(
            "Vector method argument count does not match",
            DiagnosticCode::TypeMethodCallArity
        );
    }
    return std::optional(intrinsic);
}

auto decide_text_method(
    const ProgramDraft& draft,
    ConstructionTypeRef operand,
    std::string_view name,
    std::size_t argument_count
) noexcept -> TextMethodDecision {
    const auto owning = builtin_type(draft, operand) == BuiltinType::String;
    if (!owning && builtin_type(draft, operand) != BuiltinType::Str) {
        return std::optional<TextIntrinsic>();
    }
    auto intrinsic = TextIntrinsic::ByteLength;
    if (name == "byte_length") {
        intrinsic = TextIntrinsic::ByteLength;
    } else if (name == "is_empty") {
        intrinsic = TextIntrinsic::IsEmpty;
    } else if (owning && name == "as_str") {
        intrinsic = TextIntrinsic::AsStr;
    } else if (owning && name == "append") {
        intrinsic = TextIntrinsic::Append;
    } else if (owning && name == "push") {
        intrinsic = TextIntrinsic::Push;
    } else if (owning && name == "clear") {
        intrinsic = TextIntrinsic::Clear;
    } else {
        return operation_error(
            name == "bytes" || name == "chars" ? "text bytes/chars are properties, not functions"
                                               : "text type has no such method",
            DiagnosticCode::TypeMethodCall
        );
    }
    if (argument_count != text_intrinsic_contract(intrinsic).parameters.size() - 1) {
        return operation_error(
            "text method argument count does not match",
            DiagnosticCode::TypeMethodCallArity
        );
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
        name == "byte_length" || name == "is_empty" ? "text byte_length/is_empty must be called"
                                                    : "text type has no such property",
        DiagnosticCode::TypeTextProperty
    );
}

auto sequence_extent(const ProgramDraft& draft, const SemanticExpression& source) noexcept
    -> std::optional<std::uint64_t> {
    if (const auto shape = sequence_shape(draft, source.type.construction());
        shape && shape->kind == SequenceShapeKind::Array) {
        return shape->extent;
    }
    if (source.constant) {
        const auto* slice = std::get_if<SliceConstant>(&draft.constant(*source.constant).value);
        if (slice) {
            return slice->elements.size();
        }
    }
    if (const auto* intrinsic = std::get_if<SemIntrinsic>(&source.value)) {
        if (const auto* slice = std::get_if<SliceIntrinsicOperation>(&intrinsic->operation)) {
            return slice->result_extent;
        }
    }
    if (const auto* take = std::get_if<SemTake>(&source.value)) {
        return sequence_extent(draft, *take->place);
    }
    return std::nullopt;
}

auto constant_index_in_bounds(
    const ProgramDraft& draft,
    std::optional<std::uint64_t> extent,
    std::optional<ConstantID> index
) noexcept -> std::optional<bool> {
    if (!extent || !index) {
        return std::nullopt;
    }
    const auto* integer = std::get_if<IntegerConstant>(&draft.constant(*index).value);
    if (!integer) {
        return std::nullopt;
    }
    return !integer->negative() && integer->magnitude() < *extent;
}

auto sequence_shape(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<SequenceShape> {
    if (const auto* generic = std::get_if<GenericTypeID>(&type)) {
        const auto expression = draft.generic_type_copy(*generic);
        if (const auto* concrete = std::get_if<TypeID>(&expression)) {
            return sequence_shape(draft, ConstructionTypeRef(*concrete));
        }
        if (const auto* sequence = std::get_if<GenericVectorType>(&expression)) {
            return SequenceShape {
                .kind = SequenceShapeKind::Vector,
                .element = sequence->element,
                .extent = std::nullopt,
            };
        }
    }
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        const auto canonical = draft.type_copy(*concrete);
        if (const auto* sequence = std::get_if<VectorTypeValue>(&canonical.value)) {
            return SequenceShape {
                .kind = SequenceShapeKind::Vector,
                .element = sequence->element,
                .extent = std::nullopt,
            };
        }
    }
    if (const auto array = array_shape(draft, type)) {
        return SequenceShape {
            .kind = SequenceShapeKind::Array,
            .element = array->element,
            .extent = array->extent
        };
    }
    if (const auto element = slice_element(draft, type)) {
        return SequenceShape {
            .kind = SequenceShapeKind::Slice,
            .element = *element,
            .extent = std::nullopt
        };
    }
    return std::nullopt;
}

auto decide_float_method(
    const ProgramDraft& draft,
    ConstructionTypeRef operand,
    std::string_view name,
    std::size_t arguments
) noexcept -> std::expected<std::optional<FloatIntrinsic>, OperationDiagnostic> {
    const auto* type = std::get_if<TypeID>(&operand);
    if (type == nullptr || name != "is_finite") {
        return std::nullopt;
    }
    const auto canonical = draft.type_copy(*type);
    const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value);
    if (builtin == nullptr
        || (builtin->kind != BuiltinType::F32 && builtin->kind != BuiltinType::F64)) {
        return std::nullopt;
    }
    if (arguments != 0) {
        return operation_error(
            "is_finite requires no arguments",
            DiagnosticCode::TypeMethodCallArity
        );
    }
    return FloatIntrinsic::IsFinite;
}
