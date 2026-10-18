module carven:semantic.analysis.expr.aggregate;

import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.construction.requests;
import :semantic.analysis.diagnostics;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.initialization;
import :semantic.semir.slice;
import :semantic.semir.structured;
import :support.invariant;
import std;

template<typename Site>
auto construct_default_expression(Site& site, ConstructionTypeRef type, Span span) noexcept
    -> AnalysisResult<typename Site::Value> {
    if (default_initialization(site.draft(), type) == DefaultInitialization::Unavailable) {
        return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeDefaultInitialization,
            "type has no default value; provide an explicit initializer"
        ));
    }
    return site.finish_constructed(type, SemDefault {}, Site::operand_state(), span);
}

template<typename Site, typename Source>
auto construct_array_expression(
    Site& site,
    const Source& source,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<typename Site::Value> {
    constexpr auto repeated = std::same_as<Source, ASTArrayRepeatExpr>;
    std::uint64_t count = 0u;
    if constexpr (repeated) {
        auto resolved = (co_await site.resolve_array_extent(source.extent));
        if (!resolved) {
            co_return std::unexpected(resolved.error());
        }
        count = *resolved;
    } else {
        count = source.element_ids.size();
    }
    if (count > std::numeric_limits<std::size_t>::max()) {
        co_return std::unexpected(site.fail(
            span,
            DiagnosticCode::ConstLimit,
            "array initializer exceeds its construction budget"
        ));
    }
    const auto extent = static_cast<std::size_t>(count);
    const auto context = repeated && extent == 0uz && !expected
        ? std::expected<std::optional<ConstructionTypeRef>, OperationDiagnostic>(std::nullopt)
        : array_literal_element(site.draft(), expected, extent);
    if (!context) {
        co_return std::unexpected(
            site.fail(span, context.error().code, std::string(context.error().message))
        );
    }
    if (auto checked = site.aggregate_cost(extent, span); !checked) {
        co_return std::unexpected(checked.error());
    }
    const auto expected_element = *context;
    auto element_type = expected_element;
    auto state = Site::operand_state();
    auto elements = std::vector<SemanticExpression>();
    elements.reserve(extent);
    const auto source_count = repeated && extent == 0uz ? 1uz : extent;
    for (auto index = 0uz; index < source_count; ++index) {
        const auto id = [&]() noexcept {
            if constexpr (repeated) {
                return source.element;
            } else {
                return source.element_ids[index];
            }
        }();
        const auto executed = index < extent;
        const auto execution = site.enter_operand_execution(executed && state.completes);
        const auto element_span = site.syntax().expression(id).span;
        auto element =
            (co_await site.read_aggregate_element(id, element_type, expected_element.has_value()));
        if (!element) {
            co_return std::unexpected(element.error());
        }
        if (!element_type) {
            auto inferred = site.infer_type(*element, element_span);
            if (!inferred) {
                co_return std::unexpected(inferred.error());
            }
            element_type = *inferred;
        } else {
            if ((!expected_element || !site.permits_pointer_narrowing())
                && pointer_narrows(site.draft(), site.type(*element), *element_type)) {
                co_return std::unexpected(site.fail(
                    element_span,
                    DiagnosticCode::TypeMismatch,
                    "mixed ptr target permissions require an explicit array element type"
                ));
            }
            if (auto checked = site.convert_argument(*element, *element_type, element_span);
                !checked) {
                co_return std::unexpected(checked.error());
            }
        }
        auto operand = executed ? site.consume_read(state, std::move(*element), element_span)
                                : site.consume_unexecuted_read(std::move(*element), element_span);
        if (!operand) {
            co_return std::unexpected(operand.error());
        }
        if (executed) {
            elements.push_back(std::move(*operand));
        }
    }
    if (!element_type) {
        invariant_violation("empty expected array lost its element type");
    }
    const auto type = [&]() noexcept -> ConstructionTypeRef {
        if (expected_element && !slice_element(site.draft(), *expected)) {
            return *expected;
        }
        return make_array_type(site.draft(), *element_type, elements.size());
    }();
    co_return site.finish_constructed(
        type,
        SemArray {.elements = std::move(elements)},
        std::move(state),
        span
    );
}

template<typename Site>
auto construct_tuple_expression(
    Site& site,
    const ASTTupleExpr& source,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<typename Site::Value> {
    auto expected_elements = expected ? tuple_elements(site.draft(), *expected) : std::nullopt;
    if (expected_elements && expected_elements->size() != source.elements.size()) {
        co_return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeMismatch,
            "tuple initializer differs from its expected product shape"
        ));
    }
    if (auto checked = site.aggregate_cost(source.elements.size(), span); !checked) {
        co_return std::unexpected(checked.error());
    }
    auto state = Site::operand_state();
    auto elements = std::vector<SemanticExpression>();
    auto types = std::vector<ConstructionTypeRef>();
    for (const auto [index, element] : std::views::enumerate(source.elements)) {
        const auto id = element;
        const auto execution = site.enter_operand_execution(state.completes);
        const auto element_span = site.syntax().expression(id).span;
        const auto context =
            expected_elements ? std::optional((*expected_elements)[index]) : std::nullopt;
        auto value = co_await site.read_aggregate_element(id, context, context.has_value());
        if (!value) {
            co_return std::unexpected(value.error());
        }
        auto inferred = context ? AnalysisResult<ConstructionTypeRef>(*context)
                                : site.infer_type(*value, element_span);
        if (!inferred) {
            co_return std::unexpected(inferred.error());
        }
        if (context) {
            if (auto checked = site.convert_argument(*value, *context, element_span); !checked) {
                co_return std::unexpected(checked.error());
            }
        }
        types.push_back(*inferred);
        auto operand = site.consume_read(state, std::move(*value), element_span);
        if (!operand) {
            co_return std::unexpected(operand.error());
        }
        elements.push_back(std::move(*operand));
    }
    const auto type = make_tuple_type(site.draft(), std::move(types));
    co_return site.finish_constructed(
        type,
        SemTuple {.elements = std::move(elements)},
        std::move(state),
        span
    );
}

