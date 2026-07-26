module carven:semantic.analysis.validation.contracts.impl;

import :diagnostics.builder;
import :diagnostics.diagnosed;
import :diagnostics.diagnostic;
import :diagnostics.sink;
import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.ids;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.region;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :frontend.ast.type;
import :semantic.analysis.analyzer;
import :semantic.analysis.catalog;
import :semantic.analysis.elaboration.types;
import :semantic.analysis.validation;
import :semantic.hir;
import :semantic.hir.access;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.pattern;
import :semantic.hir.stmt;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :semantic.visibility;
import :source.manager;
import :source.provenance;
import :source.text;
import :support.invariant;
import :support.visit;
import std;

namespace {

struct NominalDeclarationSurface final {
    DeclarationVisibility visibility;
    ProgramModuleID module_id;
    ProgramOriginID origin;
};

auto nominal_declaration_surface(
    const SemanticConstruction& builder,
    HIRNominalDeclRef nominal
) noexcept -> std::optional<NominalDeclarationSurface> {
    return std::visit(
        Overloaded {
            [&](StructID id) noexcept -> std::optional<NominalDeclarationSurface> {
                if (id.index() >= builder.structures().size()) {
                    return std::nullopt;
                }
                const auto& declaration = builder.structure(id);
                const auto module_id = builder.symbol(declaration.symbol).module_id;
                return module_id.has_value() ? std::optional {NominalDeclarationSurface {
                                                   .visibility = declaration.visibility,
                                                   .module_id = *module_id,
                                                   .origin = declaration.origin,
                                               }}
                                             : std::nullopt;
            },
            [&](EnumID id) noexcept -> std::optional<NominalDeclarationSurface> {
                if (id.index() >= builder.enumerations().size()) {
                    return std::nullopt;
                }
                const auto& declaration = builder.enumeration(id);
                const auto module_id = builder.symbol(declaration.symbol).module_id;
                return module_id.has_value() ? std::optional {NominalDeclarationSurface {
                                                   .visibility = declaration.visibility,
                                                   .module_id = *module_id,
                                                   .origin = declaration.origin,
                                               }}
                                             : std::nullopt;
            },
        },
        nominal
    );
}

class DeclarationSurfaceValidator final {
public:
    DeclarationSurfaceValidator(
        const SemanticConstruction& builder,
        DiagnosticSink& diagnostics,
        DeclarationVisibility visibility,
        ProgramModuleID defining_module,
        ProgramOriginID origin,
        std::string_view surface
    ) noexcept
        : builder(builder),
          diagnostics(diagnostics),
          audience(declaration_audience(visibility, defining_module, builder.provenance())),
          surface_origin(origin),
          surface_description(surface) {}

    auto validate_type(HIRTypeID type_id) noexcept -> void {
        if (!visited_types.insert(type_id).second) {
            return;
        }
        const auto& value = builder.type(type_id).value;
        std::visit(
            Overloaded {
                [&](const HIRStructTypeValue& nominal) noexcept {
                    validate_nominal_identity(HIRNominalDeclRef {nominal.structure});
                },
                [&](const HIREnumTypeValue& nominal) noexcept {
                    validate_nominal_identity(HIRNominalDeclRef {nominal.enumeration});
                },
                [&](const HIRArrayTypeValue& array) noexcept {
                    validate_type(array.element_type_id);
                },
                [&](const HIRFunctionTypeValue& function) noexcept {
                    validate_callable(builder.callable(function.callable));
                },
                [&](const HIRFunctionRefTypeValue& function) noexcept {
                    validate_callable(builder.callable_signature(function.signature));
                },
                [&](const HIRClosureTypeValue& closure) noexcept {
                    validate_callable(builder.callable(closure.callable));
                },
                [](const HIRBuiltinTypeValue&) static noexcept {},
                [](const HIRForeignTypeValue&) static noexcept {},
                [](const HIRErrorTypeValue&) static noexcept {},
            },
            value
        );
    }

