module carven:semantic.analysis.validation.surface.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.program;
import :semantic.analysis.validation;
import :semantic.semir.constant;
import :semantic.semir.structured;
import :semantic.visibility;
import :source.module_path;
import :support.visit;
import std;

namespace {

class DeclarationSurfaceValidator final {
public:
    DeclarationSurfaceValidator(
        ProgramDraft& target,
        DeclarationVisibility visibility,
        ModuleID module_id,
        ProgramOriginID origin,
        std::string_view description
    ) noexcept
        : draft(target),
          surface_visibility(visibility),
          surface_module(module_id),
          primary(origin),
          description(description) {}

    auto check(TypeID type) noexcept -> AnalysisResult<void> {
        validate(type);
        return failure.has_value() ? AnalysisResult<void>(std::unexpected(*failure))
                                   : AnalysisResult<void>();
    }

    auto check_constant(ConstantID id) noexcept -> AnalysisResult<void> {
        validate_constant(id);
        return failure.has_value() ? AnalysisResult<void>(std::unexpected(*failure))
                                   : AnalysisResult<void>();
    }

private:
    auto validate(TypeID type) noexcept -> void {
        if (surface_visibility == DeclarationVisibility::Module
            || !visited_types.insert(type).second) {
            return;
        }
        std::visit(
            Overloaded {
                [](const BuiltinTypeValue&) static noexcept {},
                [&](const StructTypeValue& value) noexcept {
                    validate_nominal(draft.declarations().structure(value.structure));
                },
                [&](const EnumTypeValue& value) noexcept {
                    validate_nominal(draft.declarations().enumeration(value.enumeration));
                },
                [&](const ArrayTypeValue& value) noexcept { validate(value.element); },
                [&](const FunctionTypeValue& value) noexcept {
                    validate_signature(draft.declarations().callable(value.callable).signature);
                },
                [&](const ClosureTypeValue& value) noexcept {
                    validate_signature(draft.declarations().callable(value.callable).signature);
                    const auto& body = draft.bodies().body(
                        *draft.declarations().body_for_callable(value.callable)
                    );
                    for (const auto capture : body.inputs().captures) {
                        validate(body.binding(capture).type);
                    }
                },
                [&](const CallableViewTypeValue& value) noexcept {
                    validate_signature(value.signature);
                },
                [&](const CppTypeValue& value) noexcept {
                    for (const auto referenced : cpp_type_references(value)) {
                        validate(referenced);
                    }
                },
            },
            draft.types().type(type).value
        );
    }

    auto validate_signature(CallableSignatureID id) noexcept -> void {
        if (!visited_signatures.insert(id).second) {
            return;
        }
        const auto& signature = draft.callable_signatures().signature(id);
        for (const auto& parameter : signature.parameters) {
            validate(parameter.type);
        }
        validate(signature.result);
        for (const auto member : draft.failure_sets().failure_set(signature.failures).members) {
            validate(member);
        }
    }

    auto validate_constant(ConstantID id) noexcept -> void {
        if (!visited_constants.insert(id).second) {
            return;
        }
        const auto& constant = draft.constants().constant(id);
        validate(constant.type);
        if (const auto* payload = std::get_if<PayloadEnumConstant>(&constant.value)) {
            for (const auto child : payload->payload) {
                validate_constant(child);
            }
        }
    }

    auto validate_nominal(const auto& nominal) noexcept -> void {
        const auto allowed = nominal.visibility == DeclarationVisibility::Compilation
            || (surface_visibility == DeclarationVisibility::ModuleDomain
                && nominal.visibility == DeclarationVisibility::ModuleDomain
                && same_module_domain(
                    draft.module_path_copy(
                        draft.declarations().module_decl(surface_module).provenance_module
                    ),
                    draft.module_path_copy(
                        draft.declarations().module_decl(nominal.module_id).provenance_module
                    )
                ));
        if (!allowed) {
            failure = draft.diagnostics().error(
                DiagnosticBuilder(
                    DiagnosticCode::TypeVisibilityLeak,
                    std::format("{} references a declaration with narrower visibility", description)
                )
                    .primary(draft.source_span(primary))
                    .related(draft.source_span(nominal.origin), "referenced declaration")
                    .build()
            );
        }
    }