template<typename Site>
auto construct_structure_expression(
    Site& site,
    const ASTConstructionExpr& source,
    Span span,
    std::optional<ConstructionTypeRef> expected
) noexcept -> AnalysisTask<typename Site::Value> {
    if (!source.type && !expected) {
        co_return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeConstructContext,
            "construction requires an explicit type or a known expected type"
        ));
    }
    auto resolved = source.type ? (co_await site.resolve_construction_type(*source.type))
                                : AnalysisResult<ConstructionTypeRef>(*expected);
    if (!resolved) {
        co_return std::unexpected(resolved.error());
    }
    if (!source.type) {
        auto prepared =
            (co_await site.construction_requests().ensure_type(*resolved, site.module_id(), span));
        if (!prepared) {
            co_return std::unexpected(prepared.error());
        }
    }
    if (site.external(*resolved)) {
        if (!source.type) {
            co_return std::unexpected(site.fail(
                span,
                DiagnosticCode::TypeConstructContext,
                "native construction requires an explicit type"
            ));
        }
        co_return (co_await site.cpp_construct(source, *resolved, span));
    }
    const auto structure = record_shape(site.draft(), *resolved);
    if (structure) {
        if (const auto* concrete = std::get_if<StructID>(&structure->owner)) {
            if (auto access = site.representation_access(*concrete, span); !access) {
                co_return std::unexpected(access.error());
            }
        }
        if (structure->kind == RecordKind::Class
            && structure->fields.empty()
            && std::holds_alternative<std::monostate>(source.initializer.value)) {
            co_return site.finish_constructed(
                *resolved,
                SemStruct {.structure = structure->owner, .fields = {}},
                Site::operand_state(),
                span
            );
        }
    }
    if (std::holds_alternative<std::monostate>(source.initializer.value)) {
        co_return construct_default_expression(site, *resolved, span);
    }
    if (!structure) {
        co_return std::unexpected(site.fail(
            source.type ? source.type->span : span,
            DiagnosticCode::TypeConstructNotStruct,
            "construction expression requires a structure type"
        ));
    }
    const auto& declaration = *structure;
    const auto initializers = select_structure_initializers(
        site.draft(),
        site.module_id(),
        source.initializer,
        declaration.fields
    );
    if (!initializers) {
        co_return std::unexpected(initializers.error());
    }
    if (auto checked = site.aggregate_cost(initializers->size(), span); !checked) {
        co_return std::unexpected(checked.error());
    }
    auto state = Site::operand_state();
    auto fields = std::vector<SemFieldInitializer>();
    for (const auto& initializer : *initializers) {
        const auto execution = site.enter_operand_execution(state.completes);
        const auto& field = declaration.fields[initializer.declaration_index];
        if (initializer.name) {
            site.observe_field(field.origin, *initializer.name, field.type);
        }
        const auto field_span = site.syntax().expression(initializer.expression).span;
        auto value =
            (co_await site.read_aggregate_element(initializer.expression, field.type, true));
        if (!value) {
            co_return std::unexpected(value.error());
        }
        if (auto checked = site.convert_argument(*value, field.type, field_span); !checked) {
            co_return std::unexpected(checked.error());
        }
        auto operand = site.consume_read(state, std::move(*value), field_span);
        if (!operand) {
            co_return std::unexpected(operand.error());
        }
        fields.push_back(
            {.declaration_index = initializer.declaration_index, .value = std::move(*operand)}
        );
    }
    co_return site.finish_constructed(
        *resolved,
        SemStruct {.structure = structure->owner, .fields = std::move(fields)},
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
) noexcept -> AnalysisResult<typename Site::Value> {
    auto state = Site::operand_state();
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
auto construct_slice_value(
    Site& site,
    SliceIntrinsic intrinsic,
    ConstructionTypeRef receiver_type,
    std::optional<std::uint64_t> extent,
    std::vector<SemCallArgument> operands,
    typename Site::OperandState state,
    Span span,
    std::optional<ConstructionTypeRef> expected = std::nullopt
) noexcept -> AnalysisResult<typename Site::Value> {
    const auto contract = slice_intrinsic_contract(intrinsic);
    const auto shape = sequence_shape(site.draft(), receiver_type);
    if (!shape
        || !slice_intrinsic_accepts_receiver(
            contract.receiver,
            shape->kind == SequenceShapeKind::Array        ? SliceIntrinsicShape::Array
                : shape->kind == SequenceShapeKind::Vector ? SliceIntrinsicShape::Vector
                                                           : SliceIntrinsicShape::Slice
        )) {
        invariant_violation("slice construction receiver mismatch");
    }
    auto result_type = receiver_type;
    if (const auto* builtin = std::get_if<BuiltinType>(&contract.result)) {
        result_type = site.draft().builtin_type(*builtin);
    } else if (shape->kind != SequenceShapeKind::Slice) {
        result_type = make_slice_type(site.draft(), shape->element);
    }
    if (expected) {
        result_type = *expected;
    }
    auto result_extent = std::optional<std::uint64_t>();
    auto known = std::optional<ConstantID>();
    if (intrinsic == SliceIntrinsic::View) {
        result_extent = extent;
    } else {
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
                case SliceIntrinsic::Length:
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
                case SliceIntrinsic::View:
                case SliceIntrinsic::Slice: std::unreachable();
            }
        }
    }
    return site.finish_constructed(
        result_type,
        SemIntrinsic {
            .operation =
                SliceIntrinsicOperation {.intrinsic = intrinsic, .result_extent = result_extent},
            .operands = std::move(operands)
        },
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
    std::span<const ASTExprID> arguments,
    Span span
) noexcept -> AnalysisTask<typename Site::Value> {
    if (intrinsic == SliceIntrinsic::Slice && !slice_element(site.draft(), site.type(receiver))) {
        auto borrowed = co_await construct_slice_call(
            site,
            SliceIntrinsic::View,
            std::move(receiver),
            std::span<const ASTExprID>(),
            span
        );
        if (!borrowed) {
            co_return std::unexpected(borrowed.error());
        }
        receiver = std::move(*borrowed);
    }
    const auto receiver_type = site.type(receiver);
    const auto extent = site.known_sequence_extent(receiver);
    const auto contract = slice_intrinsic_contract(intrinsic);
    if (arguments.size() != contract.arguments.size()) {
        invariant_violation("slice construction argument count mismatch");
    }
    auto state = Site::operand_state();
    auto value = site.consume_read_argument(state, std::move(receiver), span);
    if (!value) {
        co_return std::unexpected(value.error());
    }
    auto operands = std::vector<SemCallArgument>();
    operands.push_back({.access = AccessMode::Read, .expression = std::move(*value)});
    for (auto index = 0uz; index < arguments.size(); ++index) {
        const auto& argument = arguments[index];
        const auto execution = site.enter_operand_execution(state.completes);
        auto built = (co_await site.read_argument(
            argument,
            site.draft().builtin_type(contract.arguments[index])
        ));
        if (!built) {
            co_return std::unexpected(built.error());
        }
        auto operand =
            site.consume_read(state, std::move(*built), site.syntax().expression(argument).span);
        if (!operand) {
            co_return std::unexpected(operand.error());
        }
        operands.push_back({.access = AccessMode::Read, .expression = std::move(*operand)});
    }
    co_return construct_slice_value(
        site,
        intrinsic,
        receiver_type,
        extent,
        std::move(operands),
        std::move(state),
        span
    );
}
