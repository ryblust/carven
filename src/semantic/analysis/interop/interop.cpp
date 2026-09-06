module carven:semantic.analysis.interop.impl;

import :diagnostics.builder;
import :diagnostics.diagnostic;
import :semantic.analysis.interop;
import :semantic.analysis.program;
import :semantic.semir.decl;
import :semantic.semir.type;
import :source.cpp.identifier;
import :support.invariant;
import std;

namespace {

auto is_cpp_scalar_type(BuiltinType type) noexcept -> bool {
    using enum BuiltinType;
    switch (type) {
        case Bool:
        case Char:
        case I8:
        case I16:
        case I32:
        case I64:
        case U8:
        case U16:
        case U32:
        case U64:
        case Isize:
        case Usize:
        case F32:
        case F64:          return true;
        case Void:
        case Str:
        case StrBytesView:
        case StrCharsView:
        case EntryArgs:    return false;
    }
    std::unreachable();
}

auto builtin_type(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::optional<BuiltinType> {
    const auto* concrete = std::get_if<TypeID>(&type);
    if (concrete == nullptr) {
        return std::nullopt;
    }
    const auto resolved = draft.type_copy(*concrete);
    const auto* builtin = std::get_if<BuiltinTypeValue>(&resolved.value);
    return builtin == nullptr ? std::nullopt : std::optional(builtin->kind);
}

auto is_cpp_parameter_type(const ProgramDraft& draft, ConstructionTypeRef type) noexcept -> bool {
    const auto builtin = builtin_type(draft, type);
    return builtin.has_value() && is_cpp_scalar_type(*builtin);
}

auto is_cpp_result_type(const ProgramDraft& draft, ConstructionTypeRef type) noexcept -> bool {
    const auto builtin = builtin_type(draft, type);
    return builtin.has_value() && (*builtin == BuiltinType::Void || is_cpp_scalar_type(*builtin));
}

auto is_unrepresentable_cpp_provider_name(std::string_view name) noexcept -> bool {
    return name == "main" || name == "std" || name == "carven";
}

struct CppExportPath final {
    std::vector<std::string> path;
    ProgramOriginID origin;
};

auto is_strict_prefix(
    std::span<const std::string> prefix,
    std::span<const std::string> value
) noexcept -> bool {
    return prefix.size() < value.size() && std::ranges::equal(prefix, value.first(prefix.size()));
}

auto source_id(const ProgramDraft& draft, ProgramModuleID module_id) noexcept -> SourceID {
    return draft.syntax_tree(module_id).view().source_id();
}

} // namespace

auto validate_cpp_boundary_declaration(
    ProgramDraft& draft,
    ProgramModuleID module_id,
    ASTView syntax,
    const ASTFunctionDecl& function,
    std::span<const ConstructionCallableParameter> parameters,
    ConstructionTypeRef result
) noexcept -> AnalysisResult<void> {
    const auto cpp_import = std::holds_alternative<ASTCppImportForm>(function.implementation);
    const auto cpp_export = function.cpp_export.has_value();
    if (!cpp_import && !cpp_export) {
        return {};
    }

    auto failure = std::optional<AnalysisFailure>();
    const auto diagnose = [&](Span span, std::string message, DiagnosticCode code) noexcept {
        failure = draft.diagnostics().error(DiagnosticBuilder(code, std::move(message))
                                                .primary(locate(source_id(draft, module_id), span))
                                                .build());
    };
    if (function.throw_clause.has_value()) {
        diagnose(
            function.throw_clause->span,
            "a C++ boundary function cannot declare failures",
            DiagnosticCode::CppBoundary
        );
    }
    if (parameters.size() != function.parameters.size()) {
        invariant_violation("C++ boundary syntax and callable parameters are not aligned");
    }
    for (const auto [index, parameter] : std::views::enumerate(parameters)) {
        const auto& source = function.parameters[index];
        if (parameter.access != AccessMode::Read) {
            diagnose(
                source.access.marker.value_or(source.span),
                "C++ boundary parameters must use Read access",
                DiagnosticCode::CppBoundary
            );
        }
        if (!is_cpp_parameter_type(draft, parameter.type)) {
            diagnose(
                source.span,
                "C++ boundary parameters require a supported scalar type",
                DiagnosticCode::CppBoundaryType
            );
        }
    }
    if (!is_cpp_result_type(draft, result)) {
        diagnose(
            function.result_type.has_value() ? syntax.type(*function.result_type).span
                                             : function.name_span,
            "a C++ boundary result requires a supported scalar type or void",
            DiagnosticCode::CppBoundaryType
        );
    }
    const auto name = draft.source_slice_copy(module_id, function.name_span);
    if (!is_supported_cpp_identifier(name)) {
        diagnose(
            function.name_span,
            cpp_import ? "C++ provider name must be a supported C++ identifier"
                       : "C++ API function name must be a supported C++ identifier",
            DiagnosticCode::CppIdentifier
        );
    } else if (cpp_import && is_unrepresentable_cpp_provider_name(name)) {
        diagnose(
            function.name_span,
            "this name cannot be represented as a global C++ provider",
            DiagnosticCode::CppIdentifier
        );
    }
    return failure.has_value() ? AnalysisResult<void>(std::unexpected(*failure))
                               : AnalysisResult<void>();
}

auto diagnose_cpp_api_surface(ProgramDraft& draft, AnalysisCatalogView catalog) noexcept
    -> AnalysisResult<void> {
    auto failure = std::optional<AnalysisFailure>();
    const auto diagnose = [&](Diagnostic diagnostic) noexcept {
        const auto current = draft.diagnostics().error(std::move(diagnostic));
        if (!failure.has_value()) {
            failure = current;
        }
    };
    auto functions = std::vector<CppExportPath>();
    auto exported_module_origins =
        std::vector<std::optional<ProgramOriginID>>(draft.module_count());
    for (const auto& symbol : catalog.symbols()) {
        const auto* form = std::get_if<CatalogFunctionForm>(&symbol.form);
        if (form == nullptr) {
            continue;
        }
        const auto function = draft.function_declaration_copy(form->function);
        if (!function.cpp_export_origin.has_value()) {
            continue;
        }
        if (symbol.module_id.index() >= exported_module_origins.size()) {
            invariant_violation("C++ API function references an unknown module");
        }
        if (!exported_module_origins[symbol.module_id.index()].has_value()) {
            exported_module_origins[symbol.module_id.index()] = function.cpp_export_origin;
        }
        auto path = draft.module_path_copy(symbol.module_id).components()
            | std::views::transform([](std::string_view value) { return std::string(value); })
            | std::ranges::to<std::vector>();
        path.push_back(draft.spelling_copy(function.name));
        functions.push_back({
            .path = std::move(path),
            .origin = *function.cpp_export_origin,
        });
    }
    for (auto index = 0uz; index < exported_module_origins.size(); ++index) {
        const auto origin = exported_module_origins[index];
        if (!origin.has_value()) {
            continue;
        }
        const auto module_id = draft.provenance_module_at(index);
        const auto module_path = draft.module_path_copy(module_id);
        for (const auto& component : module_path.components()) {
            if (is_supported_cpp_identifier(component)) {
                continue;
            }
            diagnose(
                DiagnosticBuilder(
                    DiagnosticCode::CppIdentifier,
                    std::format(
                        "module-path component '{}' is not a supported C++ namespace identifier",
                        component
                    )
                )
                    .primary(draft.source_span(*origin))
                    .build()
            );
        }
    }
    for (auto left = 0uz; left < functions.size(); ++left) {
        for (auto right = left + 1; right < functions.size(); ++right) {
            const auto collision = is_strict_prefix(functions[left].path, functions[right].path)
                || is_strict_prefix(functions[right].path, functions[left].path);
            if (!collision) {
                continue;
            }
            diagnose(DiagnosticBuilder(
                         DiagnosticCode::CppAPIPathCollision,
                         "a C++ API function name conflicts with another API namespace path"
            )
                         .primary(draft.source_span(functions[right].origin))
                         .related(
                             draft.source_span(functions[left].origin),
                             "conflicting C++ API declaration"
                         )
                         .build());
        }
    }
    return failure.has_value() ? AnalysisResult<void>(std::unexpected(*failure))
                               : AnalysisResult<void>();
}
