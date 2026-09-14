module carven:semantic.analysis.expr.aggregate;

import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.expr.result;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.structured;
import :support.invariant;
import std;

template<typename Site>
auto construct_array_expression(
    Site& site,
    const ASTArrayExpr& source,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> ExpressionResult<typename Site::Value> {
    const auto context = array_literal_element(site.draft(), expected, source.element_ids.size());
    if (!context) {
        return std::unexpected(
            site.fail(span, context.error().code, std::string(context.error().message))
        );
    }
    if (auto checked = site.aggregate_cost(source.element_ids.size(), span); !checked) {
        return std::unexpected(checked.error());
    }
    const auto expected_element = *context;
    auto element_type = expected_element;
    auto state = typename Site::OperandState();
    auto elements = std::vector<SemanticExpression>();
    elements.reserve(source.element_ids.size());
    for (const auto id : source.element_ids) {
        const auto execution = site.enter_operand_execution(state.completes);
        const auto element_span = site.syntax().expression(id).span;
        auto element = site.read_array_element(id, element_type, expected_element.has_value());
        if (!element) {
            return std::unexpected(element.error());
        }
        if (!element_type) {
            auto inferred = site.infer_type(*element, element_span);
            if (!inferred) {
                return std::unexpected(inferred.error());
            }
            element_type = *inferred;
        } else {
            if ((!expected_element || !site.permits_pointer_narrowing())
                && pointer_narrows(site.draft(), site.type(*element), *element_type)) {
                return std::unexpected(site.fail(
                    element_span,
                    DiagnosticCode::TypeMismatch,
                    "mixed ptr target permissions require an explicit array element type"
                ));
            }
            if (auto checked = site.convert_argument(*element, *element_type, element_span);
                !checked) {
                return std::unexpected(checked.error());
            }
        }
        auto operand = site.consume_read(state, std::move(*element), element_span);
        if (!operand) {
            return std::unexpected(operand.error());
        }
        elements.push_back(std::move(*operand));
    }
    if (!element_type) {
        invariant_violation("empty expected array lost its element type");
    }
    const auto type = [&]() noexcept -> ConstructionTypeRef {
        if (expected_element && !slice_element(site.draft(), *expected)) {
            return *expected;
        }
        if (const auto* concrete = std::get_if<TypeID>(&*element_type)) {
            return site.draft().intern_type(
                {.value = ArrayTypeValue {.element = *concrete, .extent = elements.size()}}
            );
        }
        return site.draft().append_construction_type(
            {.value =
                 ConstructionArrayTypeValue {.element = *element_type, .extent = elements.size()}}
        );
    }();
    const auto admitted = site.aggregate_admitted(type, span);
    if (!admitted) {
        return std::unexpected(admitted.error());
    }
    if (!*admitted) {
        return std::unexpected(ExpressionNotAdmitted {});
    }
    return site.finish_constructed(
        type,
        SemArray {.elements = std::move(elements)},
        std::move(state),
        span
    );
}

template<typename Site>
auto construct_structure_expression(
    Site& site,
    const ASTConstructionExpr& source,
    Span span
) noexcept -> ExpressionResult<typename Site::Value> {
    auto resolved = site.resolve_construction_type(source.type);
    if (!resolved) {
        return std::unexpected(resolved.error());
    }
    if (site.external(*resolved)) {
        return site.cpp_construct(source, *resolved, span);
    }
    const auto* concrete = std::get_if<TypeID>(&*resolved);
    const auto canonical =
        concrete ? std::optional(site.draft().type_copy(*concrete)) : std::nullopt;
    const auto* structure = canonical ? std::get_if<StructTypeValue>(&canonical->value) : nullptr;
    if (structure == nullptr) {
        return std::unexpected(site.fail(
            source.type.span,
            DiagnosticCode::TypeConstructNotStruct,
            "construction expression requires a structure type"
        ));
    }
    const auto declaration =
        site.draft().construction_struct_declaration_copy(structure->structure);
    const auto initializers =
        select_structure_initializers(site.draft(), site.module_id(), source, declaration.fields);
    if (!initializers) {
        return std::unexpected(initializers.error());
    }
    if (auto checked = site.aggregate_cost(initializers->size(), span); !checked) {
        return std::unexpected(checked.error());
    }
    auto state = typename Site::OperandState();
    auto fields = std::vector<SemFieldInitializer>();
    for (const auto& initializer : *initializers) {
        const auto execution = site.enter_operand_execution(state.completes);
        const auto field_span = site.syntax().expression(initializer.expression).span;
        auto value = site.read_argument(
            initializer.expression,
            declaration.fields[initializer.declaration_index].type
        );
        if (!value) {
            return std::unexpected(value.error());
        }
        auto operand = site.consume_read(state, std::move(*value), field_span);
        if (!operand) {
            return std::unexpected(operand.error());
        }
        fields.push_back(
            {.declaration_index = initializer.declaration_index, .value = std::move(*operand)}
        );
    }
    const auto admitted = site.aggregate_admitted(*concrete, span);
    if (!admitted) {
        return std::unexpected(admitted.error());
    }
    if (!*admitted) {
        return std::unexpected(ExpressionNotAdmitted {});
    }
    return site.finish_constructed(
        *concrete,
        SemStruct {.structure = structure->structure, .fields = std::move(fields)},
        std::move(state),
        span
    );
}

template<typename Site>
auto construct_enum_value(
    Site& site,
    TypeID type,
    EnumCaseID selected,
    std::vector<typename Site::Value> arguments,
    std::optional<ConstantID> known,
    Span span
) noexcept -> ExpressionResult<typename Site::Value> {
    auto state = typename Site::OperandState();
    auto payload = std::vector<SemanticExpression>();
    for (auto& argument : arguments) {
        auto value = site.consume_read(state, std::move(argument), span);
        if (!value) {
            return std::unexpected(value.error());
        }
        payload.push_back(std::move(*value));
    }
    return site.finish_constructed(
        type,
        SemEnumCase {.enum_case = selected, .payload = std::move(payload)},
        std::move(state),
        span,
        known
    );
}

template<typename Site>
auto construct_slice_call(
    Site& site,
    SliceIntrinsic intrinsic,
    typename Site::Value receiver,
    std::span<const ASTCallArgument> arguments,
    Span span
) noexcept -> ExpressionResult<typename Site::Value> {
    const auto receiver_type = site.type(receiver);
    const auto extent = site.known_sequence_extent(receiver);
    const auto contract = slice_intrinsic_contract(intrinsic);
    if (arguments.size() != contract.arguments.size()) {
        invariant_violation("slice construction argument count mismatch");
    }
    const auto shape = sequence_shape(site.draft(), receiver_type);
    if (!shape || shape->extent.has_value() != (contract.receiver == SliceIntrinsicShape::Array)) {
        invariant_violation("slice construction receiver mismatch");
    }
    auto result_type = receiver_type;
    if (const auto* builtin = std::get_if<BuiltinType>(&contract.result)) {
        result_type = site.draft().intern_builtin_type(*builtin);
    } else if (contract.receiver == SliceIntrinsicShape::Array) {
        if (const auto* concrete = std::get_if<TypeID>(&shape->element)) {
            result_type =
                site.draft().intern_type({.value = SliceTypeValue {.element = *concrete}});
        } else {
            result_type = site.draft().append_construction_type(
                {.value = ConstructionSliceTypeValue {.element = shape->element}}
            );
        }
    }
    auto state = typename Site::OperandState();
    auto value = site.consume_read(state, std::move(receiver), span);
    if (!value) {
        return std::unexpected(value.error());
    }
    auto operands = std::vector<SemCallArgument>();
    operands.push_back({.access = AccessMode::Read, .expression = std::move(*value)});
    for (auto index = 0uz; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        const auto execution = site.enter_operand_execution(state.completes);
        auto built = site.read_argument(
            argument.expression,
            site.draft().intern_builtin_type(contract.arguments[index])
        );
        if (!built) {
            return std::unexpected(built.error());
        }
        auto operand = site.consume_read(
            state,
            std::move(*built),
            site.syntax().expression(argument.expression).span
        );
        if (!operand) {
            return std::unexpected(operand.error());
        }
        operands.push_back({.access = AccessMode::Read, .expression = std::move(*operand)});
    }
    auto result_extent = std::optional<std::uint64_t>();
    auto known = std::optional<ConstantID>();
    if (intrinsic == SliceIntrinsic::FromArray) {
        result_extent = extent;
    } else if constexpr (Site::mode == ExpressionMode::Body) {
        if (intrinsic == SliceIntrinsic::Slice) {
            const auto bound = [&](std::size_t index) noexcept -> std::optional<std::uint64_t> {
                const auto constant = operands[index].expression.constant;
                if (!constant) {
                    return std::nullopt;
                }
                const auto& fact = site.draft().constant(*constant);
                const auto* integer = std::get_if<IntegerConstant>(&fact.value);
                return integer ? integer->as_unsigned() : std::nullopt;
            };
            const auto start = bound(1uz);
            const auto end = bound(2uz);
            if (start && end && *start <= *end) {
                result_extent = *end - *start;
            }
        } else if (extent) {
            switch (intrinsic) {
                case SliceIntrinsic::Len:
                    known = site.draft().intern_constant(
                        {.type = std::get<TypeID>(result_type),
                         .value = IntegerConstant::from_parts(*extent, false)}
                    );
                    break;
                case SliceIntrinsic::IsEmpty:
                    known = site.draft().intern_constant(
                        {.type = std::get<TypeID>(result_type),
                         .value = BooleanConstant {.value = *extent == 0u}}
                    );
                    break;
                case SliceIntrinsic::FromArray:
                case SliceIntrinsic::Slice:     std::unreachable();
            }
        }
    }
    return site.finish_constructed(
        result_type,
        SemSliceIntrinsic {
            .intrinsic = intrinsic,
            .operands = std::move(operands),
            .result_extent = result_extent
        },
        std::move(state),
        span,
        known
    );
}