    auto validate_constant(HIRConstantID constant_id) noexcept -> void {
        if (!visited_constants.insert(constant_id).second) {
            return;
        }
        const auto& fact = builder.constant(constant_id);
        validate_type(fact.type);
        std::visit(
            Overloaded {
                [&](const HIRNumericEnumConstant& value) noexcept {
                    validate_enum_case(value.enum_case);
                },
                [&](const HIRPayloadEnumConstant& value) noexcept {
                    validate_enum_case(value.enum_case);
                    for (const auto payload : value.payload) {
                        validate_constant(payload);
                    }
                },
                [](const HIRIntegerConstant&) static noexcept {},
                [](const HIRBooleanConstant&) static noexcept {},
                [](const HIRStringConstant&) static noexcept {},
                [](const HIRFloatingConstant&) static noexcept {},
                [](const HIRCharacterConstant&) static noexcept {},
            },
            fact.value
        );
    }

private:
    template<typename Callable>
    auto validate_callable(const Callable& value) noexcept -> void {
        for (const auto& parameter : value.parameters) {
            validate_type(parameter.type);
        }
        validate_type(value.result);
        for (const auto failure : builder.failure_set(value.failure_set).members) {
            validate_type(failure);
        }
    }

    auto validate_enum_case(EnumCaseID enum_case) noexcept -> void {
        if (enum_case.index() < builder.enum_cases().size()) {
            validate_nominal_identity(HIRNominalDeclRef {builder.enum_case(enum_case).owner});
        }
    }

    auto validate_nominal_identity(HIRNominalDeclRef nominal) noexcept -> void {
        const auto referenced = nominal_declaration_surface(builder, nominal);
        if (!referenced.has_value() || !reported_nominals.insert(nominal).second) {
            return;
        }
        const auto referenced_audience = declaration_audience(
            referenced->visibility,
            referenced->module_id,
            builder.provenance()
        );
        if (audience_subset_of(audience, referenced_audience, builder.provenance())) {
            return;
        }
        auto diagnostic = DiagnosticBuilder(
            DiagnosticCode::TypeVisibilityLeak,
            std::format("{} references a declaration with narrower visibility", surface_description)
        );
        diagnostic.primary(diagnostic_span(builder, surface_origin));
        diagnostic.related(diagnostic_span(builder, referenced->origin), "referenced declaration");
        diagnostics.emit(diagnostic.build());
    }

    const SemanticConstruction& builder;
    DiagnosticSink& diagnostics;
    DeclarationAudience audience;
    ProgramOriginID surface_origin;
    std::string_view surface_description;
    std::flat_set<HIRTypeID> visited_types;
    std::flat_set<HIRConstantID> visited_constants;
    std::flat_set<HIRNominalDeclRef> reported_nominals;
};

auto declaration_module(const SemanticConstruction& builder, SymbolID symbol) noexcept
    -> ProgramModuleID {
    const auto module_id = builder.symbol(symbol).module_id;
    if (!module_id.has_value()) {
        invariant_violation("top-level declaration symbol has no defining module");
    }
    return *module_id;
}

} // namespace

auto diagnose_declaration_surface_type(
    const SemanticConstruction& builder,
    DiagnosticSink& diagnostics,
    DeclarationVisibility visibility,
    ProgramModuleID defining_module,
    HIRTypeID type,
    ProgramOriginID origin,
    std::string_view surface
) noexcept -> void {
    auto validator = DeclarationSurfaceValidator(
        builder,
        diagnostics,
        visibility,
        defining_module,
        origin,
        surface
    );
    validator.validate_type(type);
}

auto diagnose_declaration_surface_constant(
    const SemanticConstruction& builder,
    DiagnosticSink& diagnostics,
    DeclarationVisibility visibility,
    ProgramModuleID defining_module,
    HIRConstantID constant,
    ProgramOriginID origin,
    std::string_view surface
) noexcept -> void {
    auto validator = DeclarationSurfaceValidator(
        builder,
        diagnostics,
        visibility,
        defining_module,
        origin,
        surface
    );
    validator.validate_constant(constant);
}

