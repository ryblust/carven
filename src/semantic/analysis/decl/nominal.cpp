module carven:semantic.analysis.decl.nominal.impl;

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

auto DeclarationResolver::resolve_struct(
    const CatalogSymbol& symbol,
    const CatalogStructForm& form,
    ASTView syntax,
    const ASTStructDecl& structure,
    Span item_span
) noexcept -> AnalysisResult<void> {
    auto fields = std::vector<ConstructionStructField>();
    auto names = std::flat_map<std::string, Span, std::less<>>();
    fields.reserve(structure.fields.size());
    for (const auto& field : structure.fields) {
        auto name = draft.source_slice_copy(symbol.module_id, field.name_span);
        const auto [prior, inserted] = names.emplace(name, field.name_span);
        if (!inserted) {
            auto diagnostic = DiagnosticBuilder(
                DiagnosticCode::NameDuplicateField,
                "a structure field is declared more than once"
            );
            diagnostic.primary(
                locate(source_id(draft, symbol.module_id), field.name_span),
                "duplicate field"
            );
            diagnostic.related(
                locate(source_id(draft, symbol.module_id), prior->second),
                "first declaration"
            );
            return std::unexpected(draft.diagnostics().error(diagnostic.build()));
        }
        auto type = resolve_value_type(symbol.module_id, syntax, field.type, "structure field");
        if (!type.has_value()) {
            return std::unexpected(type.error());
        }
        fields.push_back({
            .name = draft.intern_spelling(name),
            .type = *type,
            .origin = declaration_origin(draft, symbol.module_id, field.span),
        });
    }
    if (form.structure.index() >= structures.size()) {
        invariant_violation("struct declaration identity is outside its reserved table");
    }
    structures[form.structure.index()] = ConstructionStructDeclaration {
        .module_id = module_declaration(symbol.module_id),
        .name = draft.intern_spelling(symbol.name),
        .origin = declaration_origin(draft, symbol.module_id, item_span),
        .visibility = symbol.visibility,
        .fields = std::move(fields),
        .capabilities = {.equality = false},
    };
    return {};
}

auto DeclarationResolver::resolve_enum(
    const CatalogSymbol& symbol,
    const CatalogEnumForm& form,
    ASTView syntax,
    const ASTEnumDecl& enumeration,
    Span item_span
) noexcept -> AnalysisResult<void> {
    if (enumeration.cases.empty()) {
        return std::unexpected(fail(
            draft,
            symbol.module_id,
            enumeration.name_span,
            DiagnosticCode::TypeEnumEmpty,
            "enum must declare at least one case"
        ));
    }
    if (form.cases.size() != enumeration.cases.size()) {
        invariant_violation("enum catalog cases are not aligned with source syntax");
    }
    const auto payload_representation =
        std::ranges::any_of(enumeration.cases, [](const ASTEnumCase& value) noexcept {
            return !value.payload_types.empty();
        });
    if (payload_representation && enumeration.underlying_type.has_value()) {
        return std::unexpected(fail(
            draft,
            symbol.module_id,
            syntax.type(*enumeration.underlying_type).span,
            DiagnosticCode::TypeEnumProfile,
            "payload enum cannot declare an underlying integer type"
        ));
    }

    auto names = std::flat_map<std::string, Span, std::less<>>();
    for (auto index = 0uz; index < enumeration.cases.size(); ++index) {
        const auto& source_case = enumeration.cases[index];
        auto name = draft.source_slice_copy(symbol.module_id, source_case.name_span);
        const auto [prior, inserted] = names.emplace(name, source_case.name_span);
        if (!inserted) {
            auto diagnostic = DiagnosticBuilder(
                DiagnosticCode::NameDuplicateEnumCase,
                "an enum case is declared more than once"
            );
            diagnostic.primary(
                locate(source_id(draft, symbol.module_id), source_case.name_span),
                "duplicate case"
            );
            diagnostic.related(
                locate(source_id(draft, symbol.module_id), prior->second),
                "first declaration"
            );
            return std::unexpected(draft.diagnostics().error(diagnostic.build()));
        }
        const auto case_id = form.cases[index];
        const auto& case_symbol = catalog_symbol(catalog, catalog.enum_case_symbol(case_id));
        const auto* case_form = std::get_if<CatalogEnumCaseForm>(&case_symbol.form);
        if (case_form == nullptr
            || case_form->owner != form.enumeration
            || case_form->index != index
            || case_form->enum_case != case_id) {
            invariant_violation("enum case catalog identity is inconsistent");
        }
        auto payload = std::vector<ConstructionTypeRef>();
        payload.reserve(source_case.payload_types.size());
        for (const auto source_type : source_case.payload_types) {
            auto type = resolve_value_type(symbol.module_id, syntax, source_type, "enum payload");
            if (!type.has_value()) {
                return std::unexpected(type.error());
            }
            payload.push_back(*type);
        }
        enum_cases[case_id.index()] = ConstructionEnumCaseDeclaration {
            .owner = form.enumeration,
            .name = draft.intern_spelling(name),
            .origin = declaration_origin(draft, symbol.module_id, source_case.span),
            .payload_types = std::move(payload),
            .constant = std::nullopt,
        };
    }

    auto representation = ConstructionEnumRepresentation {PayloadEnumRepresentation {}};
    if (!payload_representation) {
        auto underlying = ConstructionTypeRef {draft.intern_builtin_type(BuiltinType::I32)};
        if (enumeration.underlying_type.has_value()) {
            auto resolved = resolve_type(symbol.module_id, syntax, *enumeration.underlying_type);
            if (!resolved.has_value()) {
                return std::unexpected(resolved.error());
            }
            underlying = *resolved;
        }
        const auto* concrete = std::get_if<TypeID>(&underlying);
        auto canonical = std::optional<CanonicalType>();
        if (concrete != nullptr) {
            canonical = draft.type_copy(*concrete);
        }
        const auto* builtin =
            canonical.has_value() ? std::get_if<BuiltinTypeValue>(&canonical->value) : nullptr;
        if (builtin == nullptr || !builtin_is_integer(builtin->kind)) {
            return std::unexpected(fail(
                draft,
                symbol.module_id,
                enumeration.underlying_type.has_value()
                    ? syntax.type(*enumeration.underlying_type).span
                    : enumeration.name_span,
                DiagnosticCode::TypeEnumUnderlying,
                "enum underlying type must be an integer type"
            ));
        }
        representation = ConstructionNumericEnumRepresentation {
            .underlying_type = underlying,
        };
    }
    enumerations[form.enumeration.index()] = ConstructionEnumDeclaration {
        .module_id = module_declaration(symbol.module_id),
        .name = draft.intern_spelling(symbol.name),
        .origin = declaration_origin(draft, symbol.module_id, item_span),
        .visibility = symbol.visibility,
        .representation = representation,
        .cases = form.cases,
        .capabilities = {.equality = false},
    };
    return {};
}

