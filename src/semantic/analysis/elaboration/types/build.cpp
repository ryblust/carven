module carven:semantic.analysis.elaboration.types.build.impl;

import :frontend.ast.decl;
import :frontend.ast.type;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.decl;
import :semantic.analysis.elaboration.expr;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.elaboration.types;
import :semantic.analysis.failures;
import :semantic.hir.access;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.visit;
import std;

namespace {

auto nominal_type(const ModuleAnalysis& module_analysis, SymbolID symbol) noexcept
    -> std::optional<HIRTypeValue> {
    const auto* catalog_symbol = module_analysis.catalog().symbol(symbol);
    if (catalog_symbol == nullptr) {
        return std::nullopt;
    }
    if (const auto* structure = std::get_if<CatalogStructForm>(&catalog_symbol->form)) {
        return HIRStructTypeValue {.structure = structure->structure};
    }
    if (const auto* enumeration = std::get_if<CatalogEnumForm>(&catalog_symbol->form)) {
        return HIREnumTypeValue {.enumeration = enumeration->enumeration};
    }
    return std::nullopt;
}

} // namespace

auto access_mode(ASTAccessSyntax access) noexcept -> HIRAccessMode {
    switch (access.mode) {
        case ASTAccessMode::Read:  return HIRAccessMode::Read;
        case ASTAccessMode::Write: return HIRAccessMode::Write;
        case ASTAccessMode::Take:  return HIRAccessMode::Take;
    }
    std::unreachable();
}

auto normalized_failures(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTThrowClause& clause
) noexcept -> std::vector<HIRTypeID> {
    const auto ast = module_analysis.syntax();
    const auto& builder = module_analysis.builder();
    auto failures = std::vector<HIRTypeID>();
    auto first_seen = std::flat_map<HIRTypeID, Span>();
    failures.reserve(clause.failures.size());
    for (const auto source_failure : clause.failures) {
        const auto failure = build_type(module_analysis, scopes, control, source_failure);
        const auto& value = builder.type(failure).value;
        if (!std::holds_alternative<HIRStructTypeValue>(value)
            && !std::holds_alternative<HIREnumTypeValue>(value)) {
            module_analysis.emit(
                ast.type(source_failure).span,
                "failure clause entries must be copyable nominal struct or enum types",
                DiagnosticCode::EffectThrowType
            );
            continue;
        }
        const auto found = first_seen.find(failure);
        if (found != first_seen.end()) {
            module_analysis.emit(
                ast.type(source_failure).span,
                "failure clause contains the same nominal type more than once",
                DiagnosticCode::EffectThrowDuplicate
            );
            continue;
        }
        first_seen.emplace(failure, ast.type(source_failure).span);
        failures.push_back(failure);
    }
    return normalize_failure_members(std::move(failures));
}

auto array_extent(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    ASTExprID expression_id
) noexcept -> std::optional<std::uint64_t> {
    auto& builder = module_analysis.builder();
    const auto proof = builder.begin_expression_proof();
    const auto failure_checkpoint = module_analysis.error_checkpoint();
    const auto extent = build_expression(module_analysis, scopes, control, expression_id);
    const auto constant = constant_integer(module_analysis, extent);
    builder.finish_expression_proof(proof);

    const auto span = module_analysis.syntax().expression(expression_id).span;
    if (!constant.has_value()) {
        if (!module_analysis.error_observed_since(failure_checkpoint)) {
            module_analysis.emit(
                span,
                "array extent must be a constant integer",
                DiagnosticCode::ConstArrayExtent
            );
        }
        return std::nullopt;
    }
    if (constant->negative()) {
        module_analysis.emit(
            span,
            "array extent cannot be negative",
            DiagnosticCode::ConstNegativeArrayExtent
        );
        return std::nullopt;
    }
    return constant->magnitude();
}

