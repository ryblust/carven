module carven:frontend.program.dependencies.impl;

import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.imports;
import :frontend.ast.literal;
import :frontend.ast.type;
import :frontend.literal;
import :frontend.program.dependencies;
import :frontend.program.parse;
import :source.standard;
import :support.function_ref;
import std;

auto source_dependency_closure(
    std::span<const CanonicalModulePath> roots,
    FunctionRef<std::shared_ptr<const ParsedDocument>(const CanonicalModulePath&) noexcept> load
) noexcept -> std::vector<CanonicalModulePath> {
    auto pending = std::vector<CanonicalModulePath>(roots.begin(), roots.end());
    auto visited = std::set<CanonicalModulePath>();
    auto selected = std::vector<CanonicalModulePath>();
    for (auto index = 0uz; index < pending.size(); ++index) {
        const auto module_path = pending[index];
        if (!visited.insert(module_path).second) {
            continue;
        }
        selected.push_back(module_path);
        const auto document = load(module_path);
        const auto syntax = document ? document->recovered_syntax() : std::optional<ASTView>();
        if (syntax) {
            pending.append_range(
                source_dependencies(*syntax, module_path, document->source().text)
            );
        }
    }
    return selected;
}

auto source_dependencies(
    ASTView syntax,
    const CanonicalModulePath& module_path,
    std::string_view source
) noexcept -> std::vector<CanonicalModulePath> {
    auto paths = std::set<std::string>();
    auto type_names = std::map<std::string, ProvidedExport, std::less<>>();
    for (const auto& entry : provided_exports()) {
        if (entry.default_available && standard_associated_receiver(entry.identity)) {
            type_names.emplace(entry.name, entry.identity);
        }
    }
    const auto add_default = [&](std::string_view name) noexcept {
        for (const auto& declaration : standard_declarations()) {
            if (declaration.default_available && name == declaration.name) {
                paths.emplace(declaration.module_path);
            }
        }
    };
    for (const auto& import_decl : syntax.module_imports()) {
        const auto path = resolve_import_path(source, module_path, import_decl.module_reference);
        if (!path) {
            continue;
        }
        const auto provided = find_provided_module(path->value());
        auto needs_source = !provided.has_value();
        const auto select = [&](const ASTImportItem& item) noexcept {
            const auto name = slice(source, item.name_span);
            const auto entry =
                std::ranges::find_if(provided_exports(), [&](const auto& entry) noexcept {
                    return provided
                        && entry.provider == *provided
                        && entry.name == name
                        && !entry.module_local;
                });
            if (entry == provided_exports().end()) {
                needs_source = true;
            } else if (entry->identity == ProvidedExport::Timeout) {
                needs_source = true;
            } else if (standard_associated_receiver(entry->identity)) {
                type_names[std::string(slice(source, item.local_name_span()))] = entry->identity;
            }
        };
        import_decl.selection.value.visit([&](const auto& selection) noexcept {
            using Selection = std::remove_cvref_t<decltype(selection)>;
            if constexpr (std::same_as<Selection, ASTSingleImport>) {
                select(selection.item);
            } else if constexpr (std::same_as<Selection, ASTImportList>) {
                for (const auto& item : selection.names) {
                    select(item);
                }
            } else {
                needs_source = true;
            }
        });
        if (needs_source) {
            paths.emplace(path->value());
        }
    }
    for (const auto& type : syntax.types()) {
        if (const auto* name = std::get_if<ASTNamedType>(&type.value);
            name && !name->components.empty()) {
            add_default(slice(source, name->components.front().name_span));
        }
    }
    for (const auto& expression : syntax.expressions()) {
        if (const auto* name = std::get_if<ASTNameExpr>(&expression.value)) {
            add_default(slice(source, name->name_span));
        }
        if (const auto* literal = std::get_if<ASTLiteral>(&expression.value)) {
            if (const auto* integer = std::get_if<IntegerLiteralValue>(&literal->value);
                integer && duration_literal_scale(integer->suffix)) {
                paths.emplace(standard_declaration(StandardDeclaration::Duration).module_path);
            }
        }
        const auto* member = std::get_if<ASTMemberExpr>(&expression.value);
        if (!member || member->op != ASTMemberOperator::Scope) {
            continue;
        }
        auto operand = member->operand_id;
        while (const auto* group = std::get_if<ASTGroupExpr>(&syntax.expression(operand).value)) {
            operand = group->expression;
        }
        const auto* type = std::get_if<ASTNameExpr>(&syntax.expression(operand).value);
        if (!type) {
            continue;
        }
        const auto found = type_names.find(slice(source, type->name_span));
        if (found == type_names.end()) {
            continue;
        }
        const auto receiver = standard_associated_receiver(found->second);
        if (const auto* entry = receiver
                ? find_standard_associated(*receiver, slice(source, member->name_span))
                : nullptr) {
            paths.emplace(standard_declaration(entry->declaration).module_path);
        }
    }
    auto result = std::vector<CanonicalModulePath>();
    for (const auto& path : paths) {
        result.push_back(*CanonicalModulePath::from_value(path));
    }
    return result;
}
