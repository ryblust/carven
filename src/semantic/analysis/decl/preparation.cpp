module carven:semantic.analysis.decl.preparation.impl;

import :semantic.analysis.decl.context;
import :semantic.analysis.decl.resolver;
import :semantic.semir.decl;
import :semantic.semir.type;
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
        const auto pending = draft.pending_function_contract_copy(function->callable);
        const auto failures = pending
            ? pending->failures
            : draft.construction_callable_contract_copy(function->callable).failures;
        for (const auto type : draft.construction_failure_term_copy(failures).direct_members) {
            completed = (co_await prepare_type(type, requester, origin));
            if (!completed) {
                co_return completed;
            }
        }
        if (pending) {
            for (const auto& parameter : pending->parameters) {
                completed = (co_await prepare_type(parameter.type, requester, origin));
                if (!completed) {
                    co_return completed;
                }
            }
        } else {
            const auto contract = draft.construction_callable_contract_copy(function->callable);
            for (const auto& parameter : contract.parameters) {
                completed = (co_await prepare_type(parameter.type, requester, origin));
                if (!completed) {
                    co_return completed;
                }
            }
            completed = (co_await prepare_type(contract.result, requester, origin));
            if (!completed) {
                co_return completed;
            }
        }
    }
    co_return publish_declaration(symbol);
}

auto DeclResolver::prepare_type(
    ConstructionTypeRef type,
    ProgramModuleID requester,
    Span span
) noexcept -> AnalysisTask<void> {
    if (heads_finished) {
        co_return {};
    }
    auto visiting = std::flat_set<TypeID>();
    auto prepared = std::vector<CatalogSymbolID>();
    auto result = (co_await prepare_type_dependencies(type, requester, span, visiting, prepared));
    if (!result) {
        co_return result;
    }
    // Resolve the whole graph before computing capabilities, including cyclic
    // nominal shapes. Nested construction requests get their own traversal.
    for (const auto id : prepared) {
        result = publish_declaration(require_catalog_symbol(catalog, id));
        if (!result) {
            co_return result;
        }
    }
    co_return {};
}

auto DeclResolver::prepare_type_dependencies(
    ConstructionTypeRef type,
    ProgramModuleID requester,
    Span span,
    std::flat_set<TypeID>& visiting,
    std::vector<CatalogSymbolID>& prepared
) noexcept -> AnalysisTask<void> {
    if (const auto* term = std::get_if<TypeTermID>(&type)) {
        const auto construction = draft.construction_type_copy(*term);
        co_return (co_await construction.value.visit(
            [&](const auto& value) noexcept -> AnalysisTask<void> {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<Value, ConstructionCallableViewTypeValue>) {
                    for (const auto& parameter : value.parameters) {
                        auto result = (co_await prepare_type_dependencies(
                            parameter.type,
                            requester,
                            span,
                            visiting,
                            prepared
                        ));
                        if (!result) {
                            co_return result;
                        }
                    }
                    co_return (co_await prepare_type_dependencies(
                        value.result,
                        requester,
                        span,
                        visiting,
                        prepared
                    ));
                } else {
                    co_return (co_await prepare_type_dependencies(
                        value.element,
                        requester,
                        span,
                        visiting,
                        prepared
                    ));
                }
            }
        ));
    }
    const auto concrete = std::get<TypeID>(type);
    if (!visiting.insert(concrete).second) {
        co_return {};
    }
    const auto canonical = draft.type_copy(concrete);
    co_return (co_await canonical.value.visit(
        Overloaded {
            [&](const StructTypeValue& value) noexcept -> AnalysisTask<void> {
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
            [&](const CatalogFunctionForm& form) noexcept {
                if (cpp_import_origins[form.callable.index()]) {
                    draft.complete_callable(
                        form.callable,
                        CppImportImplementation {
                            .form_origin = *cpp_import_origins[form.callable.index()],
                        }
                    );
                }
            },
            [&](const CatalogStructForm& form) noexcept {
                const auto type =
                    draft.intern_type({.value = StructTypeValue {.structure = form.structure}});
                auto& declaration = *structures[form.structure.index()];
                declaration.capabilities.equality = supports_equality(type);
                draft.define_declaration(form.structure, declaration);
            },
            [&](const CatalogEnumForm& form) noexcept {
                const auto type =
                    draft.intern_type({.value = EnumTypeValue {.enumeration = form.enumeration}});
                auto& declaration = *enumerations[form.enumeration.index()];
                declaration.capabilities.equality = supports_equality(type);
                draft.define_declaration(form.enumeration, declaration);
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
