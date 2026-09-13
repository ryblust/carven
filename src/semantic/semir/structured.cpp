module carven:semantic.semir.structured.impl;

import :semantic.semir.body;
import :semantic.semir.ids;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :support.invariant;
import :support.unique_indirect;
import :support.visit;
import std;

SemIRBody::SemIRBody(SemIRBodyData data) noexcept
    : data(std::move(data)) {
    visit_semantic_nodes(
        this->data.region,
        Overloaded {
            [](const SemanticRegion& region) static noexcept {
                static_cast<void>(region.failures.resolved());
            },
            [](const SemanticExpression& expression) static noexcept {
                static_cast<void>(expression.type.resolved());
                static_cast<void>(expression.failures.resolved());
                if (const auto* call = std::get_if<SemCall>(&expression.value)) {
                    static_cast<void>(call->callee_failures.resolved());
                }
                if (const auto* attempt = std::get_if<SemTry>(&expression.value)) {
                    static_cast<void>(attempt->protected_failures.resolved());
                    static_cast<void>(attempt->residual_failures.resolved());
                    for (const auto& arm : attempt->arms) {
                        static_cast<void>(arm.accepted_failures.resolved());
                        for (const auto& alternative : arm.alternatives) {
                            if (const auto* typed =
                                    std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                                static_cast<void>(typed->type.resolved());
                            }
                        }
                    }
                }
            },
        }
    );
}

BodyType::BodyType(ConstructionTypeRef value) noexcept
    : value(value) {}

BodyType::BodyType(TypeID value) noexcept
    : value(value) {}

BodyType::BodyType(TypeTermID value) noexcept
    : value(value) {}

auto BodyType::construction() const noexcept -> const ConstructionTypeRef& {
    return value;
}

auto BodyType::resolved() const noexcept -> TypeID {
    if (const auto* type = std::get_if<TypeID>(&value)) {
        return *type;
    }
    invariant_violation("body type has not been resolved");
}

BodyFailures::BodyFailures(FailureTermID value) noexcept
    : value(value) {}

BodyFailures::BodyFailures(FailureSetID value) noexcept
    : value(value) {}

auto BodyFailures::term() const noexcept -> FailureTermID {
    if (const auto* term = std::get_if<FailureTermID>(&value)) {
        return *term;
    }
    invariant_violation("resolved body failures have no construction term");
}

auto BodyFailures::resolved() const noexcept -> FailureSetID {
    if (const auto* failures = std::get_if<FailureSetID>(&value)) {
        return *failures;
    }
    invariant_violation("body failures have not been resolved");
}

auto SemanticExpression::selects_storage() const noexcept -> bool {
    return std::holds_alternative<SemBinding>(value)
        || std::holds_alternative<SemField>(value)
        || std::holds_alternative<SemIndex>(value)
        || std::holds_alternative<SemDereference>(value);
}

auto SemIRBody::id() const noexcept -> BodyID {
    return data.id;
}

auto SemIRBody::kind() const noexcept -> BodyKind {
    return data.kind;
}

auto SemIRBody::identity() const noexcept -> BodyIdentity {
    return data.lifetime_regions.owner();
}

auto SemIRBody::provenance_identity() const noexcept -> ProvenanceIdentity {
    return data.provenance_identity;
}

auto SemIRBody::inputs() const noexcept -> const BodyInputs& {
    return data.inputs;
}

auto SemIRBody::lifetime_regions() const noexcept -> const LifetimeRegionTree& {
    return data.lifetime_regions;
}

auto SemIRBody::bindings() const noexcept
    -> IDTableEntries<LocalBindingID, LocalBinding, BodyIdentity> {
    return data.bindings.entries();
}

auto SemIRBody::patterns() const noexcept -> IDTableEntries<PatternID, Pattern, BodyIdentity> {
    return data.patterns.entries();
}

auto SemIRBody::pattern_table() const noexcept -> const ImmutableBodyTable<Pattern, PatternID>& {
    return data.patterns;
}

auto SemIRBody::binding(LocalBindingID id) const noexcept -> const LocalBinding& {
    return data.bindings.get(id);
}

auto SemIRBody::pattern(PatternID id) const noexcept -> const Pattern& {
    return data.patterns.get(id);
}

auto SemIRBody::region() const noexcept -> const SemanticRegion& {
    return data.region;
}
