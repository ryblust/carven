module carven:semantic.analysis.decl.resolve.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.interop;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.constant.proof;
import :semantic.analysis.decl;
import :semantic.analysis.decl.context;
import :semantic.analysis.decl.resolver;
import :semantic.analysis.interop;
import :semantic.analysis.nominal.containment;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :semantic.visibility;
import :source.module_path;
import :source.provenance.ids;
import :source.text;
import :support.invariant;
import :support.visit;
import std;

namespace decl_resolution {

auto DeclarationResolver::supports_equality(
    ConstructionTypeRef type,
    std::flat_set<TypeID>& visiting
) noexcept -> bool {
    if (const auto* term = std::get_if<TypeTermID>(&type)) {
        const auto construction = draft.construction_type_copy(*term);
        if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&construction.value)) {
            return supports_equality(array->element, visiting);
        }
        return false;
    }
    const auto concrete = std::get<TypeID>(type);
    if (!visiting.insert(concrete).second) {
        return true;
    }
    const auto result = std::visit(
        Overloaded {
            [](const BuiltinTypeValue& value) noexcept {
                return builtin_type_supports_equality(value.kind);
            },
            [&](const StructTypeValue& value) noexcept {
                if (value.structure.index() >= structures.size()
                    || !structures[value.structure.index()].has_value()) {
                    return false;
                }
                return std::ranges::all_of(
                    structures[value.structure.index()]->fields,
                    [&](const ConstructionStructField& field) noexcept {
                        return supports_equality(field.type, visiting);
                    }
                );
            },
            [&](const EnumTypeValue& value) noexcept {
                if (value.enumeration.index() >= enumerations.size()
                    || !enumerations[value.enumeration.index()].has_value()) {
                    return false;
                }
                const auto& declaration = *enumerations[value.enumeration.index()];
                if (std::holds_alternative<ConstructionNumericEnumRepresentation>(
                        declaration.representation
                    )) {
                    return true;
                }
                return std::ranges::all_of(declaration.cases, [&](EnumCaseID case_id) noexcept {
                    return std::ranges::all_of(
                        enum_cases[case_id.index()]->payload_types,
                        [&](ConstructionTypeRef payload) noexcept {
                            return supports_equality(payload, visiting);
                        }
                    );
                });
            },
            [&](const ArrayTypeValue& value) noexcept {
                return supports_equality(ConstructionTypeRef {value.element}, visiting);
            },
            [](const FunctionTypeValue&) static noexcept { return false; },
            [](const ClosureTypeValue&) static noexcept { return false; },
            [](const CallableViewTypeValue&) static noexcept { return false; },
        },
        draft.type_copy(concrete).value
    );
    visiting.erase(concrete);
    return result;
}

auto DeclarationResolver::finish_capabilities() noexcept -> void {
    for (const auto& symbol : catalog.symbols()) {
        if (const auto* form = std::get_if<CatalogStructForm>(&symbol.form)) {
            auto visiting = std::flat_set<TypeID>();
            const auto type = draft.intern_type(
                CanonicalType {
                    .value = StructTypeValue {.structure = form->structure},
                }
            );
            structures[form->structure.index()]->capabilities.equality =
                supports_equality(ConstructionTypeRef {type}, visiting);
        } else if (const auto* form = std::get_if<CatalogEnumForm>(&symbol.form)) {
            auto visiting = std::flat_set<TypeID>();
            const auto type = draft.intern_type(
                CanonicalType {
                    .value = EnumTypeValue {.enumeration = form->enumeration},
                }
            );
            enumerations[form->enumeration.index()]->capabilities.equality =
                supports_equality(ConstructionTypeRef {type}, visiting);
        }
    }
}

