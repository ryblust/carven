module carven:semantic.analysis.decl.preparation.impl;

import :semantic.analysis.decl.context;
import :semantic.analysis.decl.resolver;
import :semantic.semir.decl;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

auto DeclResolver::ensure_available(
    CatalogSymbolID id,
    ProgramModuleID requester,
    Span origin
) noexcept -> AnalysisTask<void> {
    if (heads_finished || published[id.index()]) {
        co_return {};
    }
    auto completed = (co_await resolve(id, requester, origin));
    if (!completed) {
        co_return completed;
    }
    const auto& symbol = require_catalog_symbol(catalog, id);
    if (const auto* generic = std::get_if<CatalogGenericForm>(&symbol.form)) {
        auto definitions = std::vector<GenericDeclarationID> {generic->definition};
        auto visited = std::flat_set<GenericDeclarationID>();
        auto completed_sources = std::vector<CatalogSymbolID>();
        auto visited_types = std::flat_set<ConstructionTypeRef>();
        const auto collect_dependencies = [&](auto&& self,
                                              ConstructionTypeRef type) noexcept -> void {
            if (!visited_types.insert(type).second) {
                return;
            }
            const auto* generic = std::get_if<GenericTypeID>(&type);
            if (!generic) {
                return;
            }
            const auto expression = draft.generic_type_copy(*generic);
            if (const auto* application = std::get_if<GenericNominalApplication>(&expression)) {
                definitions.push_back(application->definition);
            }
            visit_generic_type_children(expression, [&](ConstructionTypeRef child) noexcept {
                self(self, child);
            });
        };
        for (auto index = 0uz; index < definitions.size(); ++index) {
            const auto definition_id = definitions[index];
            if (!visited.insert(definition_id).second) {
                continue;
            }

            const auto source = catalog.generic_symbol(definition_id);
            completed = (co_await resolve(source, requester, origin));
            if (!completed) {
                co_return completed;
            }
            completed_sources.push_back(source);
            draft.generic_declaration_copy(definition_id)
                .visit(
                    Overloaded {
                        [&](const GenericFunctionDefinition& function) noexcept {
                            for (const auto& parameter : function.signature.parameters) {
                                if (const auto* type =
                                        std::get_if<GenericTypeID>(&parameter.type)) {
                                    collect_dependencies(collect_dependencies, *type);
                                }
                            }
                            if (const auto* type =
                                    std::get_if<GenericTypeID>(&function.signature.result)) {
                                collect_dependencies(collect_dependencies, *type);
                            }
                        },
                        [&](const GenericRecordDefinition& record) noexcept {
                            for (const auto& field : record.fields) {
                                collect_dependencies(collect_dependencies, field.type);
                            }
                        },
                        [&](const GenericEnumDefinition& enumeration) noexcept {
                            for (const auto& source_case : enumeration.cases) {
                                for (const auto type : source_case.payload_types) {
                                    collect_dependencies(collect_dependencies, type);
                                }
                            }
                        },
                    }
                );
        }
        for (const auto source : completed_sources) {
            published[source.index()] = true;
        }
        co_return {};
    }
    if (const auto* structure = std::get_if<CatalogStructForm>(&symbol.form)) {
        co_return (co_await prepare_type(
            draft.intern_type({.value = StructTypeValue {.structure = structure->structure}}),
            requester,
            origin
        ));
    }
    if (const auto* enumeration = std::get_if<CatalogEnumForm>(&symbol.form)) {
        co_return (co_await prepare_type(
            draft.intern_type({.value = EnumTypeValue {.enumeration = enumeration->enumeration}}),
            requester,
            origin
        ));
    }
    if (const auto* item = std::get_if<CatalogEnumCaseForm>(&symbol.form)) {
        co_return (co_await ensure_available(catalog.enum_symbol(item->owner), requester, origin));
    }
    if (const auto* function = std::get_if<CatalogFunctionForm>(&symbol.form)) {
        completed = co_await prepare_callable(function->callable, requester, origin);
        if (!completed) {
            co_return completed;
        }
    }
    co_return publish_declaration(symbol);
}