auto diagnose_type_contracts(
    const SemanticConstruction& builder,
    DiagnosticSink& diagnostics
) noexcept -> void {
    const auto contains_callable_view = [&](this const auto& self,
                                            HIRTypeID type_id) noexcept -> bool {
        const auto& value = builder.type(type_id).value;
        if (std::holds_alternative<HIRFunctionRefTypeValue>(value)) {
            return true;
        }
        if (const auto* array = std::get_if<HIRArrayTypeValue>(&value)) {
            return self(array->element_type_id);
        }
        return false;
    };
    const auto contains_foreign = [&](this const auto& self, HIRTypeID type_id) noexcept -> bool {
        const auto& value = builder.type(type_id).value;
        if (std::holds_alternative<HIRForeignTypeValue>(value)) {
            return true;
        }
        if (const auto* array = std::get_if<HIRArrayTypeValue>(&value)) {
            return self(array->element_type_id);
        }
        return false;
    };
    const auto report =
        [&](ProgramOriginID source_origin, std::string message, DiagnosticCode code) noexcept {
            diagnostics.emit(DiagnosticBuilder(code, std::move(message))
                                 .primary(diagnostic_span(builder, source_origin))
                                 .build());
        };
    for (auto index = 0uz; index < builder.structures().size(); ++index) {
        const auto id = StructID::from_index(static_cast<std::uint32_t>(index));
        const auto& structure = builder.structure(id);
        const auto defining_module = declaration_module(builder, structure.symbol);
        for (const auto& field : structure.fields) {
            if (contains_callable_view(field.type)) {
                report(
                    field.origin,
                    "non-owning callable view cannot be stored in a struct",
                    DiagnosticCode::TypeCallableViewEscape
                );
            }
            diagnose_declaration_surface_type(
                builder,
                diagnostics,
                structure.visibility,
                defining_module,
                field.type,
                field.origin,
                "struct field"
            );
        }
    }
    for (auto index = 0uz; index < builder.enumerations().size(); ++index) {
        const auto id = EnumID::from_index(static_cast<std::uint32_t>(index));
        const auto& enumeration = builder.enumeration(id);
        const auto defining_module = declaration_module(builder, enumeration.symbol);
        if (enumeration.underlying_type.has_value()) {
            diagnose_declaration_surface_type(
                builder,
                diagnostics,
                enumeration.visibility,
                defining_module,
                *enumeration.underlying_type,
                enumeration.origin,
                "enum underlying type"
            );
        }
        for (const auto member_id : enumeration.cases) {
            const auto& member = builder.enum_case(member_id);
            for (const auto payload : member.payload_types) {
                if (contains_callable_view(payload)) {
                    report(
                        member.origin,
                        "non-owning callable view cannot be stored in an enum payload",
                        DiagnosticCode::TypeCallableViewEscape
                    );
                }
                diagnose_declaration_surface_type(
                    builder,
                    diagnostics,
                    enumeration.visibility,
                    defining_module,
                    payload,
                    member.origin,
                    "enum payload"
                );
            }
        }
    }
    for (auto index = 0uz; index < builder.functions().size(); ++index) {
        const auto id = FunctionID::from_index(static_cast<std::uint32_t>(index));
        const auto& function = builder.function(id);
        const auto& body = builder.body(builder.callable(function.callable).body);
        const auto defining_module = declaration_module(builder, function.symbol);
        for (const auto& parameter : body.parameters) {
            diagnose_declaration_surface_type(
                builder,
                diagnostics,
                function.visibility,
                defining_module,
                parameter.type,
                parameter.origin,
                "function parameter"
            );
        }
        diagnose_declaration_surface_type(
            builder,
            diagnostics,
            function.visibility,
            defining_module,
            function.result,
            function.result_origin,
            "function result"
        );
        for (const auto failure :
             builder.failure_set(builder.callable(function.callable).failure_set).members) {
            diagnose_declaration_surface_type(
                builder,
                diagnostics,
                function.visibility,
                defining_module,
                failure,
                function.origin,
                "function failure"
            );
        }
        if (contains_callable_view(function.result)) {
            report(
                function.result_origin,
                "non-owning callable view cannot be returned from a function",
                DiagnosticCode::TypeCallableViewEscape
            );
        }
    }
    for (const auto& expression : builder.expressions()) {
        if (std::holds_alternative<HIRArrayExpr>(expression.value)
            && contains_foreign(expression.type)) {
            report(
                expression.origin,
                "Foreign values cannot be stored in a Carven array",
                DiagnosticCode::TypeForeignEscape
            );
        }
        const auto* closure = std::get_if<HIRClosureExpr>(&expression.value);
        if (closure == nullptr) {
            continue;
        }
        if (contains_callable_view(closure->result)) {
            report(
                expression.origin,
                "non-owning callable view cannot be returned from a lambda",
                DiagnosticCode::TypeCallableViewEscape
            );
        }
        if (contains_foreign(closure->result)) {
            report(
                expression.origin,
                "Foreign value cannot be returned from a lambda",
                DiagnosticCode::TypeForeignEscape
            );
        }
        for (const auto& capture : closure->captures) {
            if (contains_callable_view(capture.type)) {
                report(
                    capture.origin,
                    "non-owning callable view cannot be captured by a lambda",
                    DiagnosticCode::TypeCallableViewEscape
                );
            }
            if (contains_foreign(capture.type)) {
                report(
                    capture.origin,
                    "Foreign value cannot be captured by a lambda",
                    DiagnosticCode::TypeForeignEscape
                );
            }
        }
    }
}