    ProgramDraft& draft;
    DeclarationVisibility surface_visibility;
    ModuleID surface_module;
    ProgramOriginID primary;
    std::string_view description;
    std::optional<AnalysisFailure> failure;
    std::flat_set<TypeID> visited_types;
    std::flat_set<CallableSignatureID> visited_signatures;
    std::flat_set<ConstantID> visited_constants;
};

} // namespace

auto validate_declaration_surfaces(ProgramDraft& draft) noexcept -> AnalysisResult<void> {
    auto failure = std::optional<AnalysisFailure>();
    const auto retain_failure = [&](AnalysisResult<void> checked) noexcept {
        if (!checked.has_value() && !failure.has_value()) {
            failure = checked.error();
        }
    };
    for (const auto& function : draft.declarations().functions()) {
        const auto& signature = draft.callable_signatures().signature(
            draft.declarations().callable(function.value.callable).signature
        );
        const auto body_id = draft.declarations().body_for_callable(function.value.callable);
        for (const auto [index, parameter] : std::views::enumerate(signature.parameters)) {
            const auto origin = body_id.has_value()
                ? draft.bodies()
                      .body(*body_id)
                      .binding(draft.bodies().body(*body_id).inputs().parameters[index])
                      .origin
                : function.value.origin;
            auto validator = DeclarationSurfaceValidator(
                draft,
                function.value.visibility,
                function.value.module_id,
                origin,
                "function parameter"
            );
            retain_failure(validator.check(parameter.type));
        }
        auto result = DeclarationSurfaceValidator(
            draft,
            function.value.visibility,
            function.value.module_id,
            function.value.origin,
            "function result"
        );
        retain_failure(result.check(signature.result));
        auto failures = DeclarationSurfaceValidator(
            draft,
            function.value.visibility,
            function.value.module_id,
            function.value.origin,
            "function failure"
        );
        for (const auto member : draft.failure_sets().failure_set(signature.failures).members) {
            retain_failure(failures.check(member));
        }
    }
    for (const auto& structure : draft.declarations().structures()) {
        for (const auto& field : structure.value.fields) {
            auto validator = DeclarationSurfaceValidator(
                draft,
                structure.value.visibility,
                structure.value.module_id,
                field.origin,
                "struct field"
            );
            retain_failure(validator.check(field.type));
        }
    }
    for (const auto& enumeration : draft.declarations().enumerations()) {
        auto validator = DeclarationSurfaceValidator(
            draft,
            enumeration.value.visibility,
            enumeration.value.module_id,
            enumeration.value.origin,
            "enum underlying type"
        );
        if (const auto* numeric =
                std::get_if<NumericEnumRepresentation>(&enumeration.value.representation)) {
            retain_failure(validator.check(numeric->underlying_type));
        }
        for (const auto case_id : enumeration.value.cases) {
            const auto& enum_case = draft.declarations().enum_case(case_id);
            auto payload = DeclarationSurfaceValidator(
                draft,
                enumeration.value.visibility,
                enumeration.value.module_id,
                enum_case.origin,
                "enum payload"
            );
            for (const auto type : enum_case.payload_types) {
                retain_failure(payload.check(type));
            }
        }
    }
    for (const auto& constant : draft.declarations().module_constants()) {
        auto validator = DeclarationSurfaceValidator(
            draft,
            constant.value.visibility,
            constant.value.module_id,
            constant.value.origin,
            "module constant"
        );
        retain_failure(validator.check_constant(constant.value.value));
    }
    return failure.has_value() ? AnalysisResult<void>(std::unexpected(*failure))
                               : AnalysisResult<void>();
}