auto DeclResolver::prepare_callable(
    CallableID callable,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<void> {
    const auto pending = draft.pending_function_contract_copy(callable);
    const auto failures =
        pending ? pending->failures : draft.construction_callable_contract_copy(callable).failures;
    for (const auto type : draft.construction_failure_known_members(failures)) {
        auto completed = co_await prepare_type(type, requester, span);
        if (!completed) {
            co_return completed;
        }
    }
    if (pending) {
        for (const auto& parameter : pending->parameters) {
            auto completed = co_await prepare_type(parameter.type, requester, span);
            if (!completed) {
                co_return completed;
            }
        }
    } else {
        const auto contract = draft.construction_callable_contract_copy(callable);
        for (const auto& parameter : contract.parameters) {
            auto completed = co_await prepare_type(parameter.type, requester, span);
            if (!completed) {
                co_return completed;
            }
        }
        co_return co_await prepare_type(contract.result, requester, span);
    }
    co_return {};
}

auto DeclResolver::prepare_type(
    ConstructionTypeRef type,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<void> {
    if (std::holds_alternative<GenericTypeID>(type)) {
        co_return {};
    }

    if (heads_finished) {
        co_return {};
    }
    auto visiting = std::flat_set<ConstructionTypeRef>();
    auto prepared = std::vector<CatalogSymbolID>();
    auto result = (co_await prepare_type_dependencies(type, requester, span, visiting, prepared));
    if (!result) {
        co_return result;
    }
    // Resolve the whole graph before computing enum equality, including cyclic
    // nominal shapes. Nested construction requests get their own traversal.
    for (const auto id : prepared) {
        result = publish_declaration(require_catalog_symbol(catalog, id));
        if (!result) {
            co_return result;
        }
    }
    auto roots = std::vector<TypeID>();
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        roots.push_back(*concrete);
    }
    for (const auto id : prepared) {
        const auto& symbol = require_catalog_symbol(catalog, id);
        if (const auto* enumeration = std::get_if<CatalogEnumForm>(&symbol.form)) {
            roots.push_back(draft.intern_type(
                {.value = EnumTypeValue {.enumeration = enumeration->enumeration}}
            ));
        }
    }
    draft.resolve_enum_equality(roots);
    co_return {};
}

auto DeclResolver::prepare_type_dependencies(
    ConstructionTypeRef type,
    ProgramModuleID requester,
    Span span,
    std::flat_set<ConstructionTypeRef>& visiting,
    std::vector<CatalogSymbolID>& prepared
) noexcept -> AnalysisTask<void> {
    if (!visiting.insert(type).second) {
        co_return {};
    }
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        const auto canonical = draft.type_copy(*concrete);

        if (std::holds_alternative<FunctionTypeValue>(canonical.value)
            || std::holds_alternative<ClosureTypeValue>(canonical.value)) {
            co_return {};
        }
    }
    const auto callable = draft.callable_shape(type);
    if (callable && !callable->owning_type) {
        for (const auto& parameter : callable->parameters) {
            auto completed = (co_await prepare_type_dependencies(
                parameter.type,
                requester,
                span,
                visiting,
                prepared
            ));
            if (!completed) {
                co_return completed;
            }
        }
        auto completed = (co_await prepare_type_dependencies(
            callable->result,
            requester,
            span,
            visiting,
            prepared
        ));
        if (!completed) {
            co_return completed;
        }
        const auto failures = draft.construction_failure_known_members(callable->failures);
        for (const auto failure : failures) {
            completed =
                (co_await prepare_type_dependencies(failure, requester, span, visiting, prepared));
            if (!completed) {
                co_return completed;
            }
        }
        co_return {};
    }
    if (const auto* term = std::get_if<TypeTermID>(&type)) {
        const auto construction = draft.construction_type_copy(*term);
        co_return (co_await construction.value.visit(
            [&](const auto& value) noexcept -> AnalysisTask<void> {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<Value, ConstructionTupleTypeValue>) {
                    for (const auto element : value.elements) {
                        auto completed = (co_await prepare_type_dependencies(
                            element,
                            requester,
                            span,
                            visiting,
                            prepared
                        ));
                        if (!completed) {
                            co_return completed;
                        }
                    }
                    co_return {};
                } else if constexpr (std::same_as<Value, ConstructionOperationTypeValue>) {
                    co_return (co_await prepare_type_dependencies(
                        value.success,
                        requester,
                        span,
                        visiting,
                        prepared
                    ));
                } else if constexpr (std::same_as<Value, ConstructionArrayTypeValue>
                                     || std::same_as<Value, ConstructionSliceTypeValue>) {
                    co_return (co_await prepare_type_dependencies(
                        value.element,
                        requester,
                        span,
                        visiting,
                        prepared
                    ));
                } else {
                    static_assert(std::same_as<Value, ConstructionCallableViewTypeValue>);
                    invariant_violation("callable view lost its construction shape");
                }
            }
        ));
    }
    const auto concrete = std::get<TypeID>(type);
    const auto canonical = draft.type_copy(concrete);
    co_return (co_await canonical.value.visit(
        Overloaded {
            [&](const StructTypeValue& value) noexcept -> AnalysisTask<void> {
                if (value.structure.index() >= catalog.struct_count()) {
                    const auto instance =
                        draft.construction_struct_declaration_copy(value.structure);
                    for (const auto& field : instance.fields) {
                        auto completed = (co_await prepare_type_dependencies(
                            field.type,
                            requester,
                            span,
                            visiting,
                            prepared
                        ));
                        if (!completed) {
                            co_return completed;
                        }
                    }
                    co_return {};
                }
                const auto id = catalog.struct_symbol(value.structure);
                auto completed = (co_await resolve(id, requester, span));
                if (!completed || published[id.index()]) {
                    co_return completed;
                }
                for (const auto& field : structures[value.structure.index()]->fields) {
                    completed = (co_await prepare_type_dependencies(
                        field.type,
                        requester,
                        span,
                        visiting,
                        prepared
                    ));
                    if (!completed) {
                        co_return completed;
                    }
                }
                prepared.push_back(id);
                co_return {};
            },
            [&](const EnumTypeValue& value) noexcept -> AnalysisTask<void> {
                if (value.enumeration.index() >= catalog.enum_count()) {
                    const auto instance = draft.enum_declaration_copy(value.enumeration);
                    for (const auto case_id : instance.cases) {
                        const auto declaration =
                            draft.construction_enum_case_declaration_copy(case_id);
                        for (const auto payload : declaration.payload_types) {
                            auto completed = (co_await prepare_type_dependencies(
                                payload,
                                requester,
                                span,
                                visiting,
                                prepared
                            ));
                            if (!completed) {
                                co_return completed;
                            }
                        }
                    }
                    co_return {};
                }
                const auto id = catalog.enum_symbol(value.enumeration);
                auto completed = (co_await resolve(id, requester, span));
                if (!completed || published[id.index()]) {
                    co_return completed;
                }
                for (const auto case_id : enumerations[value.enumeration.index()]->cases) {
                    const auto case_symbol = catalog.enum_case_symbol(case_id);
                    completed = (co_await resolve(case_symbol, requester, span));
                    if (!completed) {
                        co_return completed;
                    }
                    for (const auto payload : enum_cases[case_id.index()]->payload_types) {
                        completed = (co_await prepare_type_dependencies(
                            payload,
                            requester,
                            span,
                            visiting,
                            prepared
                        ));
                        if (!completed) {
                            co_return completed;
                        }
                    }
                    prepared.push_back(case_symbol);
                }
                prepared.push_back(id);
                co_return {};
            },
            [&](const ArrayTypeValue& value) noexcept {
                return prepare_type_dependencies(
                    value.element,
                    requester,
                    span,
                    visiting,
                    prepared
                );
            },
            [&](const VectorTypeValue& value) noexcept {
                return prepare_type_dependencies(
                    value.element,
                    requester,
                    span,
                    visiting,
                    prepared
                );
            },
            [&](const TupleTypeValue& value) noexcept -> AnalysisTask<void> {
                for (const auto element : value.elements) {
                    auto completed = (co_await prepare_type_dependencies(
                        element,
                        requester,
                        span,
                        visiting,
                        prepared
                    ));
                    if (!completed) {
                        co_return completed;
                    }
                }
                co_return {};
            },
            [&](const SliceTypeValue& value) noexcept {
                return prepare_type_dependencies(
                    value.element,
                    requester,
                    span,
                    visiting,
                    prepared
                );
            },
            [&](const PointerTypeValue& value) noexcept {
                return prepare_type_dependencies(value.target, requester, span, visiting, prepared);
            },
            [](const auto&) static noexcept -> AnalysisTask<void> { co_return {}; },
        }
    ));
}

auto DeclResolver::publish_declaration(const CatalogSymbol& symbol) noexcept
    -> AnalysisResult<void> {
    if (published[symbol.symbol_id.index()]) {
        return {};
    }
    symbol.form.visit(
        Overloaded {
            [](const CatalogGenericForm&) static noexcept {},
            [&](const CatalogFunctionForm& form) noexcept {
                if (cpp_imports[form.callable.index()]) {
                    draft.complete_callable(form.callable, *cpp_imports[form.callable.index()]);
                }
            },
            [&](const CatalogStructForm& form) noexcept {
                draft.define_declaration(form.structure, *structures[form.structure.index()]);
            },
            [&](const CatalogEnumForm& form) noexcept {
                draft.define_declaration(form.enumeration, *enumerations[form.enumeration.index()]);
            },
            [&](const CatalogEnumCaseForm& form) noexcept {
                draft.define_declaration(form.enum_case, *enum_cases[form.enum_case.index()]);
            },
            [&](const CatalogConstantForm& form) noexcept {
                draft.define_declaration(form.constant, *module_constants[form.constant.index()]);
            },
        }
    );
    published[symbol.symbol_id.index()] = true;
    return {};
}
