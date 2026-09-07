module carven:semantic.analysis.validation.operations.impl;
import :semantic.analysis.validation.context;
import std;

auto BodyContractVerifier::verify_computations() const noexcept -> void {
    visit_semantic_nodes(body.region(), [&](const SemanticExpression& source) noexcept {
        const auto check_result = [&](const OperatorDecision& decision, TypeID operand) noexcept {
            if (!decision.has_value()) {
                invariant_violation("invalid semantic operator");
            }
            const auto valid = *decision == OperatorResult::Operand
                ? source.type.resolved() == operand
                : require_type(source.type.resolved()).value
                    == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Bool}};
            if (!valid) {
                invariant_violation("semantic operator has an incompatible result");
            }
        };
        std::visit(
            Overloaded {
                [&](const SemUnary& value) noexcept {
                    check_result(
                        decide_unary_operator(
                            draft->types(),
                            value.operation,
                            value.operand->type.resolved()
                        ),
                        value.operand->type.resolved()
                    );
                },
                [&](const SemBinary& value) noexcept {
                    check_result(
                        decide_binary_operator(
                            draft->types(),
                            value.operation,
                            value.left->type.resolved(),
                            value.right->type.resolved(),
                            true,
                            type_supports_equality(
                                draft->types(),
                                draft->declarations(),
                                value.left->type.resolved()
                            )
                        ),
                        value.left->type.resolved()
                    );
                },
                [&](const SemCast& value) noexcept {
                    const auto type = require_type(value.operand->type.resolved());
                    const auto* enumeration = std::get_if<EnumTypeValue>(&type.value);
                    const auto numeric_enum =
                        enumeration != nullptr
                        && std::holds_alternative<NumericEnumRepresentation>(
                            require_enumeration(enumeration->enumeration).representation
                        );
                    const auto decision = decide_cast(
                        draft->types(),
                        value.operand->type.resolved(),
                        source.type.resolved(),
                        numeric_enum
                    );
                    if (!decision.has_value() || *decision != value.kind) {
                        invariant_violation("semantic cast has an incompatible kind");
                    }
                },
                [](const auto&) static noexcept {},
            },
            source.value
        );
    });
}

