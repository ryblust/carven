module carven:semantic.analysis.catalog.decl.impl;

import :diagnostics.builder;
import :frontend.ast.decl;
import :frontend.ast.interop;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.catalog;
import :semantic.analysis.catalog.builder;
import :semantic.analysis.program;
import :semantic.analysis.source.imports;
import :semantic.analysis.types;
import :semantic.visibility;
import :source.standard;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto declaration_name(const ASTItem& item) noexcept -> std::optional<Span> {
    return item.value.visit(
        Overloaded {
            [](const ASTEnumDecl& value) static noexcept -> std::optional<Span> {
                return value.name_span;
            },
            [](const ASTRecordDecl& value) static noexcept -> std::optional<Span> {
                return value.name_span;
            },
            [](const ASTFunctionDecl& value) static noexcept -> std::optional<Span> {
                return value.name_span;
            },
            [](const ASTConstantDecl& value) static noexcept -> std::optional<Span> {
                return value.name_span;
            },
            [](const ASTConstBlock&) static noexcept -> std::optional<Span> {
                return std::nullopt;
            },
            [](const ASTTestDecl&) static noexcept -> std::optional<Span> { return std::nullopt; },
        }
    );
}


} // namespace

auto AnalysisCatalogBuilder::collect_declarations() noexcept -> void {
    const auto syntax_trees = draft.syntax_trees();
    const auto module_count = draft.module_count();

    if (syntax_trees.size() != module_count) {
        invariant_violation("syntax trees are not aligned with compilation modules");
    }

    result.modules.reserve(module_count);
    result.visible_candidates.reserve(module_count);
    for (auto module_index = 0uz; module_index < module_count; ++module_index) {
        const auto module_id = draft.provenance_module_at(module_index);
        if (draft.imported(module_id)) {
            continue;
        }
        const auto source_id = syntax_trees[module_index]->view().source_id();
        const auto ast = syntax_trees[module_index]->view();
        const auto& ast_module = ast.ast_module();
        const auto declaration = draft.reserve_module_declaration();
        if (declaration.index() != module_index) {
            invariant_violation("semantic module reservation is not aligned with source modules");
        }
        auto catalog_module = CatalogModule {
            .module_id = module_id,
            .declaration = declaration,
            .symbols = {},
            .items = {},
        };
        auto names = std::flat_map<std::string, Span, std::less<>>();
        auto test_names = std::flat_map<std::string, Span, std::less<>>();
        auto local_candidates =
            std::flat_map<std::string, std::vector<CatalogLookupCandidate>, std::less<>>();
        const auto provided = find_provided_module(draft.module_path_copy(module_id).value());
        for (const auto& entry : provided_exports()) {
            if (provided == entry.provider) {
                local_candidates[std::string(entry.name)].push_back(
                    {.entity = entry.identity, .import_binding = std::nullopt}
                );
            }
        }
        auto first_entry = std::optional<SourceSpan>();
        for (const auto item_id : ast_module.items) {
            const auto& item = ast.item(item_id);
            if (const auto* function = std::get_if<ASTFunctionDecl>(&item.value)) {
                const auto function_name = function->is_implicit_entry
                    ? std::string("main")
                    : draft.source_slice_copy(module_id, function->name_span);
                const auto is_cpp_import =
                    std::holds_alternative<ASTCppImportForm>(function->implementation);
                if (function_name == "main" && !is_cpp_import) {
                    const auto current = locate(
                        source_id,
                        function->is_implicit_entry ? item.span : function->name_span
                    );
                    if (first_entry.has_value()) {
                        auto diagnostic = DiagnosticBuilder(
                            DiagnosticCode::EntryDuplicate,
                            "a module may define only one entry function"
                        );
                        diagnostic.primary(current, "duplicate entry");
                        diagnostic.related(*first_entry, "first entry");
                        diagnostics.push_back(diagnostic.build());
                    } else {
                        first_entry = current;
                    }
                }
            }
            if (const auto* test = std::get_if<ASTTestDecl>(&item.value);
                test != nullptr && test->label) {
                if (test->label->text == "main") {
                    diagnostics.push_back(DiagnosticBuilder(
                                              DiagnosticCode::TestMainName,
                                              "a test cannot be named 'main'"
                    )
                                              .primary(locate(source_id, test->label->span))
                                              .build());
                }
                const auto [position, inserted] =
                    test_names.emplace(test->label->text, test->label->span);
                if (!inserted) {
                    auto diagnostic = DiagnosticBuilder(
                        DiagnosticCode::TestDuplicateName,
                        "a test name is defined more than once"
                    );
                    diagnostic.primary(locate(source_id, test->label->span), "duplicate test name");
                    diagnostic.related(locate(source_id, position->second), "first definition");
                    diagnostics.push_back(diagnostic.build());
                }
            }
            const auto name_span = declaration_name(item);
            if (!name_span.has_value()) {
                if (std::holds_alternative<ASTConstBlock>(item.value)) {
                    catalog_module.items.push_back({
                        .item_id = item_id,
                        .form = CatalogConstBlockForm {},
                    });
                }
                if (std::holds_alternative<ASTTestDecl>(item.value)) {
                    catalog_module.items.push_back({
                        .item_id = item_id,
                        .form = CatalogTestForm {.test = draft.reserve_test()},
                    });
                }
                continue;
            }
            const auto* function = std::get_if<ASTFunctionDecl>(&item.value);
            const auto implicit_entry = function != nullptr && function->is_implicit_entry;
            auto name = implicit_entry ? std::string("main")
                                       : draft.source_slice_copy(module_id, *name_span);
            if (local_candidates.contains(name)
                && std::holds_alternative<ProvidedExport>(
                    local_candidates.at(name).front().entity
                )) {
                diagnostics.push_back(binding_error(
                    source_id,
                    "a source declaration conflicts with a provided module declaration",
                    *name_span
                ));
                continue;
            }
            if (source_type_name_is_reserved(name)) {
                diagnostics.push_back(
                    binding_error(source_id, "language type names are reserved", *name_span)
                );
                continue;
            }
            if (const auto prior = names.find(name); !implicit_entry && prior != names.end()) {
                auto error = binding_error(
                    source_id,
                    "a module declaration name is defined more than once",
                    *name_span
                );
                error.attachment.primary->message = "duplicate declaration";
                error.attachment.related.push_back({
                    .span = locate(source_id, prior->second),
                    .message = "first declaration",
                });
                diagnostics.push_back(std::move(error));
                continue;
            }
            if (!implicit_entry) {
                names.emplace(name, *name_span);
            }
            if (result.symbols.size() == std::numeric_limits<std::uint32_t>::max()) {
                resource_limit_exceeded("catalog symbols exhausted their 32-bit identity space");
            }
            const auto symbol_id =
                CatalogSymbolID::from_index(static_cast<std::uint32_t>(result.symbols.size()));
            auto form = item.value.visit(
                Overloaded {
                    [&](const ASTFunctionDecl& source_function) noexcept -> CatalogSymbolForm {
                        if (source_function.type_parameters) {
                            const auto definition = draft.reserve_generic_declaration();
                            if (definition.index() != result.generic_symbols.size()) {
                                invariant_violation("semantic generic reservation is not aligned");
                            }
                            result.generic_symbols.push_back(symbol_id);
                            return CatalogGenericForm {.definition = definition, .operations = {}};
                        }
                        const auto function = draft.reserve_function_declaration();
                        result.function_symbols.resize(function.index() + 1uz);
                        result.function_symbols[function.index()] = symbol_id;
                        return CatalogFunctionForm {
                            .function = function,
                            .callable = draft.reserve_callable_declaration(),
                        };
                    },
                    [&](const ASTRecordDecl& record) noexcept -> CatalogSymbolForm {
                        if (record.type_parameters) {
                            const auto definition = draft.reserve_generic_declaration();
                            if (definition.index() != result.generic_symbols.size()) {
                                invariant_violation("semantic generic reservation is not aligned");
                            }
                            result.generic_symbols.push_back(symbol_id);
                            return CatalogGenericForm {.definition = definition, .operations = {}};
                        }
                        const auto structure = draft.reserve_struct_declaration();
                        result.struct_symbols.resize(structure.index() + 1uz);
                        result.struct_symbols[structure.index()] = symbol_id;
                        return CatalogStructForm {
                            .structure = structure,
                            .operations = {},
                        };
                    },
                    [&](const ASTEnumDecl& source_enum) noexcept -> CatalogSymbolForm {
                        if (source_enum.type_parameters) {
                            const auto definition = draft.reserve_generic_declaration();
                            if (definition.index() != result.generic_symbols.size()) {
                                invariant_violation("semantic generic reservation is not aligned");
                            }
                            result.generic_symbols.push_back(symbol_id);
                            return CatalogGenericForm {.definition = definition, .operations = {}};
                        }
                        const auto enumeration = draft.reserve_enum_declaration();
                        result.enum_symbols.resize(enumeration.index() + 1uz);
                        result.enum_symbols[enumeration.index()] = symbol_id;
                        return CatalogEnumForm {
                            .enumeration = enumeration,
                            .cases = {},
                        };
                    },
                    [&](const ASTConstantDecl&) noexcept -> CatalogSymbolForm {
                        return CatalogConstantForm {
                            .constant = draft.reserve_module_constant_declaration(),
                        };
                    },
                    [](const ASTConstBlock&) static noexcept -> CatalogSymbolForm {
                        invariant_violation("const block has no symbol form");
                    },
                    [](const ASTTestDecl&) static noexcept -> CatalogSymbolForm {
                        invariant_violation("catalog declaration item has no symbol form");
                    },
                }
            );
            result.symbols.push_back({
                .symbol_id = symbol_id,
                .module_id = module_id,
                .item_id = item_id,
                .name = std::move(name),
                .form = std::move(form),
                .visibility = source_declaration_visibility(item),
                .declaration_span = *name_span,
                .class_operation = std::nullopt,
            });
            catalog_module.symbols.push_back(symbol_id);
            if (!implicit_entry) {
                local_candidates[result.symbols[symbol_id.index()].name].push_back(
                    {.entity = symbol_id, .import_binding = std::nullopt}
                );
            }

            result.symbols[symbol_id.index()].form.visit(
                Overloaded {
                    [&](const CatalogFunctionForm& value) noexcept {
                        catalog_module.items.push_back({
                            .item_id = item_id,
                            .form = value.function,
                        });
                    },
                    [&](const CatalogStructForm& value) noexcept {
                        catalog_module.items.push_back({
                            .item_id = item_id,
                            .form = value.structure,
                        });
                    },
                    [&](const CatalogEnumForm& value) noexcept {
                        catalog_module.items.push_back({
                            .item_id = item_id,
                            .form = value.enumeration,
                        });
                    },
                    [&](const CatalogConstantForm& value) noexcept {
                        catalog_module.items.push_back({
                            .item_id = item_id,
                            .form = value.constant,
                        });
                    },
                    [](const CatalogGenericForm&) static noexcept {},
                    [](const CatalogEnumCaseForm&) static noexcept {
                        invariant_violation("enum case cannot be a module declaration item");
                    },
                }
            );

            if (const auto* record = std::get_if<ASTRecordDecl>(&item.value)) {
                auto member_names = std::flat_set<std::string>();
                for (const auto& field : record->fields) {
                    member_names.insert(draft.source_slice_copy(module_id, field.name_span));
                }
                const auto owner = std::holds_alternative<CatalogGenericForm>(
                                       result.symbols[symbol_id.index()].form
                                   )
                    ? ClassDeclarationRef(
                          std::get<CatalogGenericForm>(result.symbols[symbol_id.index()].form)
                              .definition
                      )
                    : ClassDeclarationRef(
                          std::get<CatalogStructForm>(result.symbols[symbol_id.index()].form)
                              .structure
                      );
                for (const auto operation_id : record->operations) {
                    const auto& operation = std::get<ASTFunctionDecl>(ast.item(operation_id).value);
                    auto member_name = draft.source_slice_copy(module_id, operation.name_span);
                    if (!member_names.insert(member_name).second) {
                        diagnostics.push_back(binding_error(
                            source_id,
                            "class member name is defined more than once",
                            operation.name_span
                        ));
                        continue;
                    }
                    const auto member_id = CatalogSymbolID::from_index(
                        static_cast<std::uint32_t>(result.symbols.size())
                    );
                    const auto generic = record->type_parameters.has_value()
                        || operation.type_parameters.has_value();
                    auto operation_form = [&]() noexcept -> CatalogSymbolForm {
                        if (generic) {
                            const auto definition = draft.reserve_generic_declaration();
                            result.generic_symbols.push_back(member_id);
                            return CatalogGenericForm {.definition = definition, .operations = {}};
                        } else {
                            const auto function = draft.reserve_function_declaration();
                            const auto callable = draft.reserve_callable_declaration();
                            result.function_symbols.resize(function.index() + 1uz);
                            result.function_symbols[function.index()] = member_id;
                            catalog_module.items.push_back(
                                {.item_id = operation_id, .form = function}
                            );
                            return CatalogFunctionForm {.function = function, .callable = callable};
                        }
                    }();
                    const auto private_member =
                        std::holds_alternative<ASTPrivateDeclarationVisibility>(
                            operation.visibility
                        );
                    const auto receiver = !operation.parameters.empty()
                        && draft.source_slice_copy(
                               module_id,
                               binding_target_span(operation.parameters.front().target)
                           ) == "self";
                    const auto audience = result.symbols[symbol_id.index()].visibility;
                    result.symbols.push_back({
                        .symbol_id = member_id,
                        .module_id = module_id,
                        .item_id = operation_id,
                        .name = std::move(member_name),
                        .form = std::move(operation_form),
                        .visibility = private_member ? DeclarationVisibility::Module : audience,
                        .declaration_span = operation.name_span,
                        .class_operation = ClassOperation {
                            .owner = owner,
                            .visibility = private_member ? MemberVisibility::Private
                                                         : MemberVisibility::Public,
                            .receiver = receiver
                        },
                    });
                    if (std::holds_alternative<GenericDeclarationID>(owner)) {
                        std::get<CatalogGenericForm>(result.symbols[symbol_id.index()].form)
                            .operations.push_back(member_id);
                    } else {
                        std::get<CatalogStructForm>(result.symbols[symbol_id.index()].form)
                            .operations.push_back(member_id);
                    }
                }
            }
            const auto* enumeration = std::get_if<ASTEnumDecl>(&item.value);
            if (enumeration == nullptr || enumeration->type_parameters) {
                continue;
            }
            for (auto case_index = 0uz; case_index < enumeration->cases.size(); ++case_index) {
                const auto& enum_case = enumeration->cases[case_index];
                if (result.symbols.size() == std::numeric_limits<std::uint32_t>::max()) {
                    resource_limit_exceeded(
                        "catalog symbols exhausted their 32-bit identity space"
                    );
                }
                const auto case_symbol =
                    CatalogSymbolID::from_index(static_cast<std::uint32_t>(result.symbols.size()));
                const auto& owner_form =
                    std::get<CatalogEnumForm>(result.symbols[symbol_id.index()].form);
                const auto case_id = draft.reserve_enum_case_declaration();
                result.enum_case_symbols.resize(case_id.index() + 1uz);
                result.enum_case_symbols[case_id.index()] = case_symbol;
                result.symbols.push_back({
                    .symbol_id = case_symbol,
                    .module_id = module_id,
                    .item_id = item_id,
                    .name = draft.source_slice_copy(module_id, enum_case.name_span),
                    .form =
                        CatalogEnumCaseForm {
                            .enum_case = case_id,
                            .owner = owner_form.enumeration,
                            .index = static_cast<std::uint32_t>(case_index),
                        },
                    .visibility = result.symbols[symbol_id.index()].visibility,
                    .declaration_span = enum_case.name_span,
                    .class_operation = std::nullopt,
                });
                auto& owner = std::get<CatalogEnumForm>(result.symbols[symbol_id.index()].form);
                owner.cases.push_back(case_id);
            }
        }
        bind_cpp_imports(module_id, names);
        result.modules.push_back(std::move(catalog_module));
        result.visible_candidates.push_back(std::move(local_candidates));
    }
}
