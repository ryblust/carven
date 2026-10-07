module carven:semantic.analysis.construction.closure.impl;

import :diagnostics.builder;
import :semantic.analysis.construction.requests;
import :semantic.analysis.construction;
import :semantic.analysis.operations;
import :semantic.semir.traversal;
import std;

auto ConstructionRequests::close_generic_environment(
    std::span<const GenericArgument> arguments,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<CanonicalGenericEnvironment> {
    auto roots = std::vector<ConstructionFailureRef>();
    auto types = std::vector<ConstructionTypeRef>();
    const auto collect = [&](const GenericArgument& argument) noexcept {
        argument.visit([&](const auto& value) noexcept {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, ConstructionTypeRef>) {
                types.push_back(value);
            } else if constexpr (std::same_as<Value, ConstructionFailureRef>) {
                roots.push_back(value);
            }
        });
    };
    for (const auto& argument : arguments) {
        collect(argument);
    }
    auto seen_types = std::set<ConstructionTypeRef>();
    while (!types.empty()) {
        const auto type = unwrap_type_alias(owner.draft, types.back());
        types.pop_back();
        if (!seen_types.insert(type).second) {
            continue;
        }
        if (const auto operation = operation_shape(owner.draft, type)) {
            roots.push_back(operation->failures);
            types.push_back(operation->success);
            continue;
        }
        if (const auto* generic = std::get_if<GenericTypeID>(&type)) {
            owner.draft.generic_type_copy(*generic).visit([&](const auto& shape) noexcept {
                using Shape = std::remove_cvref_t<decltype(shape)>;
                if constexpr (std::same_as<Shape, GenericArrayType>
                              || std::same_as<Shape, GenericSliceType>
                              || std::same_as<Shape, GenericOwnedSequenceType>) {
                    types.push_back(shape.element);
                } else if constexpr (std::same_as<Shape, GenericPointerType>) {
                    types.push_back(shape.target);
                } else if constexpr (std::same_as<Shape, GenericTupleType>) {
                    types.append_range(shape.elements);
                } else if constexpr (std::same_as<Shape, GenericNominalApplication>) {
                    for (const auto& actual : shape.arguments) {
                        collect(actual);
                    }
                }
            });
        } else if (const auto* term = std::get_if<TypeTermID>(&type)) {
            owner.draft.construction_type_copy(*term).value.visit([&](const auto& shape) noexcept {
                using Shape = std::remove_cvref_t<decltype(shape)>;
                if constexpr (std::same_as<Shape, ConstructionArrayTypeValue>
                              || std::same_as<Shape, ConstructionSliceTypeValue>) {
                    types.push_back(shape.element);
                } else if constexpr (std::same_as<Shape, ConstructionTupleTypeValue>) {
                    types.append_range(shape.elements);
                }
            });
        }
    }
    auto pending = roots;
    auto seen = std::set<ConstructionFailureRef>();
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (!seen.insert(current).second) {
            continue;
        }
        if (std::holds_alternative<FailureTermID>(current)) {
            for (const auto function : owner.draft.function_declaration_ids()) {
                const auto declaration = owner.draft.function_declaration_copy(function);
                if (owner.draft.pending_function_contract_copy(declaration.callable)) {
                    continue;
                }
                const auto contract =
                    owner.draft.construction_callable_contract_copy(declaration.callable);
                if (contract.failures == current) {
                    auto completed = co_await ensure_function_body(function, requester, span);
                    if (!completed) {
                        co_return std::unexpected(completed.error());
                    }
                }
            }
            for (const auto definition : owner.draft.generic_declaration_ids()) {
                const auto source = owner.draft.generic_declaration_copy(definition);
                const auto* function = std::get_if<GenericFunctionDefinition>(&source);
                if (function && function->signature.failures == current) {
                    auto completed = co_await ensure_generic_function(definition, requester, span);
                    if (!completed) {
                        co_return std::unexpected(completed.error());
                    }
                    if (!owner.draft.has_generic_function_body(definition)) {
                        co_return std::unexpected(owner.draft.diagnostics().error(
                            DiagnosticBuilder(
                                DiagnosticCode::ConstEvaluation,
                                "static type closure depends on an unfinished generic body"
                            )
                                .primary(owner.draft.source_span(owner.draft.append_source_origin(
                                    owner.draft.module_source(requester),
                                    span
                                )))
                                .build()
                        ));
                    }
                }
            }
        }
        pending.append_range(owner.draft.failure_dependencies(current));
    }
    if (!roots.empty()) {
        const auto completed = owner.draft.complete_failure_applications(roots);
        if (!completed) {
            co_return std::unexpected(completed.error());
        }
    }
    auto preview = owner.draft.preview_failures();
    if (!preview) {
        co_return std::unexpected(preview.error());
    }
    const auto environment = owner.draft.normalize_generic_environment(arguments, *preview);
    if (!environment) {
        co_return std::unexpected(owner.draft.diagnostics().error(
            DiagnosticBuilder(
                DiagnosticCode::ConstAdmission,
                "compile-time generic call requires a closed type and failure environment"
            )
                .primary(owner.draft.source_span(
                    owner.draft.append_source_origin(owner.draft.module_source(requester), span)
                ))
                .build()
        ));
    }
    for (const auto& argument : *environment) {
        auto dependencies = std::vector<TypeID>();
        if (const auto* type = std::get_if<TypeID>(&argument)) {
            dependencies.push_back(*type);
        } else {
            dependencies = owner.draft.failure_set_copy(std::get<FailureSetID>(argument)).members;
        }
        for (const auto type : dependencies) {
            auto completed = co_await ensure_type(type, requester, span);
            if (!completed) {
                co_return std::unexpected(completed.error());
            }
        }
    }
    co_return *environment;
}

auto ConstructionRequests::close_generic_calls(SemanticRegion& region) noexcept
    -> AnalysisTask<bool> {
    auto calls = std::vector<SemanticExpression*>();
    visit_semantic_nodes(region, [&](SemanticExpression& expression) noexcept {
        if (std::holds_alternative<SemGenericCall>(expression.value)) {
            calls.push_back(&expression);
        }
    });
    for (auto* expression : calls | std::views::reverse) {
        const auto location = owner.draft.source_origin(expression->origin);
        const auto& call = std::get<SemGenericCall>(expression->value);
        auto environment = co_await close_generic_environment(
            call.environment,
            owner.draft.source_module(location.source_id),
            location.span
        );
        if (!environment) {
            co_return std::unexpected(environment.error());
        }
        const auto closed = owner.draft.instantiate_generic_call(*expression, *environment);
        if (!closed) {
            co_return std::unexpected(closed.error());
        }
    }
    co_return !calls.empty();
}
