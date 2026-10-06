module carven:semantic.analysis.body.bindings.impl;

import :semantic.analysis.body.builder;
import :support.invariant;
import std;

auto BodyBuilder::add_binding(
    ProgramSpellingID name,
    ConstructionTypeRef type,
    LifetimeRegionID lifetime,
    BindingStorage storage,
    ProgramOriginID origin
) noexcept -> BoundStorage {
    const auto binding = bindings.add({name, type, lifetime, storage, origin});
    return {.binding = binding};
}

auto BodyBuilder::add_parameter(
    ProgramSpellingID name,
    ConstructionTypeRef type,
    LifetimeRegionID lifetime,
    AccessMode access,
    ProgramOriginID origin
) noexcept -> BoundStorage {
    if (body_kind == BodyKind::Test) {
        invariant_violation("test body cannot receive callable parameters");
    }
    const auto result = add_binding(name, type, lifetime, ParameterBindingStorage {access}, origin);
    body_inputs.parameters.push_back(result.binding);
    return result;
}

auto BodyBuilder::add_capture(
    ProgramSpellingID name,
    ConstructionTypeRef type,
    LifetimeRegionID lifetime,
    CaptureMode mode,
    ProgramOriginID origin
) noexcept -> BoundStorage {
    if (body_kind != BodyKind::Closure) {
        invariant_violation("only closure bodies can receive capture bindings");
    }
    const auto result = add_binding(name, type, lifetime, CaptureBindingStorage {mode}, origin);
    body_inputs.captures.push_back(result.binding);
    return result;
}

auto BodyBuilder::add_owner_binding(
    ProgramSpellingID name,
    ConstructionTypeRef type,
    LifetimeRegionID lifetime,
    bool writable,
    ProgramOriginID origin
) noexcept -> BoundStorage {
    return add_binding(name, type, lifetime, OwnerBindingStorage {writable}, origin);
}

auto BodyBuilder::add_pattern(ElaboratedPattern pattern) noexcept -> PatternID {
    return patterns.add(std::move(pattern));
}

auto BodyBuilder::add_alias_binding(
    ProgramSpellingID name,
    ConstructionTypeRef type,
    LifetimeRegionID lifetime,
    AccessMode access,
    ProgramOriginID origin
) noexcept -> BoundStorage {
    if (access == AccessMode::Take) {
        invariant_violation("an alias binding cannot own a taken value");
    }
    return add_binding(name, type, lifetime, AliasBindingStorage {.access = access}, origin);
}

auto BodyBuilder::binding_copy(LocalBindingID id) const noexcept -> ElaboratedLocalBinding {
    if (!bindings.contains(id)) {
        invariant_violation("body builder queried a foreign or invalid binding");
    }
    return bindings.copy(id);
}

auto BodyBuilder::pattern_copy(PatternID id) const noexcept -> ElaboratedPattern {
    if (!patterns.contains(id)) {
        invariant_violation("body builder queried a foreign or invalid pattern");
    }
    return patterns.copy(id);
}