auto DeclarationResolver::resolve_enum_case(
    const CatalogSymbol& symbol,
    const CatalogEnumCaseForm& form
) noexcept -> AnalysisResult<void> {
    const auto owner_symbol_id = catalog.enum_symbol(form.owner);
    auto owner_result = resolve(owner_symbol_id, symbol.module_id, symbol.declaration_span);
    if (!owner_result.has_value()) {
        return std::unexpected(owner_result.error());
    }
    if (form.owner.index() >= enumerations.size()
        || !enumerations[form.owner.index()].has_value()
        || form.enum_case.index() >= enum_cases.size()
        || !enum_cases[form.enum_case.index()].has_value()) {
        invariant_violation("resolved enum case has no owner contract");
    }
    auto& declaration = *enum_cases[form.enum_case.index()];
    const auto& owner = *enumerations[form.owner.index()];
    const auto& owner_symbol = catalog_symbol(catalog, owner_symbol_id);
    const auto syntax = draft.syntax_tree(symbol.module_id).view();
    const auto* source_enum = std::get_if<ASTEnumDecl>(&syntax.item(owner_symbol.item_id).value);
    if (source_enum == nullptr || form.index >= source_enum->cases.size()) {
        invariant_violation("enum case source syntax is inconsistent");
    }
    const auto& source_case = source_enum->cases[form.index];
    const auto enum_type = draft.intern_type(
        CanonicalType {
            .value = EnumTypeValue {.enumeration = form.owner},
        }
    );

    const auto* numeric = std::get_if<ConstructionNumericEnumRepresentation>(&owner.representation);
    if (numeric == nullptr) {
        if (source_case.initializer.has_value()) {
            return std::unexpected(fail(
                draft,
                symbol.module_id,
                syntax.expression(*source_case.initializer).span,
                DiagnosticCode::TypeEnumProfile,
                "payload enum case cannot declare a numeric initializer"
            ));
        }
        if (declaration.payload_types.empty()) {
            declaration.constant = draft.intern_constant(
                ConstantFact {
                    .type = enum_type,
                    .value = PayloadEnumConstant {
                        .enum_case = form.enum_case,
                        .payload = {},
                    },
                }
            );
        }
        return {};
    }

    const auto* underlying = std::get_if<TypeID>(&numeric->underlying_type);
    if (underlying == nullptr) {
        invariant_violation("numeric enum underlying type is not concrete");
    }
    auto value = IntegerConstant::zero();
    if (source_case.initializer.has_value()) {
        const auto environment = constant_environment(symbol.module_id, syntax);
        auto proof = prove_constant_expression(
            draft,
            symbol.module_id,
            syntax,
            environment,
            *source_case.initializer,
            numeric->underlying_type
        );
        if (!proof.has_value()) {
            return std::unexpected(proof.error());
        }
        const auto* selected =
            proof->has_value() ? std::get_if<IntegerConstant>(&(**proof).value) : nullptr;
        if (selected == nullptr) {
            return std::unexpected(fail(
                draft,
                symbol.module_id,
                syntax.expression(*source_case.initializer).span,
                DiagnosticCode::ConstEnumCase,
                "enum case initializer must be a constant integer"
            ));
        }
        value = *selected;
    } else if (form.index != 0u) {
        const auto& owner_form = std::get<CatalogEnumForm>(owner_symbol.form);
        const auto previous_id = owner_form.cases[form.index - 1u];
        const auto previous_symbol = catalog.enum_case_symbol(previous_id);
        auto previous_result = resolve(previous_symbol, symbol.module_id, source_case.name_span);
        if (!previous_result.has_value()) {
            return std::unexpected(previous_result.error());
        }
        const auto previous_constant = enum_cases[previous_id.index()]->constant;
        if (!previous_constant.has_value()) {
            invariant_violation("resolved numeric enum case has no constant");
        }
        const auto fact = draft.constant_copy(*previous_constant);
        const auto* previous = std::get_if<NumericEnumConstant>(&fact.value);
        if (previous == nullptr) {
            invariant_violation("numeric enum predecessor has a non-numeric constant");
        }
        if (previous->value.negative()) {
            const auto signed_value = previous->value.as_signed();
            if (!signed_value.has_value()
                || *signed_value == std::numeric_limits<std::int64_t>::max()) {
                return std::unexpected(fail(
                    draft,
                    symbol.module_id,
                    source_case.name_span,
                    DiagnosticCode::ConstEnumOverflow,
                    "enum case value overflows"
                ));
            }
            value = IntegerConstant::from_signed(*signed_value + 1);
        } else {
            if (previous->value.magnitude() == std::numeric_limits<std::uint64_t>::max()) {
                return std::unexpected(fail(
                    draft,
                    symbol.module_id,
                    source_case.name_span,
                    DiagnosticCode::ConstEnumOverflow,
                    "enum case value overflows"
                ));
            }
            value = IntegerConstant::from_parts(previous->value.magnitude() + 1u, false);
        }
    }
    const auto canonical = draft.type_copy(*underlying);
    const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value);
    if (builtin == nullptr || !integer_constant_fits(value, builtin->kind)) {
        return std::unexpected(fail(
            draft,
            symbol.module_id,
            source_case.initializer.has_value() ? syntax.expression(*source_case.initializer).span
                                                : source_case.name_span,
            DiagnosticCode::ConstEnumRange,
            "enum case value is not representable by the underlying type"
        ));
    }
    declaration.constant = draft.intern_constant(
        ConstantFact {
            .type = enum_type,
            .value = NumericEnumConstant {.enum_case = form.enum_case, .value = value},
        }
    );
    return {};
}

