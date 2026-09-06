module carven:semantic.analysis.decl.surface.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.interop;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.decl.context;
import :semantic.analysis.decl;
import :semantic.analysis.expr.scope;
import :semantic.analysis.interop;
import :semantic.analysis.nominal.containment;
import :semantic.analysis.operations;
import :semantic.analysis.program;
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

namespace {

auto surface_allows(
    const ProgramDraft& draft,
    DeclarationVisibility surface_visibility,
    ProgramModuleID surface_module,
    const CatalogSymbol& referenced
) noexcept -> bool {
    switch (surface_visibility) {
        case DeclarationVisibility::Module: return true;
        case DeclarationVisibility::ModuleDomain:
            if (referenced.visibility == DeclarationVisibility::Module) {
                return false;
            }
            return referenced.visibility == DeclarationVisibility::Compilation
                || same_module_domain(
                       draft.module_path_copy(surface_module),
                       draft.module_path_copy(referenced.module_id)
                );
        case DeclarationVisibility::Compilation:
            return referenced.visibility == DeclarationVisibility::Compilation;
    }
    std::unreachable();
}

class DeclarationSurfaceValidator final {
public:
    DeclarationSurfaceValidator(
        ProgramDraft& target,
        AnalysisCatalogView source_catalog,
        DeclarationVisibility visibility,
        ProgramModuleID module_id,
        SourceSpan primary,
        std::string_view description,
        std::optional<AnalysisFailure>& failure_state
    ) noexcept
        : draft(target),
          catalog(source_catalog),
          surface_visibility(visibility),
          surface_module(module_id),
          primary_span(primary),
          surface_description(description),
          failure(failure_state) {}

    auto validate(ConstructionTypeRef type) noexcept -> void {
        if (const auto* term = std::get_if<TypeTermID>(&type)) {
            const auto construction = draft.construction_type_copy(*term);
            std::visit(
                Overloaded {
                    [&](const ConstructionArrayTypeValue& array) noexcept {
                        validate(array.element);
                    },
                    [&](const ConstructionCallableViewTypeValue& callable) noexcept {
                        for (const auto& parameter : callable.parameters) {
                            validate(parameter.type);
                        }
                        validate(callable.result);
                    },
                },
                construction.value
            );
            return;
        }
        const auto concrete = std::get<TypeID>(type);
        if (!visited_types.insert(concrete).second) {
            return;
        }
        std::visit(
            Overloaded {
                [](const BuiltinTypeValue&) static noexcept {},
                [&](const StructTypeValue& value) noexcept {
                    validate_nominal(NominalDeclarationRef {value.structure});
                },
                [&](const EnumTypeValue& value) noexcept {
                    validate_nominal(NominalDeclarationRef {value.enumeration});
                },
                [&](const ArrayTypeValue& value) noexcept {
                    validate(ConstructionTypeRef {value.element});
                },
                [&](const FunctionTypeValue& value) noexcept { validate_callable(value.callable); },
                [&](const ClosureTypeValue& value) noexcept { validate_callable(value.callable); },
                [](const CallableViewTypeValue&) static noexcept {},
                [&](const CppTypeValue& value) noexcept {
                    for (const auto argument : cpp_type_references(value)) {
                        validate(ConstructionTypeRef {argument});
                    }
                },
            },
            draft.type_copy(concrete).value
        );
    }

    auto validate_constant(ConstantID constant) noexcept -> void {
        if (!visited_constants.insert(constant).second) {
            return;
        }
        const auto fact = draft.constant_copy(constant);
        validate(ConstructionTypeRef {fact.type});
        std::visit(
            [&](const auto& value) noexcept {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<Value, NumericEnumConstant>
                              || std::same_as<Value, PayloadEnumConstant>) {
                    const auto enum_case =
                        draft.construction_enum_case_declaration_copy(value.enum_case);
                    const auto canonical = draft.type_copy(fact.type);
                    const auto* nominal = std::get_if<EnumTypeValue>(&canonical.value);
                    if (nominal == nullptr || nominal->enumeration != enum_case.owner) {
                        invariant_violation(
                            "module constant enum value disagrees with its semantic type"
                        );
                    }
                    validate_nominal(NominalDeclarationRef {enum_case.owner});
                    if constexpr (std::same_as<Value, PayloadEnumConstant>) {
                        for (const auto child : value.payload) {
                            validate_constant(child);
                        }
                    }
                } else {
                    static_assert(
                        std::same_as<Value, IntegerConstant>
                            || std::same_as<Value, BooleanConstant>
                            || std::same_as<Value, StringConstant>
                            || std::same_as<Value, F32Constant>
                            || std::same_as<Value, F64Constant>
                            || std::same_as<Value, CharacterConstant>,
                        "unhandled module constant value"
                    );
                }
            },
            fact.value
        );
    }

