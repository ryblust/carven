module carven:semantic.analysis.validation.operations.impl;
import :semantic.analysis.validation.context;
import std;

namespace validation_detail {
auto BodyContractVerifier::verify_computations() const noexcept -> void {
    visit_semantic_nodes(body.region(), [&](const SemIRExpression& source) noexcept {
        const auto check_result = [&](const OperatorDecision& decision, TypeID operand) noexcept {
            if (!decision.has_value()) {
                invariant_violation("invalid semantic operator");
            }
            const auto valid = *decision == OperatorResult::Operand
                ? source.type == operand
                : require_type(source.type).value
                    == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Bool}};
            if (!valid) {
                invariant_violation("semantic operator has an incompatible result");
            }
        };
        std::visit(
            Overloaded {
                [&](const SemUnary<TypeID, FailureSetID>& value) noexcept {
                    check_result(
                        decide_unary_operator(
                            *draft,
                            value.operation,
                            ConstructionTypeRef {value.operand->type}
                        ),
                        value.operand->type
                    );
                },
                [&](const SemBinary<TypeID, FailureSetID>& value) noexcept {
                    check_result(
                        decide_binary_operator(
                            *draft,
                            value.operation,
                            ConstructionTypeRef {value.left->type},
                            ConstructionTypeRef {value.right->type},
                            true,
                            type_supports_equality(*draft, ConstructionTypeRef {value.left->type})
                        ),
                        value.left->type
                    );
                },
                [&](const SemCast<TypeID, FailureSetID>& value) noexcept {
                    const auto type = require_type(value.operand->type);
                    const auto* enumeration = std::get_if<EnumTypeValue>(&type.value);
                    const auto numeric_enum =
                        enumeration != nullptr
                        && std::holds_alternative<ConstructionNumericEnumRepresentation>(
                            require_enumeration(enumeration->enumeration).representation
                        );
                    const auto decision = decide_cast(
                        *draft,
                        ConstructionTypeRef {value.operand->type},
                        ConstructionTypeRef {source.type},
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

auto BodyContractVerifier::verify_expression(const SemIRExpression& source) const noexcept -> void {
    static_cast<void>(require_type(source.type));
    static_cast<void>(require_failure_set(source.failures));
    require_origin(source.origin);
    if (!body.lifetime_regions().contains(source.lifetime)) {
        invariant_violation("semantic expression has foreign lifetime");
    }
    std::visit(
        Overloaded {
            [&](const SemLiteral& value) noexcept {
                const auto canonical = require_type(source.type);
                const auto valid = std::visit(
                    Overloaded {
                        [&](const IntegerLiteral& literal) noexcept {
                            const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value);
                            return builtin != nullptr
                                && builtin_is_integer(builtin->kind)
                                && integer_constant_fits(literal.value, builtin->kind);
                        },
                        [&](const F32Literal&) noexcept {
                            return canonical.value
                                == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::F32}};
                        },
                        [&](const F64Literal&) noexcept {
                            return canonical.value
                                == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::F64}};
                        },
                        [&](const BooleanLiteral&) noexcept {
                            return canonical.value
                                == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Bool}};
                        },
                        [&](const CharacterLiteral& literal) noexcept {
                            return canonical.value
                                    == CanonicalTypeValue {
                                        BuiltinTypeValue {BuiltinType::Char},
                                    }
                                && literal.scalar <= 0x10ffffu
                                && (literal.scalar < 0xd800u
                                    || literal.scalar > 0xdfffu);
                        },
                        [&](const StringLiteral& literal) noexcept {
                            return literal.bytes.owner() == body.provenance_identity()
                                && canonical.value
                                == CanonicalTypeValue {
                                    BuiltinTypeValue {BuiltinType::Str},
                                };
                        },
                    },
                    value.value
                );
                if (!valid) {
                    invariant_violation("literal has an incompatible result type");
                }
            },
            [&](const SemConstant& value) noexcept {
                if (draft->constant_copy(value.constant).type != source.type) {
                    invariant_violation("constant expression type mismatch");
                }
            },
            [&](const SemBinding& value) noexcept {
                if (body.binding(value.binding).type != source.type) {
                    invariant_violation("binding expression type mismatch");
                }
            },
            [&](const SemCpp<TypeID, FailureSetID>& value) noexcept {
                if (!cpp_operation_accepts_arity(value.operation, value.operands.size())) {
                    invariant_violation("invalid C++ operation operands");
                }
                if (const auto* name = std::get_if<CppNameOperation>(&value.operation)) {
                    static_cast<void>(draft->module_declaration_copy(name->module_id));
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
            [&](const SemCall<TypeID, FailureSetID>& value) noexcept {
                const auto signature =
                    draft->callable_signature_copy(signature_for_type(value.callee->type));
                if (signature.result != source.type
                    || signature.parameters.size() != value.arguments.size()
                    || signature.failures != value.callee_failures) {
                    invariant_violation("call differs from signature");
                }
                for (const auto& [argument, parameter] :
                     std::views::zip(value.arguments, signature.parameters)) {
                    if (argument.expression.type != parameter.type
                        || argument.access != parameter.access) {
                        invariant_violation("call argument differs from parameter");
                    }
                }
            },
            [&](const SemArray<TypeID, FailureSetID>& value) noexcept {
                const auto type = require_type(source.type);
                const auto* array = std::get_if<ArrayTypeValue>(&type.value);
                if (array == nullptr || array->extent != value.elements.size()) {
                    invariant_violation("array initialization has wrong shape");
                }
                for (const auto& element : value.elements) {
                    if (element.type != array->element) {
                        invariant_violation("array element type mismatch");
                    }
                }
            },
            [&](const SemStruct<TypeID, FailureSetID>& value) noexcept {
                const auto type = require_type(source.type);
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
                        || draft->concrete_type(fields[field.declaration_index].type)
                            != field.value.type) {
                        invariant_violation("structure field initialization mismatch");
                    }
                    seen[field.declaration_index] = true;
                }
            },
            [&](const SemEnumCase<TypeID, FailureSetID>& value) noexcept {
                const auto type = require_type(source.type);
                const auto* enumeration = std::get_if<EnumTypeValue>(&type.value);
                const auto member = require_enum_case(value.enum_case);
                if (enumeration == nullptr
                    || enumeration->enumeration != member.owner
                    || value.payload.size() != member.payload_types.size()) {
                    invariant_violation("enum initialization has wrong shape");
                }
                for (const auto& [element, type] :
                     std::views::zip(value.payload, member.payload_types)) {
                    if (element.type != draft->concrete_type(type)) {
                        invariant_violation("enum payload type mismatch");
                    }
                }
            },
            [&](const SemShortCircuit<TypeID, FailureSetID>& value) noexcept {
                const auto boolean = CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Bool}};
                if (require_type(source.type).value != boolean
                    || value.left->type != source.type
                    || value.right->type != source.type) {
                    invariant_violation("short circuit requires boolean values");
                }
            },
            [&](const SemTextIntrinsic<TypeID, FailureSetID>& value) noexcept {
                if (require_type(value.source->type).value
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
                if (require_type(source.type).value
                    != CanonicalTypeValue {BuiltinTypeValue {expected}}) {
                    invariant_violation("text intrinsic result mismatch");
                }
            },
            [&](const SemTake<TypeID, FailureSetID>& value) noexcept {
                if (source.type != value.place->type
                    || value.place->category != SemanticValueCategory::Place) {
                    invariant_violation("Take requires a matching place");
                }
            },
            [&](const SemIf<TypeID, FailureSetID>& value) noexcept {
                const auto check = [&](const SemIRRegion& region) noexcept {
                    if (region.result.has_value() && region.result->type != source.type) {
                        invariant_violation("conditional result type mismatch");
                    }
                };
                for (const auto& branch : value.branches) {
                    if (require_type(branch.condition.type).value
                        != CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Bool}}) {
                        invariant_violation("conditional requires bool");
                    }
                    check(branch.body);
                }
                if (value.otherwise.has_value()) {
                    check(**value.otherwise);
                }
            },
            [&](const SemMatch<TypeID, FailureSetID>& value) noexcept {
                for (const auto& arm : value.arms) {
                    if (body.pattern(arm.pattern).type != value.subject->type) {
                        invariant_violation("match pattern type mismatch");
                    }
                    auto expected = pattern_bindings(arm.pattern);
                    auto actual = arm.bindings;
                    std::ranges::sort(expected, {}, &LocalBindingID::index);
                    std::ranges::sort(actual, {}, &LocalBindingID::index);
                    if (expected != actual) {
                        invariant_violation("match binding mismatch");
                    }
                    if (arm.body.result.has_value() && arm.body.result->type != source.type) {
                        invariant_violation("match result type mismatch");
                    }
                }
            },
            [&](const SemField<TypeID, FailureSetID>& value) noexcept {
                const auto type = require_type(value.source->type);
                const auto* structure = std::get_if<StructTypeValue>(&type.value);
                const auto fields = require_structure(value.field.owner).fields;
                if (structure == nullptr
                    || structure->structure != value.field.owner
                    || value.field.field_index >= fields.size()
                    || draft->concrete_type(fields[value.field.field_index].type) != source.type) {
                    invariant_violation("field projection type mismatch");
                }
            },
            [&](const SemIndex<TypeID, FailureSetID>& value) noexcept {
                const auto type = require_type(value.source->type);
                const auto* array = std::get_if<ArrayTypeValue>(&type.value);
                const auto index = require_type(value.index->type);
                const auto* integer = std::get_if<BuiltinTypeValue>(&index.value);
                if (array == nullptr
                    || array->element != source.type
                    || integer == nullptr
                    || !builtin_is_integer(integer->kind)) {
                    invariant_violation("invalid array projection");
                }
            },
            [&](const SemClosure<TypeID, FailureSetID>& value) noexcept {
                const auto& closure = related_body(*draft->body_for_callable(value.callable));
                if (closure.inputs().captures.size() != value.captures.size()) {
                    invariant_violation("closure capture arity mismatch");
                }
                for (const auto& [id, capture] :
                     std::views::zip(closure.inputs().captures, value.captures)) {
                    const auto& binding = closure.binding(id);
                    if (binding.type != capture.expression.type
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
} // namespace validation_detail
