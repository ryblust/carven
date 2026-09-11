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
                            program.types(),
                            value.operation,
                            value.operand->type.resolved()
                        ),
                        value.operand->type.resolved()
                    );
                },
                [&](const SemBinary& value) noexcept {
                    check_result(
                        decide_binary_operator(
                            program.types(),
                            value.operation,
                            value.left->type.resolved(),
                            value.right->type.resolved(),
                            true,
                            type_supports_equality(
                                program.types(),
                                program.declarations(),
                                value.left->type.resolved()
                            )
                        ),
                        value.left->type.resolved()
                    );
                },
                [&](const SemCast& value) noexcept {
                    const auto& type = require_type(value.operand->type.resolved());
                    const auto* enumeration = std::get_if<EnumTypeValue>(&type.value);
                    const auto numeric_enum =
                        enumeration != nullptr
                        && std::holds_alternative<NumericEnumRepresentation>(
                            require_enumeration(enumeration->enumeration).representation
                        );
                    const auto decision = decide_cast(
                        program.types(),
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
                if (program.constants().constant(value.constant).type != source.type.resolved()) {
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
                    static_cast<void>(
                        program.declarations().module_decl(name->name.context_module)
                    );
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
                    const auto* actual =
                        std::get_if<CppTypeValue>(&require_type(source.type.resolved()).value);
                    if (!valid_cpp_type(expected) || actual == nullptr || *actual != expected) {
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
                const auto* actual =
                    std::get_if<CppTypeValue>(&require_type(source.type.resolved()).value);
                if (!valid_cpp_type(expected) || actual == nullptr || *actual != expected) {
                    invariant_violation("C++ call differs from its result query");
                }
                if (const auto* name = cpp_type_name(expected)) {
                    static_cast<void>(program.declarations().module_decl(name->context_module));
                }
            },
            [&](const SemCall& value) noexcept {
                const auto& signature = program.callable_signatures().signature(
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
                const auto& type = require_type(source.type.resolved());
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
                const auto& type = require_type(source.type.resolved());
                const auto* structure = std::get_if<StructTypeValue>(&type.value);
                const auto& fields = require_structure(value.structure).fields;
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
                const auto& type = require_type(source.type.resolved());
                const auto* enumeration = std::get_if<EnumTypeValue>(&type.value);
                const auto& member = require_enum_case(value.enum_case);
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
            [&](const SemFormat& value) noexcept {
                const auto& specification = program.constants().constant(value.format_string_id);
                if (require_type(specification.type).value
                        != CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Str}}
                    || !std::holds_alternative<StringConstant>(specification.value)
                    || require_type(source.type.resolved()).value
                        != CanonicalTypeValue {BuiltinTypeValue {BuiltinType::String}}
                    || source.category != SemanticValueCategory::Value
                    || source.constant) {
                    invariant_violation(
                        "format requires a str constant and an owning String result"
                    );
                }
                for (const auto& operand : value.operands) {
                    if (operand.access != AccessMode::Read) {
                        invariant_violation("format operands require Read access");
                    }
                }
            },
            [&](const SemSliceIntrinsic& value) noexcept {
                if (value.operands.size()
                    != (value.intrinsic == SliceIntrinsic::Slice ? 3uz : 1uz)) {
                    invariant_violation("slice intrinsic operand count mismatch");
                }
                for (const auto& operand : value.operands) {
                    if (operand.access != AccessMode::Read) {
                        invariant_violation("slice intrinsic requires Read operands");
                    }
                }
                const auto& receiver =
                    require_type(value.operands.front().expression.type.resolved()).value;
                const auto* array = std::get_if<ArrayTypeValue>(&receiver);
                const auto* slice = std::get_if<SliceTypeValue>(&receiver);
                if ((value.intrinsic == SliceIntrinsic::FromArray && array == nullptr)
                    || (value.intrinsic != SliceIntrinsic::FromArray && slice == nullptr)) {
                    invariant_violation("slice intrinsic receiver mismatch");
                }
                const auto result = require_type(source.type.resolved()).value;
                if (value.intrinsic == SliceIntrinsic::Len
                    || value.intrinsic == SliceIntrinsic::IsEmpty) {
                    if (result
                        != CanonicalTypeValue {BuiltinTypeValue {
                            value.intrinsic == SliceIntrinsic::Len ? BuiltinType::Usize
                                                                   : BuiltinType::Bool
                        }}) {
                        invariant_violation("slice query result mismatch");
                    }
                } else if (result
                           != CanonicalTypeValue {
                               SliceTypeValue {.element = array ? array->element : slice->element}
                           }) {
                    invariant_violation("slice result element mismatch");
                }
                for (auto i = 1uz; i < value.operands.size(); ++i) {
                    if (require_type(value.operands[i].expression.type.resolved()).value
                        != CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Usize}}) {
                        invariant_violation("slice bound type mismatch");
                    }
                }
            },
            [&](const SemTextIntrinsic& value) noexcept {
                if (value.operands.size() != text_intrinsic_arity(value.intrinsic)) {
                    invariant_violation("text intrinsic operand count mismatch");
                }
                for (const auto& [index, operand] : std::views::enumerate(value.operands)) {
                    const auto access = index == 0 && text_intrinsic_writes(value.intrinsic)
                        ? AccessMode::Write
                        : AccessMode::Read;
                    if (operand.access != access
                        || (access == AccessMode::Write
                            && operand.expression.category != SemanticValueCategory::Place)) {
                        invariant_violation("text intrinsic operand access mismatch");
                    }
                    const auto& type = require_type(operand.expression.type.resolved()).value;
                    const auto* builtin = std::get_if<BuiltinTypeValue>(&type);
                    const auto query = value.intrinsic == TextIntrinsic::Len
                        || value.intrinsic == TextIntrinsic::IsEmpty
                        || value.intrinsic == TextIntrinsic::Bytes
                        || value.intrinsic == TextIntrinsic::Chars;
                    const auto expected = value.intrinsic == TextIntrinsic::FromStr || index == 1
                        ? (value.intrinsic == TextIntrinsic::Push ? BuiltinType::Char
                                                                  : BuiltinType::Str)
                        : BuiltinType::String;
                    if (builtin == nullptr
                        || (builtin->kind != expected
                            && !(query && builtin->kind == BuiltinType::Str))) {
                        invariant_violation("text intrinsic operand type mismatch");
                    }
                }
                const auto& result = require_type(source.type.resolved()).value;
                const auto* slice = std::get_if<SliceTypeValue>(&result);
                const auto valid_result = value.intrinsic == TextIntrinsic::Bytes ? slice != nullptr
                        && require_type(slice->element).value
                            == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::U8}}
                                                                                  : result
                        == CanonicalTypeValue {
                            BuiltinTypeValue {*text_intrinsic_builtin_result(value.intrinsic)}
                        };
                if (!valid_result) {
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
            [&](const SemDereference& value) noexcept {
                require_origin(value.origin);
                const auto& source_type = require_type(value.source->type.resolved());
                const auto* pointer = std::get_if<PointerTypeValue>(&source_type.value);
                if (pointer == nullptr
                    || pointer->target != source.type.resolved()
                    || require_type(source.type.resolved()).value
                        == CanonicalTypeValue {BuiltinTypeValue {BuiltinType::Void}}) {
                    invariant_violation("dereference has an invalid target type");
                }
            },
            [&](const SemField& value) noexcept {
                const auto& type = require_type(value.source->type.resolved());
                const auto* structure = std::get_if<StructTypeValue>(&type.value);
                const auto& fields = require_structure(value.field.owner).fields;
                if (structure == nullptr
                    || structure->structure != value.field.owner
                    || value.field.field_index >= fields.size()
                    || fields[value.field.field_index].type != source.type.resolved()) {
                    invariant_violation("field projection type mismatch");
                }
            },
            [&](const SemIndex& value) noexcept {
                const auto& type = require_type(value.source->type.resolved());
                const auto* array = std::get_if<ArrayTypeValue>(&type.value);
                const auto& index = require_type(value.index->type.resolved());
                const auto* integer = std::get_if<BuiltinTypeValue>(&index.value);
                const auto* slice = std::get_if<SliceTypeValue>(&type.value);
                if ((array == nullptr && slice == nullptr)
                    || (array != nullptr ? array->element : slice->element)
                        != source.type.resolved()
                    || integer == nullptr
                    || !builtin_is_integer(integer->kind)) {
                    invariant_violation("invalid array projection");
                }
            },
            [&](const SemClosure& value) noexcept {
                const auto& closure =
                    related_body(*program.declarations().body_for_callable(value.callable));
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