auto construction_type(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    const ASTConstructionType& source_type
) noexcept -> HIRTypeID {
    auto& builder = module_analysis.builder();
    return std::visit(
        Overloaded {
            [&](const ASTNamedType& named) noexcept -> HIRTypeID {
                if (named.components.size() == 1) {
                    const auto name = module_analysis.spelling(named.components.front().name_span);
                    static constexpr auto builtins = std::array {
                        std::pair {std::string_view("bool"), HIRBuiltinType::Bool},
                        std::pair {std::string_view("char"), HIRBuiltinType::Char},
                        std::pair {std::string_view("str"), HIRBuiltinType::Str},
                        std::pair {std::string_view("i8"), HIRBuiltinType::I8},
                        std::pair {std::string_view("i16"), HIRBuiltinType::I16},
                        std::pair {std::string_view("i32"), HIRBuiltinType::I32},
                        std::pair {std::string_view("i64"), HIRBuiltinType::I64},
                        std::pair {std::string_view("u8"), HIRBuiltinType::U8},
                        std::pair {std::string_view("u16"), HIRBuiltinType::U16},
                        std::pair {std::string_view("u32"), HIRBuiltinType::U32},
                        std::pair {std::string_view("u64"), HIRBuiltinType::U64},
                        std::pair {std::string_view("isize"), HIRBuiltinType::Isize},
                        std::pair {std::string_view("usize"), HIRBuiltinType::Usize},
                        std::pair {std::string_view("f32"), HIRBuiltinType::F32},
                        std::pair {std::string_view("f64"), HIRBuiltinType::F64},
                        std::pair {std::string_view("void"), HIRBuiltinType::Void},
                    };
                    for (const auto& [builtin_name, builtin_kind] : builtins) {
                        if (name == builtin_name) {
                            return builtin(module_analysis, source_type.span, builtin_kind);
                        }
                    }
                    const auto symbol_resolution = symbol_for(
                        module_analysis,
                        scopes,
                        name,
                        named.components.front().name_span
                    );
                    if (symbol_resolution.has_value()) {
                        const auto symbol = symbol_resolution.value();
                        const auto type = nominal_type(module_analysis, symbol);
                        if (!type.has_value()) {
                            return module_analysis.diagnose_and_recover_type(
                                source_type.span,
                                std::format("'{}' does not name a type", name),
                                DiagnosticCode::TypeUnresolved
                            );
                        }
                        return builder.intern_type({.value = *type});
                    }
                    if (symbol_resolution.error() != LookupError::Missing) {
                        return error_type(module_analysis, source_type.span);
                    }
                }
                return module_analysis.diagnose_and_recover_type(
                    source_type.span,
                    "unresolved construction type",
                    DiagnosticCode::TypeUnresolved
                );
            },
            [&](const ASTFunctionType& function) noexcept -> HIRTypeID {
                auto parameters = std::vector<HIRFunctionParameterType>();
                for (const auto& parameter : function.parameters) {
                    parameters.push_back({
                        .access = access_mode(parameter.access),
                        .type = build_type(module_analysis, scopes, control, parameter.type),
                    });
                }
                return builder.intern_function_ref_type(
                    std::move(parameters),
                    build_type(module_analysis, scopes, control, function.result_type),
                    function.throw_clause.has_value() ? normalized_failures(
                                                            module_analysis,
                                                            scopes,
                                                            control,
                                                            *function.throw_clause
                                                        )
                                                      : std::vector<HIRTypeID>()
                );
            },
        },
        source_type.value
    );
}

