module carven:semantic.analysis.validation.identity.impl;
import :semantic.analysis.validation.context;
import std;

namespace validation_detail {
auto BodyContractVerifier::related_body(BodyID id) const noexcept -> const SemIRBody& {
    for (const auto& body : bodies) {
        if (body.id() == id) {
            return body;
        }
    }
    invariant_violation("cross-body validation referenced a missing semantic body");
}

auto BodyContractVerifier::require_top_level_owners() const noexcept -> void {
    const auto identity = body.identity();
    if (body.id().owner() != identity.program() || draft->identity() != identity.program()) {
        invariant_violation("SemIR verifier mixed semantic program identities");
    }
    if (body.provenance_identity() != draft->provenance_identity()) {
        invariant_violation("SemIR verifier mixed provenance identities");
    }
    if (body.scopes().owner() != identity || body.lifetime_regions().owner() != identity) {
        invariant_violation("semantic body has foreign scope or lifetime ownership");
    }
}

auto BodyContractVerifier::require_origin(ProgramOriginID origin) const noexcept -> void {
    if (origin.owner() != body.provenance_identity()) {
        invariant_violation("SemIR row uses an origin from another provenance owner");
    }
    static_cast<void>(draft->source_span(origin));
}

auto BodyContractVerifier::require_type(TypeID type) const noexcept -> CanonicalType {
    if (type.owner() != draft->identity()) {
        invariant_violation("SemIR row uses a type from another semantic program");
    }
    return draft->canonical_type_copy(type);
}

auto BodyContractVerifier::require_failure_set(FailureSetID failures) const noexcept -> FailureSet {
    if (failures.owner() != draft->identity()) {
        invariant_violation("SemIR row uses a failure set from another semantic program");
    }
    return draft->failure_set_copy(failures);
}

auto BodyContractVerifier::require_structure(StructID structure) const noexcept
    -> ConstructionStructDeclaration {
    if (structure.owner() != draft->identity()) {
        invariant_violation("SemIR row uses a structure from another semantic program");
    }
    return draft->construction_struct_declaration_copy(structure);
}

auto BodyContractVerifier::require_enumeration(EnumID enumeration) const noexcept
    -> ConstructionEnumDeclaration {
    if (enumeration.owner() != draft->identity()) {
        invariant_violation("SemIR row uses an enum from another semantic program");
    }
    return draft->construction_enum_declaration_copy(enumeration);
}

auto BodyContractVerifier::require_enum_case(EnumCaseID enum_case) const noexcept
    -> ConstructionEnumCaseDeclaration {
    if (enum_case.owner() != draft->identity()) {
        invariant_violation("SemIR row uses an enum case from another semantic program");
    }
    return draft->construction_enum_case_declaration_copy(enum_case);
}

auto BodyContractVerifier::require_nominal_failure_member(TypeID type) const noexcept -> void {
    const auto canonical = require_type(type);
    if (!std::holds_alternative<StructTypeValue>(canonical.value)
        && !std::holds_alternative<EnumTypeValue>(canonical.value)) {
        invariant_violation("Throw failure member is not a nominal structure or enum");
    }
}

auto BodyContractVerifier::body_callable() const noexcept -> std::optional<CallableID> {
    const auto callable = draft->callable_for_body(body.id());
    if (body.kind() == BodyKind::Test) {
        if (callable.has_value()) {
            invariant_violation("test body is owned by a callable declaration");
        }
        return std::nullopt;
    }
    if (!callable.has_value()) {
        invariant_violation("function or closure body has no callable declaration");
    }
    return callable;
}

auto BodyContractVerifier::concrete_failures(FailureTermID failures) const noexcept
    -> FailureSetID {
    return draft->concrete_failure_set(failures);
}

auto BodyContractVerifier::verify_body_inputs() const noexcept -> void {
    const auto callable = body_callable();
    if (!callable.has_value()) {
        if (!body.inputs().parameters.empty() || !body.inputs().captures.empty()) {
            invariant_violation("test body has language parameters or captures");
        }
        return;
    }
    const auto contract = draft->construction_callable_contract_copy(*callable);
    if (body.inputs().parameters.size() != contract.parameters.size()) {
        invariant_violation("BodyInputs parameters differ from callable contract arity");
    }
    for (auto index = 0uz; index < contract.parameters.size(); ++index) {
        const auto& binding = body.binding(body.inputs().parameters[index]);
        const auto* storage = std::get_if<ParameterBindingStorage>(&binding.storage);
        const auto& parameter = contract.parameters[index];
        if (storage == nullptr
            || storage->access != parameter.access
            || binding.type != draft->concrete_type(parameter.type)) {
            invariant_violation("BodyInputs parameter differs from callable contract");
        }
    }
    if (body.kind() == BodyKind::Function && !body.inputs().captures.empty()) {
        invariant_violation("ordinary function body has closure captures");
    }
    if (body.kind() == BodyKind::Closure
        && std::ranges::any_of(body.inputs().captures, [&](LocalBindingID binding) noexcept {
               return !std::holds_alternative<CaptureBindingStorage>(body.binding(binding).storage);
           })) {
        invariant_violation("closure BodyInputs contains a non-capture binding");
    }
}

auto BodyContractVerifier::require_body_failure_set(FailureSetID failures) const noexcept -> void {
    const auto callable = body_callable();
    if (!callable.has_value()) {
        invariant_violation("test body exposes a failure contract");
    }
    const auto contract = draft->construction_callable_contract_copy(*callable);
    const auto expected = concrete_failures(contract.failures);
    const auto actual = require_failure_set(failures);
    const auto allowed = require_failure_set(expected);
    if (!std::ranges::includes(
            allowed.members,
            actual.members,
            {},
            &TypeID::index,
            &TypeID::index
        )) {
        invariant_violation(
            std::format(
                "body {} failure exit {} exceeds callable {} contract {}",
                body.id().index(),
                failures.index(),
                callable->index(),
                expected.index()
            )
        );
    }
}

auto BodyContractVerifier::verify_trees() const noexcept -> void {
    for (const auto [id, scope] : body.scopes().entries()) {
        require_origin(scope.origin);
        if (scope.parent.has_value()
            && (!body.scopes().contains(*scope.parent) || scope.parent->index() >= id.index())) {
            invariant_violation("lexical scope tree contains a forward edge or cycle");
        }
    }
    for (const auto [id, region] : body.lifetime_regions().entries()) {
        require_origin(region.origin);
        if (region.parent.has_value()
            && (!body.lifetime_regions().contains(*region.parent)
                || region.parent->index() >= id.index())) {
            invariant_violation("lifetime region tree contains a forward edge or cycle");
        }
    }
}

} // namespace validation_detail
