module carven:semantic.analysis.source.completion.impl;

import :frontend.ast.decl;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.names;
import :semantic.analysis.operations;
import :semantic.analysis.source.completion;
import :semantic.analysis.types;
import :source.identifier;
import std;

auto source_name_candidates(
    const ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ProgramModuleID module_id,
    bool types_only
) noexcept -> std::vector<SourceCompletionCandidate> {
    auto result = std::vector<SourceCompletionCandidate>();
    for (const auto& name : catalog.visible_names(module_id)) {
        const auto bindings = catalog.lookup(module_id, name);
        if (bindings.size() != 1 || !source_name_available(draft, module_id, name)) {
            continue;
        }
        auto kind = SourceCompletionKind::Function;
        if (const auto* symbol = catalog.symbol(bindings.front().entity)) {
            if (!source_name_available(draft, symbol->module_id, symbol->name)) {
                continue;
            }
            kind = std::visit(
                [&](const auto& form) noexcept {
                    using T = std::decay_t<decltype(form)>;
                    if constexpr (std::same_as<T, CatalogStructForm>) {
                        return SourceCompletionKind::Structure;
                    } else if constexpr (std::same_as<T, CatalogGenericForm>) {
                        if (symbol->item_id) {
                            const auto& item =
                                draft.syntax_tree(symbol->module_id).view().item(*symbol->item_id);
                            if (std::holds_alternative<ASTFunctionDecl>(item.value)) {
                                return SourceCompletionKind::Function;
                            }
                            if (std::holds_alternative<ASTEnumDecl>(item.value)) {
                                return SourceCompletionKind::Enumeration;
                            }
                        }
                        return SourceCompletionKind::Structure;
                    } else if constexpr (std::same_as<T, CatalogEnumForm>) {
                        return SourceCompletionKind::Enumeration;
                    } else if constexpr (std::same_as<T, CatalogEnumCaseForm>) {
                        return SourceCompletionKind::EnumCase;
                    } else if constexpr (std::same_as<T, CatalogConstantForm>) {
                        return SourceCompletionKind::Constant;
                    } else {
                        return SourceCompletionKind::Function;
                    }
                },
                symbol->form
            );
        } else if (const auto* provided = std::get_if<ProvidedExport>(&bindings.front().entity)) {
            if (provided_builtin_type(*provided)
                || *provided == ProvidedExport::Vector
                || *provided == ProvidedExport::Range
                || *provided == ProvidedExport::Operation) {
                kind = SourceCompletionKind::Structure;
            }
        }
        if (types_only
            && kind != SourceCompletionKind::Structure
            && kind != SourceCompletionKind::Enumeration) {
            continue;
        }
        result.push_back({name, kind, {}});
    }
    if (!types_only) {
        // Standalone expression, statement and declaration starters. Operators
        // such as `as`, `in` and `using` are not unqualified name candidates.
        for (const auto name :
             {"async",  "await",   "break",   "cancel", "const",   "continue", "enum",
              "export", "false",   "fn",      "for",    "if",      "import",   "let",
              "match",  "nullptr", "private", "return", "rethrow", "struct",   "class",
              "test",   "throw",   "true",    "try",    "var",     "while"}) {
            result.push_back({name, SourceCompletionKind::Keyword, {}});
        }
    }
    auto seen = std::set<std::string, std::less<>>();
    for (const auto& binding : catalog.cpp_imports(module_id)) {
        if (binding.opens_namespace || binding.components.empty()) {
            continue;
        }
        const auto name = draft.source_slice_copy(module_id, binding.origin);
        // Explicit selections do not reveal whether a native name denotes a
        // type, function or namespace. Keep the same neutral kind in type sites.
        if (!std::holds_alternative<OrdinaryIdentifier>(classify_identifier(name))
            || !source_name_available(draft, module_id, name)
            || !catalog.lookup(module_id, name).empty()
            || !seen.insert(name).second) {
            continue;
        }
        auto path = std::string("C++ import ");
        for (const auto& component : binding.components) {
            path += "::" + component;
        }
        result.push_back({name, SourceCompletionKind::Value, std::move(path)});
    }
    return result;
}

auto source_intrinsic_members(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::vector<SourceCompletionCandidate> {
    auto result = std::vector<SourceCompletionCandidate>();
    // The operation selectors remain authoritative for receiver shape and arity.
    // A shared spelling may be available through more than one operation family.
    constexpr auto methods = std::to_array<std::pair<std::string_view, std::size_t>>(
        {{"length", 0},
         {"is_empty", 0},
         {"slice", 0},
         {"slice", 2},
         {"push", 1},
         {"remove", 1},
         {"capacity", 0},
         {"reserve", 1},
         {"clear", 0},
         {"byte_length", 0},
         {"as_str", 0},
         {"append", 1},
         {"is_finite", 0},
         {"sqrt", 0},
         {"abs", 0},
         {"min", 1},
         {"max", 1},
         {"clamp", 2}}
    );
    for (const auto& [name, arity] : methods) {
        const auto slice = decide_slice_method(draft, type, name, arity);
        const auto vector = decide_vector_method(draft, type, name, arity);
        const auto text = decide_text_method(draft, type, name, arity);
        const auto number = decide_float_method(draft, type, name, arity);
        if ((slice && *slice) || (vector && *vector) || (text && *text) || (number && *number)) {
            if (std::ranges::none_of(result, [&](const auto& candidate) {
                    return candidate.name == name;
                })) {
                result.push_back({std::string(name), SourceCompletionKind::Method, {}});
            }
        }
    }
    if (const auto* concrete = std::get_if<TypeID>(&type)) {
        const auto canonical = draft.type_copy(*concrete);
        if (const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value)) {
            if (builtin->kind == BuiltinType::Str || builtin->kind == BuiltinType::String) {
                result.push_back({"bytes", SourceCompletionKind::Field, "[u8]"});
                result.push_back({"chars", SourceCompletionKind::Field, "str.chars"});
            }
            if (builtin->kind == BuiltinType::String) {
                result.push_back({"append_format", SourceCompletionKind::Method, {}});
            }
        }
    }
    return result;
}