auto BodyContractVerifier::verify_expression(const SemanticExpression& source) const noexcept
    -> void {
    static_cast<void>(require_type(source.type.resolved()));
    static_cast<void>(require_failure_set(source.failures.resolved()));
    require_origin(source.origin);
    if (!body.lifetime_regions().contains(source.lifetime)) {
        invariant_violation("semantic expression has foreign lifetime");
    }
    std::visit(
        Overloaded {
            [&](const SemConstant& value) noexcept {
                if (draft->constants().constant(value.constant).type != source.type.resolved()) {
                    invariant_violation("constant expression type mismatch");
                }
            },
            [&](const SemBinding& value) noexcept {
                if (body.binding(value.binding).type != source.type.resolved()) {
                    invariant_violation("binding expression type mismatch");
                }
            },
            [&](const SemCpp& value) noexcept {
                if (!cpp_operation_accepts_arity(value.operation, value.operands.size())) {
                    invariant_violation("invalid C++ operation operands");
                }
                if (const auto* name = std::get_if<CppNameOperation>(&value.operation)) {
                    static_cast<void>(draft->declarations().module_decl(name->name.context_module));
                }
                if (std::holds_alternative<CppCStringOperation>(value.operation)
                    && require_type(source.type.resolved()).value
                        != CanonicalTypeValue {CppTypeValue {.form = CppConstCharPointerType {}}}) {
                    invariant_violation("C string literal does not have const char pointer type");
                }
                if (!std::holds_alternative<CppConstructOperation>(value.operation)
                    && !std::holds_alternative<CppCStringOperation>(value.operation)
                    && !std::holds_alternative<CppConvertOperation>(value.operation)
                    && !std::holds_alternative<CppUpdateOperation>(value.operation)) {
                    auto operands = std::vector<CppTypeOperand>();
                    for (const auto& operand : value.operands) {
                        operands.push_back(
                            {.type = operand.expression.type.resolved(), .access = operand.access}
                        );
                    }
                    const auto expected =
                        CppTypeValue {.form = cpp_query_type(value.operation, operands)};
                    if (!valid_cpp_type(expected)
                        || require_type(source.type.resolved()).value
                            != CanonicalTypeValue {expected}) {
                        invariant_violation("C++ operation differs from its result query");
                    }
                }
                if (source.category == SemanticValueCategory::Place
                    && !(
                        std::holds_alternative<CppMemberOperation>(value.operation)
                        || std::holds_alternative<CppIndexOperation>(value.operation)
                        || std::holds_alternative<CppConvertOperation>(value.operation)
                    )) {
                    invariant_violation("C++ operation cannot denote storage");
                }
            },
            [&](const SemCppCall& value) noexcept {
                if (source.category != SemanticValueCategory::Value) {
                    invariant_violation("C++ call cannot denote storage");
                }
                const auto expected = CppTypeValue {
                    .form = cpp_call_query(value, [](TypeID type) static noexcept { return type; })
                };
                if (!valid_cpp_type(expected)
                    || require_type(source.type.resolved()).value
                        != CanonicalTypeValue {expected}) {
                    invariant_violation("C++ call differs from its result query");
                }
                for (const auto& name : cpp_type_names(expected)) {
                    static_cast<void>(draft->declarations().module_decl(name.context_module));
                }
            },
            [&](const SemCall& value) noexcept {
                const auto signature = draft->callable_signatures().signature(
                    signature_for_type(value.callee->type.resolved())
                );
                if (signature.result != source.type.resolved()
                    || signature.parameters.size() != value.arguments.size()
                    || signature.failures != value.callee_failures.resolved()) {
                    invariant_violation("call differs from signature");
                }
                for (const auto& [argument, parameter] :
                     std::views::zip(value.arguments, signature.parameters)) {
                    if (argument.expression.type.resolved() != parameter.type
                        || argument.access != parameter.access) {
                        invariant_violation("call argument differs from parameter");
                    }
                }
            },
            [&](const SemArray& value) noexcept {
                const auto type = require_type(source.type.resolved());
                const auto* array = std::get_if<ArrayTypeValue>(&type.value);
                if (array == nullptr || array->extent != value.elements.size()) {
                    invariant_violation("array initialization has wrong shape");
                }
                for (const auto& element : value.elements) {
                    if (element.type.resolved() != array->element) {
                        invariant_violation("array element type mismatch");
                    }
                }
            },
            [&](const SemStruct& value) noexcept {
                const auto type = require_type(source.type.resolved());
                const auto* structure = std::get_if<StructTypeValue>(&type.value);
                const auto fields = require_structure(value.structure).fields;
                if (structure == nullptr
                    || structure->structure != value.structure
                    || fields.size() != value.fields.size()) {
                    invariant_violation("structure initialization has wrong shape");
                }
                auto seen = std::vector<bool>(fields.size());
                for (const auto& field : value.fields) {
                    if (field.declaration_index >= fields.size()
                        || seen[field.declaration_index]
                        || fields[field.declaration_index].type != field.value.type.resolved()) {
                        invariant_violation("structure field initialization mismatch");
                    }
                    seen[field.declaration_index] = true;
                }
            },
            [&](const SemEnumCase& value) noexcept {
                const auto type = require_type(source.type.resolved());
                const auto* enumeration = std::get_if<EnumTypeValue>(&type.value);
                const auto member = require_enum_case(value.enum_case);
                if (enumeration == nullptr
                    || enumeration->enumeration != member.owner
                    || value.payload.size() != member.payload_types.size()) {
                    invariant_violation("enum initialization has wrong shape");
                }
                for (const auto& [element, type] :
                     std::views::zip(value.payload, member.payload_types)) {
                    if (element.type.resolved() != type) {
                        invariant_violation("enum payload type mismatch");
                    }
                }
            },
            [&](const SemShortCircuit& value) noexcept {
                const auto boolean = CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Bool}};
                if (require_type(source.type.resolved()).value != boolean
                    || value.left->type.resolved() != source.type.resolved()
                    || value.right->type.resolved() != source.type.resolved()) {
                    invariant_violation("short circuit requires boolean values");
                }
            },
            [&](const SemTextIntrinsic& value) noexcept {
                if (require_type(value.source->type.resolved()).value
                    != CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Str}}) {
                    invariant_violation("text intrinsic requires str");
                }
                const auto expected = [&]() noexcept {
                    switch (value.intrinsic) {
                        case TextIntrinsic::Len:     return BuiltinType::Usize;
                        case TextIntrinsic::IsEmpty: return BuiltinType::Bool;
                        case TextIntrinsic::Bytes:   return BuiltinType::StrBytesView;
                        case TextIntrinsic::Chars:   return BuiltinType::StrCharsView;
                    }
                    std::unreachable();
                }();
                if (require_type(source.type.resolved()).value
                    != CanonicalTypeValue {BuiltinTypeValue {expected}}) {
                    invariant_violation("text intrinsic result mismatch");
                }
            },
            [&](const SemTake& value) noexcept {
                if (source.type.resolved() != value.place->type.resolved()
                    || value.place->category != SemanticValueCategory::Place) {
                    invariant_violation("Take requires a matching place");
                }
            },
            [&](const SemIf& value) noexcept {
                const auto check = [&](const SemanticRegion& region) noexcept {
                    if (region.result.has_value()
                        && region.result->type.resolved() != source.type.resolved()) {
                        invariant_violation("conditional result type mismatch");
                    }
                };
                for (const auto& branch : value.branches) {
                    if (require_type(branch.condition.type.resolved()).value
                        != CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Bool}}) {
                        invariant_violation("conditional requires bool");
                    }
                    check(branch.body);
                }
                if (value.otherwise.has_value()) {
                    check(**value.otherwise);
                }
            },
            [&](const SemMatch& value) noexcept {
                for (const auto& arm : value.arms) {
                    if (body.pattern(arm.pattern).type != value.subject->type.resolved()) {
                        invariant_violation("match pattern type mismatch");
                    }
                    auto expected = pattern_bindings(arm.pattern);
                    auto actual = arm.bindings;
                    std::ranges::sort(expected, {}, &LocalBindingID::index);
                    std::ranges::sort(actual, {}, &LocalBindingID::index);
                    if (expected != actual) {
                        invariant_violation("match binding mismatch");
                    }
                    if (arm.body.result.has_value()
                        && arm.body.result->type.resolved() != source.type.resolved()) {
                        invariant_violation("match result type mismatch");
                    }
                }
            },
            [&](const SemField& value) noexcept {
                const auto type = require_type(value.source->type.resolved());
                const auto* structure = std::get_if<StructTypeValue>(&type.value);
                const auto fields = require_structure(value.field.owner).fields;
                if (structure == nullptr
                    || structure->structure != value.field.owner
                    || value.field.field_index >= fields.size()
                    || fields[value.field.field_index].type != source.type.resolved()) {
                    invariant_violation("field projection type mismatch");
                }
            },
            [&](const SemIndex& value) noexcept {
                const auto type = require_type(value.source->type.resolved());
                const auto* array = std::get_if<ArrayTypeValue>(&type.value);
                const auto index = require_type(value.index->type.resolved());
                const auto* integer = std::get_if<BuiltinTypeValue>(&index.value);
                if (array == nullptr
                    || array->element != source.type.resolved()
                    || integer == nullptr
                    || !builtin_is_integer(integer->kind)) {
                    invariant_violation("invalid array projection");
                }
            },
            [&](const SemClosure& value) noexcept {
                const auto& closure = related_body(*draft->body_for_callable(value.callable));
                if (closure.inputs().captures.size() != value.captures.size()) {
                    invariant_violation("closure capture arity mismatch");
                }
                for (const auto& [id, capture] :
                     std::views::zip(closure.inputs().captures, value.captures)) {
                    const auto& binding = closure.binding(id);
                    if (binding.type != capture.expression.type.resolved()
                        || std::get<CaptureBindingStorage>(binding.storage).mode != capture.mode) {
                        invariant_violation("closure capture contract mismatch");
                    }
                }
            },
            [](const auto&) static noexcept {},
        },
        source.value
    );
}
