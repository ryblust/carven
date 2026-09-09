module carven:semantic.analysis.body.bindings.impl;

import :semantic.analysis.body.builder;
import :semantic.analysis.operations;
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

auto BodyBuilder::pattern_copy(PatternID id) const noexcept -> ElaboratedPattern {
    if (!patterns.contains(id)) {
        invariant_violation("body builder queried a foreign or invalid pattern");
    }
    return patterns.copy(id);
}

auto BodyBuilder::place_access(const PlaceExpression& place) const noexcept -> AccessMode {
    if (!place.root) {
        const auto* source = &place.expression;
        for (;;) {
            if (const auto* dereference = std::get_if<SemDereference>(&source->value)) {
                const auto pointer = pointer_shape(draft, dereference->source->type.construction());
                if (!pointer) {
                    invariant_violation("indirect place lost its ptr type");
                }
                return pointer->access == PointerAccess::Write ? AccessMode::Write
                                                               : AccessMode::Read;
            }
            if (const auto* field = std::get_if<SemField>(&source->value)) {
                source = &*field->source;
            } else if (const auto* index = std::get_if<SemIndex>(&source->value)) {
                source = &*index->source;
            } else if (const auto* cpp = std::get_if<SemCpp>(&source->value)) {
                source = &cpp->operands.front().expression;
            } else {
                invariant_violation("indirect place has no dereference root");
            }
        }
    }
    return std::visit(
        [](const auto& storage) static noexcept -> AccessMode {
            using Storage = std::remove_cvref_t<decltype(storage)>;
            if constexpr (std::same_as<Storage, OwnerBindingStorage>) {
                return storage.writable ? AccessMode::Write : AccessMode::Read;
            } else if constexpr (std::same_as<Storage, ParameterBindingStorage>) {
                return storage.access == AccessMode::Write ? AccessMode::Write : AccessMode::Read;
            } else {
                return storage.mode == CaptureMode::Write ? AccessMode::Write : AccessMode::Read;
            }
        },
        bindings.copy(*place.root).storage
    );
}