auto build_type(
    ModuleAnalysis& module_analysis,
    ScopeStack& scopes,
    BodyControl control,
    ASTTypeID id
) noexcept -> HIRTypeID {
    const auto ast = module_analysis.syntax();
    auto& builder = module_analysis.builder();
    const auto& value = ast.type(id);
    return std::visit(
        Overloaded {
            [&](const ASTNamedType& named) noexcept -> HIRTypeID {
                if (named.components.size() == 1) {
                    const auto name = module_analysis.spelling(named.components.front().name_span);
                    static constexpr auto builtins = std::array {
                        std::pair {std::string_view("bool"), HIRBuiltinType::Bool},
                        std::pair {std::string_view("char"), HIRBuiltinType::Char},
                        std::pair {std::string_view("str"), HIRBuiltinType::Str},
                        std::pair {std::string_view("i8"), HIRBuiltinType::I8},
                        std::pair {std::string_view("i16"), HIRBuiltinType::I16},
                        std::pair {std::string_view("i32"), HIRBuiltinType::I32},
                        std::pair {std::string_view("i64"), HIRBuiltinType::I64},
                        std::pair {std::string_view("u8"), HIRBuiltinType::U8},
                        std::pair {std::string_view("u16"), HIRBuiltinType::U16},
                        std::pair {std::string_view("u32"), HIRBuiltinType::U32},
                        std::pair {std::string_view("u64"), HIRBuiltinType::U64},
                        std::pair {std::string_view("isize"), HIRBuiltinType::Isize},
                        std::pair {std::string_view("usize"), HIRBuiltinType::Usize},
                        std::pair {std::string_view("f32"), HIRBuiltinType::F32},
                        std::pair {std::string_view("f64"), HIRBuiltinType::F64},
                        std::pair {std::string_view("void"), HIRBuiltinType::Void},
                    };
                    for (const auto& [builtin_name, builtin_kind] : builtins) {
                        if (name == builtin_name) {
                            return builtin(module_analysis, value.span, builtin_kind);
                        }
                    }
                    const auto symbol_resolution = symbol_for(
                        module_analysis,
                        scopes,
                        name,
                        named.components.front().name_span
                    );
                    if (symbol_resolution.has_value()) {
                        const auto symbol = symbol_resolution.value();
                        const auto type = nominal_type(module_analysis, symbol);
                        if (!type.has_value()) {
                            return module_analysis.diagnose_and_recover_type(
                                value.span,
                                std::format("'{}' does not name a type", name),
                                DiagnosticCode::TypeUnresolved
                            );
                        }
                        return builder.intern_type({.value = *type});
                    }
                    if (symbol_resolution.error() != LookupError::Missing) {
                        return error_type(module_analysis, value.span);
                    }
                }
                return module_analysis.diagnose_and_recover_type(
                    value.span,
                    std::format("unresolved type name '{}'", module_analysis.spelling(value.span)),
                    DiagnosticCode::TypeUnresolved
                );
            },
            [&](const ASTArrayType& array) noexcept -> HIRTypeID {
                const auto extent = array_extent(module_analysis, scopes, control, array.extent);
                if (!extent.has_value()) {
                    return error_type(module_analysis, value.span);
                }
                const auto element = require_value_type(
                    module_analysis,
                    build_type(module_analysis, scopes, control, array.element_type),
                    ast.type(array.element_type).span,
                    ValueTypeRole::ArrayElement
                );
                return builder.intern_type({
                    .value = HIRArrayTypeValue {
                        .element_type_id = element,
                        .extent = *extent,
                    },
                });
            },
            [&](const ASTFunctionType& function) noexcept -> HIRTypeID {
                auto parameters = std::vector<HIRFunctionParameterType>();
                for (const auto& parameter : function.parameters) {
                    const auto parameter_type =
                        build_type(module_analysis, scopes, control, parameter.type);
                    parameters.push_back({
                        .access = access_mode(parameter.access),
                        .type = require_value_type(
                            module_analysis,
                            parameter_type,
                            ast.type(parameter.type).span,
                            ValueTypeRole::FunctionParameter
                        ),
                    });
                }
                return builder.intern_function_ref_type(
                    std::move(parameters),
                    build_type(module_analysis, scopes, control, function.result_type),
                    function.throw_clause.has_value() ? normalized_failures(
                                                            module_analysis,
                                                            scopes,
                                                            control,
                                                            *function.throw_clause
                                                        )
                                                      : std::vector<HIRTypeID>()
                );
            },
        },
        value.value
    );
}

auto error_type(ModuleAnalysis& module_analysis, Span span) noexcept -> HIRTypeID {
    auto& builder = module_analysis.builder();
    static_cast<void>(span);
    return builder.intern_type({.value = HIRErrorTypeValue {}});
}

auto builtin(ModuleAnalysis& module_analysis, Span span, HIRBuiltinType kind) noexcept
    -> HIRTypeID {
    auto& builder = module_analysis.builder();
    static_cast<void>(span);
    return builder.intern_type({.value = HIRBuiltinTypeValue {.kind = kind}});
}