    auto validate_source(ASTView syntax, ASTTypeID type) noexcept -> void {
        const auto& source = syntax.type(type);
        std::visit(
            Overloaded {
                [&](const ASTNamedType& named) noexcept {
                    if (named.global_root.has_value() || named.components.size() != 1uz) {
                        return;
                    }
                    const auto name =
                        draft.source_slice_copy(surface_module, named.components.front().name_span);
                    const auto candidates = catalog.lookup(surface_module, name);
                    if (candidates.size() != 1uz) {
                        return;
                    }
                    const auto& selected = catalog_symbol(catalog, candidates.front().symbol_id);
                    if (const auto* structure = std::get_if<CatalogStructForm>(&selected.form)) {
                        validate_nominal(NominalDeclarationRef {structure->structure});
                    } else if (const auto* enumeration =
                                   std::get_if<CatalogEnumForm>(&selected.form)) {
                        validate_nominal(
                            NominalDeclarationRef {
                                enumeration->enumeration,
                            }
                        );
                    }
                },
                [&](const ASTArrayType& array) noexcept {
                    validate_source(syntax, array.element_type);
                },
                [&](const ASTFunctionType& callable) noexcept {
                    for (const auto& parameter : callable.parameters) {
                        validate_source(syntax, parameter.type);
                    }
                    validate_source(syntax, callable.result_type);
                    if (callable.throw_clause.has_value()) {
                        for (const auto member : callable.throw_clause->failures) {
                            validate_source(syntax, member);
                        }
                    }
                },
            },
            source.value
        );
    }

private:
    auto validate_callable(CallableID callable) noexcept -> void {
        const auto contract = draft.construction_callable_contract_copy(callable);
        for (const auto& parameter : contract.parameters) {
            validate(parameter.type);
        }
        validate(contract.result);
        if (contract.policy == FailureContractPolicy::Declared) {
            for (const auto member :
                 draft.construction_failure_term_copy(contract.failures).direct_members) {
                validate(ConstructionTypeRef {member});
            }
        }
    }

    auto validate_nominal(NominalDeclarationRef nominal) noexcept -> void {
        if (!reported_nominals.insert(nominal).second) {
            return;
        }
        const auto symbol_id = std::visit(
            Overloaded {
                [&](StructID id) noexcept { return catalog.struct_symbol(id); },
                [&](EnumID id) noexcept { return catalog.enum_symbol(id); },
            },
            nominal
        );
        const auto& referenced = catalog_symbol(catalog, symbol_id);
        if (surface_allows(draft, surface_visibility, surface_module, referenced)) {
            return;
        }
        auto diagnostic = DiagnosticBuilder(
            DiagnosticCode::TypeVisibilityLeak,
            std::format("{} references a declaration with narrower visibility", surface_description)
        );
        diagnostic.primary(primary_span);
        diagnostic.related(
            locate(source_id(draft, referenced.module_id), referenced.declaration_span),
            "referenced declaration"
        );
        failure = draft.diagnostics().error(diagnostic.build());
    }

    ProgramDraft& draft;
    AnalysisCatalogView catalog;
    DeclarationVisibility surface_visibility;
    ProgramModuleID surface_module;
    SourceSpan primary_span;
    std::string_view surface_description;
    std::optional<AnalysisFailure>& failure;
    std::flat_set<TypeID> visited_types;
    std::flat_set<ConstantID> visited_constants;
    std::flat_set<NominalDeclarationRef> reported_nominals;
};

auto validate_source_surface(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    const CatalogSymbol& declaration,
    ASTView syntax,
    ASTTypeID type,
    Span primary,
    std::string_view description,
    std::optional<AnalysisFailure>& failure
) noexcept -> void {
    auto validator = DeclarationSurfaceValidator(
        draft,
        catalog,
        declaration.visibility,
        declaration.module_id,
        locate(source_id(draft, declaration.module_id), primary),
        description,
        failure
    );
    validator.validate_source(syntax, type);
}

