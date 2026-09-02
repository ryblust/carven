module carven:semantic.analysis.interop.impl;

import :diagnostics.builder;
import :semantic.analysis.interop;
import :semantic.hir.decl;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :source.cpp.identifier;
import :support.invariant;
import std;

namespace {

auto is_cpp_scalar_type(HIRBuiltinType type) noexcept -> bool {
    using enum HIRBuiltinType;
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

auto is_cpp_parameter_type(SemanticDraftView semantic, HIRTypeID type) noexcept -> bool {
    const auto& value = semantic.type(type).value;
    if (std::holds_alternative<HIRErrorTypeValue>(value)) {
        return true;
    }
    const auto* builtin = std::get_if<HIRBuiltinTypeValue>(&value);
    return builtin != nullptr && is_cpp_scalar_type(builtin->kind);
}

auto is_cpp_result_type(SemanticDraftView semantic, HIRTypeID type) noexcept -> bool {
    const auto& value = semantic.type(type).value;
    if (std::holds_alternative<HIRErrorTypeValue>(value)) {
        return true;
    }
    const auto* builtin = std::get_if<HIRBuiltinTypeValue>(&value);
    return builtin != nullptr
        && (builtin->kind == HIRBuiltinType::Void || is_cpp_scalar_type(builtin->kind));
}

auto is_unrepresentable_cpp_provider_name(std::string_view name) noexcept -> bool {
    return name == "main" || name == "std" || name == "carven";
}

struct CppExportPath final {
    std::vector<std::string_view> path;
    ProgramOriginID origin;
};

auto is_strict_prefix(
    std::span<const std::string_view> prefix,
    std::span<const std::string_view> value
) noexcept -> bool {
    return prefix.size() < value.size() && std::ranges::equal(prefix, value.first(prefix.size()));
}

} // namespace

auto diagnose_cpp_boundary_declaration(
    ModuleAnalysis& module_analysis,
    const ASTFunctionDecl& function,
    std::span<const HIRFunctionParameterType> parameters,
    HIRTypeID result
) noexcept -> void {
    const auto cpp_import = std::holds_alternative<ASTCppImportForm>(function.implementation);
    const auto cpp_export = function.cpp_export.has_value();
    if (!cpp_import && !cpp_export) {
        return;
    }
    if (function.throw_clause.has_value()) {
        module_analysis.emit(
            function.throw_clause->span,
            "a C++ boundary function cannot declare failures",
            DiagnosticCode::CppBoundary
        );
    }
    if (parameters.size() != function.parameters.size()) {
        invariant_violation("C++ boundary syntax and callable parameters are not aligned");
    }
    for (const auto [index, parameter] : std::views::enumerate(parameters)) {
        const auto& syntax = function.parameters[index];
        if (parameter.access != HIRAccessMode::Read) {
            module_analysis.emit(
                syntax.access.marker.value_or(syntax.span),
                "C++ boundary parameters must use Read access",
                DiagnosticCode::CppBoundary
            );
        }
        if (!is_cpp_parameter_type(module_analysis.builder(), parameter.type)) {
            module_analysis.emit(
                syntax.span,
                "C++ boundary parameters require a supported scalar type",
                DiagnosticCode::CppBoundaryType
            );
        }
    }
    if (!is_cpp_result_type(module_analysis.builder(), result)) {
        const auto span = function.result_type.has_value()
            ? module_analysis.syntax().type(*function.result_type).span
            : function.name_span;
        module_analysis.emit(
            span,
            "a C++ boundary result requires a supported scalar type or void",
            DiagnosticCode::CppBoundaryType
        );
    }
    const auto name = module_analysis.spelling(function.name_span);
    if (!is_supported_cpp_identifier(name)) {
        module_analysis.emit(
            function.name_span,
            cpp_import ? "C++ provider name must be a supported C++ identifier"
                       : "C++ API function name must be a supported C++ identifier",
            DiagnosticCode::CppIdentifier
        );
    } else if (cpp_import && is_unrepresentable_cpp_provider_name(name)) {
        module_analysis.emit(
            function.name_span,
            "this name cannot be represented as a global C++ provider",
            DiagnosticCode::CppIdentifier
        );
    }
}

auto diagnose_cpp_api_surface(SemanticDraftView semantic, DiagnosticSink& diagnostics) noexcept
    -> void {
    auto functions = std::vector<CppExportPath> {};
    auto exported_module_origins =
        std::vector<std::optional<ProgramOriginID>>(semantic.modules().size());
    for (const auto& function : semantic.functions()) {
        if (!function.cpp_export_form_origin.has_value()) {
            continue;
        }
        const auto module_id = semantic.symbol(function.symbol).module_id;
        if (!module_id.has_value()) {
            invariant_violation("C++ API function has no owning module");
        }
        if (!exported_module_origins[module_id->index()].has_value()) {
            exported_module_origins[module_id->index()] = function.cpp_export_form_origin;
        }
        auto path = std::vector<std::string_view> {};
        const auto& module = semantic.provenance().module_record(*module_id);
        for (const auto& component : module.path.components()) {
            path.push_back(component);
        }
        path.push_back(semantic.provenance().spelling(function.name));
        functions.push_back({.path = std::move(path), .origin = *function.cpp_export_form_origin});
    }
    for (auto index = 0uz; index < exported_module_origins.size(); ++index) {
        const auto origin = exported_module_origins[index];
        if (!origin.has_value()) {
            continue;
        }
        const auto module_id = ProgramModuleID::from_index(static_cast<std::uint32_t>(index));
        const auto& module = semantic.provenance().module_record(module_id);
        for (const auto& component : module.path.components()) {
            if (is_supported_cpp_identifier(component)) {
                continue;
            }
            diagnostics.emit(
                DiagnosticBuilder(
                    DiagnosticCode::CppIdentifier,
                    std::format(
                        "module-path component '{}' is not a supported C++ namespace identifier",
                        component
                    )
                )
                    .primary(semantic.provenance().source_span(*origin))
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
            diagnostics.emit(
                DiagnosticBuilder(
                    DiagnosticCode::CppAPIPathCollision,
                    "a C++ API function name conflicts with another API namespace path"
                )
                    .primary(semantic.provenance().source_span(functions[right].origin))
                    .related(
                        semantic.provenance().source_span(functions[left].origin),
                        "conflicting C++ API declaration"
                    )
                    .build()
            );
        }
    }
}
