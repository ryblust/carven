module carven:semantic.analysis.elaboration.declarations.constants.impl;

import :diagnostics.builder;
import :frontend.ast.decl;
import :semantic.analysis.elaboration.body;
import :semantic.analysis.elaboration.declarations;
import :semantic.analysis.elaboration.expressions;
import :semantic.analysis.elaboration.module_analysis;
import :semantic.analysis.elaboration.scopes;
import :semantic.analysis.elaboration.types;
import :semantic.hir.constant;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.type;
import :semantic.visibility;
import :support.invariant;
import std;

auto elaborate_module_constant(
    ModuleAnalysis& module_analysis,
    SymbolID symbol,
    const ASTConstantDecl& declaration
) noexcept -> bool {
    auto& builder = module_analysis.builder();
    if (builder.symbol_constant(symbol).has_value()) {
        return true;
    }

    auto scopes = ScopeStack();
    const auto* catalog_symbol = module_analysis.catalog().symbol(symbol);
    if (catalog_symbol == nullptr
        || !std::holds_alternative<CatalogConstantForm>(catalog_symbol->form)) {
        invariant_violation("constant syntax does not match its catalog identity");
    }
    const auto exported = catalog_symbol->visibility == DeclarationVisibility::Compilation;
    if (exported && !declaration.type.has_value()) {
        module_analysis.emit(
            declaration.name_span,
            "exported constant requires an explicit declared type",
            DiagnosticCode::ConstExportedType
        );
    }

    const auto declared_type = declaration.type.has_value()
                                 ? std::optional<HIRTypeID> {
                                       build_type(module_analysis,
                                            scopes,
                                            root_body_control(),
                                            *declaration.type),
                                   }
                                 : std::nullopt;
    const auto failure_checkpoint = module_analysis.error_checkpoint();
    if (constant_initializer_requires_body_scope(
            module_analysis.syntax(),
            declaration.initializer
        )) {
        module_analysis.emit(
            module_analysis.syntax().expression(declaration.initializer).span,
            "constant initializer is not a supported compile-time value",
            DiagnosticCode::ConstInitializer
        );
        return false;
    }
    const auto value = declared_type.has_value()
        ? build_expected_expression(
              module_analysis,
              scopes,
              root_body_control(),
              declaration.initializer,
              *declared_type,
              ExpectedExpressionUsage::Binding
          )
        : build_expression(module_analysis, scopes, root_body_control(), declaration.initializer);
    const auto selected_type = require_value_type(
        module_analysis,
        declared_type.value_or(expression_type(module_analysis, value)),
        declaration.type.has_value()
            ? module_analysis.syntax().type(*declaration.type).span
            : module_analysis.syntax().expression(declaration.initializer).span,
        ValueTypeRole::Constant
    );
    set_symbol_type(module_analysis, symbol, selected_type);
    builder.define_symbol_binding(symbol, SemanticBindingRole::CompileTime, false);

    const auto constant = builder.expression(value).constant;
    if (!constant.has_value()) {
        if (!module_analysis.error_observed_since(failure_checkpoint)) {
            module_analysis.emit(
                module_analysis.syntax().expression(declaration.initializer).span,
                "constant initializer is not a supported compile-time value",
                DiagnosticCode::ConstInitializer
            );
        }
        return false;
    }
    builder.define_symbol_constant(symbol, *constant);
    return true;
}