auto validate_module_constant_surface(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    const CatalogSymbol& symbol,
    const CatalogConstantForm& form,
    ASTView syntax,
    const ASTConstantDecl& source,
    std::optional<AnalysisFailure>& failure
) noexcept -> void {
    const auto declaration = draft.module_constant_declaration_copy(form.constant);
    auto validator = DeclarationSurfaceValidator(
        draft,
        catalog,
        symbol.visibility,
        symbol.module_id,
        locate(
            source_id(draft, symbol.module_id),
            source.type.has_value() ? syntax.type(*source.type).span : source.name_span
        ),
        "module constant",
        failure
    );
    if (source.type.has_value()) {
        validator.validate_source(syntax, *source.type);
    }
    validator.validate_constant(declaration.value);
}

} // namespace

auto validate_declaration_surfaces(ProgramDraft& draft, AnalysisCatalogView catalog) noexcept
    -> AnalysisResult<void> {
    auto failure = std::optional<AnalysisFailure>();
    for (const auto& symbol : catalog.symbols()) {
        const auto syntax = draft.syntax_tree(symbol.module_id).view();
        const auto& item = syntax.item(symbol.item_id);
        std::visit(
            Overloaded {
                [&](const CatalogFunctionForm&) noexcept {
                    const auto* function = std::get_if<ASTFunctionDecl>(&item.value);
                    if (function == nullptr) {
                        invariant_violation("function surface does not match source syntax");
                    }
                    for (const auto& parameter : function->parameters) {
                        if (parameter.type.has_value()) {
                            validate_source_surface(
                                draft,
                                catalog,
                                symbol,
                                syntax,
                                *parameter.type,
                                parameter.span,
                                "function parameter",
                                failure
                            );
                        }
                    }
                    if (function->result_type.has_value()) {
                        validate_source_surface(
                            draft,
                            catalog,
                            symbol,
                            syntax,
                            *function->result_type,
                            syntax.type(*function->result_type).span,
                            "function result",
                            failure
                        );
                    }
                    if (function->throw_clause.has_value()) {
                        for (const auto type : function->throw_clause->failures) {
                            validate_source_surface(
                                draft,
                                catalog,
                                symbol,
                                syntax,
                                type,
                                syntax.type(type).span,
                                "function failure",
                                failure
                            );
                        }
                    }
                },
                [&](const CatalogStructForm&) noexcept {
                    const auto* structure = std::get_if<ASTStructDecl>(&item.value);
                    if (structure == nullptr) {
                        invariant_violation("struct surface does not match source syntax");
                    }
                    for (const auto& field : structure->fields) {
                        validate_source_surface(
                            draft,
                            catalog,
                            symbol,
                            syntax,
                            field.type,
                            field.span,
                            "struct field",
                            failure
                        );
                    }
                },
                [&](const CatalogEnumForm&) noexcept {
                    const auto* enumeration = std::get_if<ASTEnumDecl>(&item.value);
                    if (enumeration == nullptr) {
                        invariant_violation("enum surface does not match source syntax");
                    }
                    if (enumeration->underlying_type.has_value()) {
                        validate_source_surface(
                            draft,
                            catalog,
                            symbol,
                            syntax,
                            *enumeration->underlying_type,
                            syntax.type(*enumeration->underlying_type).span,
                            "enum underlying type",
                            failure
                        );
                    }
                    for (const auto& enum_case : enumeration->cases) {
                        for (const auto type : enum_case.payload_types) {
                            validate_source_surface(
                                draft,
                                catalog,
                                symbol,
                                syntax,
                                type,
                                syntax.type(type).span,
                                "enum payload",
                                failure
                            );
                        }
                    }
                },
                [&](const CatalogConstantForm& form) noexcept {
                    const auto* constant = std::get_if<ASTConstantDecl>(&item.value);
                    if (constant == nullptr) {
                        invariant_violation("constant surface does not match source syntax");
                    }
                    validate_module_constant_surface(
                        draft,
                        catalog,
                        symbol,
                        form,
                        syntax,
                        *constant,
                        failure
                    );
                },
                [](const CatalogEnumCaseForm&) static noexcept {},
            },
            symbol.form
        );
    }
    return failure.has_value() ? AnalysisResult<void>(std::unexpected(*failure))
                               : AnalysisResult<void>();
}

} // namespace decl_resolution
