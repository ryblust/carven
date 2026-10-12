module carven:frontend.program.dependencies.impl;

import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.literal;
import :frontend.program.dependencies;
import :frontend.program.parse;
import :source.standard;
import std;

auto source_dependencies(
    ASTView syntax,
    const CanonicalModulePath& module,
    std::string_view source
) noexcept -> std::vector<CanonicalModulePath> {
    auto paths = std::set<std::string>();
    const auto require_record = [&](StandardRecord record) noexcept {
        paths.emplace(standard_record_key(record).module);
    };
    for (const auto& import : syntax.module_imports()) {
        const auto path = resolve_import_path(source, module, import.module_reference);
        if (!path) {
            continue;
        }
        paths.emplace(path->value());
        const auto provided = find_provided_module(path->value());
        if (!provided) {
            continue;
        }
        for (const auto& entry : provided_exports()) {
            if (entry.module != *provided) {
                continue;
            }
            const auto selects = [&](const ASTImportItem& item) noexcept {
                return slice(source, item.name_span) == entry.name;
            };
            const auto selected = import.selection.value.visit([&](const auto& selection) noexcept {
                using Selection = std::remove_cvref_t<decltype(selection)>;
                if constexpr (std::same_as<Selection, ASTSingleImport>) {
                    return selects(selection.item);
                } else if constexpr (std::same_as<Selection, ASTImportList>) {
                    return std::ranges::any_of(selection.names, selects);
                } else {
                    return true;
                }
            });
            if (selected) {
                if (const auto source_export = provided_source_key(entry.identity)) {
                    paths.emplace(source_export->module);
                }
                for (const auto record : provided_contract_dependencies(entry.identity)) {
                    require_record(record);
                }
            }
        }
    }
    for (const auto& expression : syntax.expressions()) {
        if (const auto* name = std::get_if<ASTNameExpr>(&expression.value)) {
            for (const auto& entry : provided_exports()) {
                if (entry.default_available && entry.name == slice(source, name->name_span)) {
                    if (const auto source_export = provided_source_key(entry.identity)) {
                        paths.emplace(source_export->module);
                    }
                }
            }
        }
        const auto* literal = std::get_if<ASTLiteral>(&expression.value);
        const auto* integer = literal ? std::get_if<IntegerLiteralValue>(&literal->value) : nullptr;
        if (integer && duration_literal_scale(integer->suffix)) {
            require_record(StandardRecord::Duration);
        }
    }
    auto result = std::vector<CanonicalModulePath>();
    for (const auto& path : paths) {
        result.push_back(*CanonicalModulePath::from_value(path));
    }
    return result;
}