auto elaborate_enum_case(
    ModuleAnalysis& module_analysis,
    SymbolID owner,
    std::size_t index
) noexcept -> bool {
    auto& builder = module_analysis.builder();
    const auto* catalog_symbol = module_analysis.catalog().symbol(owner);
    const auto* catalog_enumeration =
        catalog_symbol == nullptr ? nullptr : std::get_if<CatalogEnumForm>(&catalog_symbol->form);
    if (catalog_enumeration == nullptr || index >= catalog_enumeration->cases.size()) {
        invariant_violation("enum case elaboration is inconsistent with its catalog signature");
    }
    const auto signature =
        module_analysis.declarations().enumeration(catalog_enumeration->enumeration);
    const auto& item = module_analysis.syntax().item(catalog_symbol->item_id);
    const auto* enumeration = std::get_if<ASTEnumDecl>(&item.value);
    if (enumeration == nullptr || index >= enumeration->cases.size()) {
        invariant_violation("enum case elaboration is inconsistent with its syntax declaration");
    }
    const auto& source_member = enumeration->cases[index];
    const auto enum_case_id = signature.cases[index];
    const auto enum_case = module_analysis.declarations().enum_case(enum_case_id);
    const auto member_symbol = enum_case.symbol;
    if (builder.symbol_constant(member_symbol).has_value()) {
        return true;
    }

    if (signature.profile == HIREnumProfile::Payload) {
        if (source_member.initializer.has_value()) {
            module_analysis.emit(
                module_analysis.syntax().expression(*source_member.initializer).span,
                "payload enum case cannot declare a numeric initializer",
                DiagnosticCode::TypeEnumProfile
            );
        }
        if (!enum_case.payload_types.empty()) {
            return true;
        }
        builder.define_symbol_constant(
            member_symbol,
            builder.append_constant({
                .type = symbol_type(module_analysis, member_symbol),
                .value = HIRPayloadEnumConstant {
                    .enum_case = enum_case_id,
                    .payload = {},
                },
            })
        );
        return true;
    }

    auto value = HIRIntegerConstant::zero();
    if (source_member.initializer.has_value()) {
        auto scopes = ScopeStack();
        const auto failure_checkpoint = module_analysis.error_checkpoint();
        const auto expression_id = build_expected_expression(
            module_analysis,
            scopes,
            root_body_control(),
            *source_member.initializer,
            *signature.underlying_type
        );
        const auto selected = constant_integer(module_analysis, expression_id);
        if (!selected.has_value()) {
            if (!module_analysis.error_observed_since(failure_checkpoint)) {
                module_analysis.emit(
                    module_analysis.syntax().expression(*source_member.initializer).span,
                    "enum case initializer must be a constant integer",
                    DiagnosticCode::ConstEnumCase
                );
            }
            return false;
        }
        value = *selected;
    } else if (index != 0) {
        const auto previous_case =
            module_analysis.declarations().enum_case(signature.cases[index - 1]);
        const auto previous_symbol = previous_case.symbol;
        if (!module_analysis.resolve_declaration(previous_symbol, source_member.name_span)) {
            return false;
        }
        const auto previous_id = builder.symbol_constant(previous_symbol);
        if (!previous_id.has_value()) {
            invariant_violation("resolved previous enum case has no constant fact");
        }
        const auto* previous =
            std::get_if<HIRNumericEnumConstant>(&builder.constant(*previous_id).value);
        if (previous == nullptr) {
            invariant_violation("resolved previous numeric enum case has a non-numeric fact");
        }
        if (previous->value.negative()) {
            const auto signed_value = previous->value.as_signed();
            if (!signed_value.has_value()
                || *signed_value == std::numeric_limits<std::int64_t>::max()) {
                module_analysis.emit(
                    source_member.name_span,
                    "enum case value overflows",
                    DiagnosticCode::ConstEnumOverflow
                );
                return false;
            }
            value = HIRIntegerConstant::from_signed(*signed_value + 1);
        } else {
            if (previous->value.magnitude() == std::numeric_limits<std::uint64_t>::max()) {
                module_analysis.emit(
                    source_member.name_span,
                    "enum case value overflows",
                    DiagnosticCode::ConstEnumOverflow
                );
                return false;
            }
            value = HIRIntegerConstant::from_parts(previous->value.magnitude() + 1, false);
        }
    }

    if (!integer_constant_fits(module_analysis, value, *signature.underlying_type)) {
        module_analysis.emit(
            source_member.initializer.has_value()
                ? module_analysis.syntax().expression(*source_member.initializer).span
                : source_member.name_span,
            "enum case value is not representable by the underlying type",
            DiagnosticCode::ConstEnumRange
        );
        return false;
    }

    builder.define_symbol_constant(
        member_symbol,
        builder.append_constant({
            .type = symbol_type(module_analysis, member_symbol),
            .value = HIRNumericEnumConstant {
                .enum_case = enum_case_id,
                .value = value,
            },
        })
    );
    return true;
}

auto validate_enum_codes(ModuleAnalysis& module_analysis, SymbolID owner) noexcept -> void {
    const auto* catalog_symbol = module_analysis.catalog().symbol(owner);
    const auto* catalog_enumeration =
        catalog_symbol == nullptr ? nullptr : std::get_if<CatalogEnumForm>(&catalog_symbol->form);
    if (catalog_enumeration == nullptr) {
        invariant_violation("enum validation references a non-enum catalog symbol");
    }
    const auto signature =
        module_analysis.declarations().enumeration(catalog_enumeration->enumeration);
    if (signature.profile != HIREnumProfile::Numeric) {
        return;
    }
    auto prior_values = std::vector<std::pair<HIRIntegerConstant, std::size_t>>();
    prior_values.reserve(signature.cases.size());
    const auto& builder = module_analysis.builder();
    for (auto index = 0uz; index < signature.cases.size(); ++index) {
        const auto member = module_analysis.declarations().enum_case(signature.cases[index]);
        const auto constant = builder.symbol_constant(member.symbol);
        if (!constant.has_value()) {
            continue;
        }
        const auto* value = std::get_if<HIRNumericEnumConstant>(&builder.constant(*constant).value);
        if (value == nullptr) {
            continue;
        }
        const auto prior = std::ranges::find_if(prior_values, [&](const auto& candidate) noexcept {
            return candidate.first == value->value;
        });
        if (prior != prior_values.end()) {
            auto diagnostic = DiagnosticBuilder(
                DiagnosticCode::TypeEnumDuplicateCode,
                "enum numeric codes must be unique"
            );
            diagnostic.primary(
                module_analysis.diagnostic_span(member.origin),
                "duplicate numeric code"
            );
            diagnostic.related(
                module_analysis.diagnostic_span(
                    module_analysis.declarations().enum_case(signature.cases[prior->second]).origin
                ),
                "first case with this code"
            );
            module_analysis.emit(diagnostic.build());
            continue;
        }
        prior_values.emplace_back(value->value, index);
    }
}