auto DeclarationResolver::validate_enum_codes(const CatalogSymbol& symbol) noexcept
    -> AnalysisResult<void> {
    const auto& form = std::get<CatalogEnumForm>(symbol.form);
    const auto& declaration = enumerations[form.enumeration.index()];
    if (!declaration.has_value()
        || !std::holds_alternative<ConstructionNumericEnumRepresentation>(
            declaration->representation
        )) {
        return {};
    }
    auto prior = std::vector<std::pair<IntegerConstant, EnumCaseID>>();
    for (const auto case_id : declaration->cases) {
        const auto& enum_case = enum_cases[case_id.index()];
        if (!enum_case.has_value() || !enum_case->constant.has_value()) {
            continue;
        }
        const auto fact = draft.constant_copy(*enum_case->constant);
        const auto* numeric = std::get_if<NumericEnumConstant>(&fact.value);
        if (numeric == nullptr) {
            invariant_violation("numeric enum case has a non-numeric constant");
        }
        const auto found = std::ranges::find(
            prior,
            numeric->value,
            &std::pair<IntegerConstant, EnumCaseID>::first
        );
        if (found != prior.end()) {
            auto diagnostic = DiagnosticBuilder(
                DiagnosticCode::TypeEnumDuplicateCode,
                "enum numeric codes must be unique"
            );
            diagnostic.primary(draft.source_span(enum_case->origin), "duplicate numeric code");
            diagnostic.related(
                draft.source_span(enum_cases[found->second.index()]->origin),
                "first case with this code"
            );
            return std::unexpected(draft.diagnostics().error(diagnostic.build()));
        }
        prior.emplace_back(numeric->value, case_id);
    }
    return {};
}

} // namespace decl_resolution