auto DeclarationResolver::publish() noexcept -> void {
    for (const auto& module : catalog.modules()) {
        const auto syntax = draft.syntax_tree(module.module_id).view();
        const auto& source = syntax.ast_module();
        auto declaration = ModuleDeclaration {
            .provenance_module = module.module_id,
            .origin = declaration_origin(draft, module.module_id, source.span),
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {},
        };
        declaration.cpp_headers.reserve(source.cpp_header_imports.size());
        for (const auto& header : source.cpp_header_imports) {
            declaration.cpp_headers.push_back({
                .delimiter = header.delimiter == ASTCppHeaderDelimiter::AngleBrackets
                    ? CppHeaderDelimiter::AngleBrackets
                    : CppHeaderDelimiter::Quotes,
                .name = draft.intern_spelling(
                    draft.source_slice_copy(module.module_id, header.name_span)
                ),
                .origin = declaration_origin(draft, module.module_id, header.span),
            });
        }
        declaration.cpp_source_fragments.reserve(source.cpp_source_fragments.size());
        for (const auto& fragment : source.cpp_source_fragments) {
            declaration.cpp_source_fragments.push_back({
                .payload_origin =
                    declaration_origin(draft, module.module_id, fragment.payload_span),
            });
        }
        declaration.items.reserve(module.items.size());
        for (const auto& item : module.items) {
            declaration.items.push_back(
                std::visit(
                    Overloaded {
                        [](FunctionID id) static noexcept -> ModuleItem { return id; },
                        [](StructID id) static noexcept -> ModuleItem { return id; },
                        [](EnumID id) static noexcept -> ModuleItem { return id; },
                        [](ModuleConstantID id) static noexcept -> ModuleItem { return id; },
                        [](const CatalogTestForm& test) static noexcept -> ModuleItem {
                            return test.test;
                        },
                    },
                    item.form
                )
            );
        }
        if (module.declaration.index() >= modules.size()) {
            invariant_violation("module declaration identity is outside its reserved table");
        }
        modules[module.declaration.index()] = std::move(declaration);
    }

    for (const auto& module : catalog.modules()) {
        if (!modules[module.declaration.index()].has_value()) {
            invariant_violation("resolved module has no declaration fact");
        }
        draft.define_declaration(
            module.declaration,
            std::move(*modules[module.declaration.index()])
        );
    }
    for (const auto& symbol : catalog.symbols()) {
        std::visit(
            Overloaded {
                [&](const CatalogFunctionForm& form) noexcept {
                    if (!functions[form.function.index()].has_value()
                        || !callable_contracts[form.callable.index()].has_value()) {
                        invariant_violation("resolved function has an incomplete contract");
                    }
                    draft.define_declaration(form.function, *functions[form.function.index()]);
                    draft.define_callable_contract(
                        form.callable,
                        std::move(*callable_contracts[form.callable.index()])
                    );
                },
                [&](const CatalogStructForm& form) noexcept {
                    if (!structures[form.structure.index()].has_value()) {
                        invariant_violation("resolved struct has no declaration fact");
                    }
                    draft.define_declaration(
                        form.structure,
                        std::move(*structures[form.structure.index()])
                    );
                },
                [&](const CatalogEnumForm& form) noexcept {
                    if (!enumerations[form.enumeration.index()].has_value()) {
                        invariant_violation("resolved enum has no declaration fact");
                    }
                    draft.define_declaration(
                        form.enumeration,
                        std::move(*enumerations[form.enumeration.index()])
                    );
                },
                [&](const CatalogEnumCaseForm& form) noexcept {
                    if (!enum_cases[form.enum_case.index()].has_value()) {
                        invariant_violation("resolved enum case has no declaration fact");
                    }
                    draft.define_declaration(
                        form.enum_case,
                        std::move(*enum_cases[form.enum_case.index()])
                    );
                },
                [&](const CatalogConstantForm& form) noexcept {
                    if (!module_constants[form.constant.index()].has_value()) {
                        invariant_violation("resolved module constant has no declaration fact");
                    }
                    draft.define_declaration(
                        form.constant,
                        *module_constants[form.constant.index()]
                    );
                },
            },
            symbol.form
        );
    }
    draft.finish_declarations();
    for (const auto& symbol : catalog.symbols()) {
        const auto* form = std::get_if<CatalogFunctionForm>(&symbol.form);
        if (form == nullptr || !cpp_import_origins[form->callable.index()].has_value()) {
            continue;
        }
        draft.complete_callable(
            form->callable,
            CppImportImplementation {
                .form_origin = *cpp_import_origins[form->callable.index()],
            }
        );
    }
}

auto resolve_declarations(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ImportUsage& import_usage
) noexcept -> AnalysisResult<void> {
    return DeclarationResolver(draft, catalog, import_usage).run();
}

} // namespace decl_resolution
