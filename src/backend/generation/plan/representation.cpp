module carven:backend.generation.plan.representation.impl;

import :backend.generation.plan;
import :semantic.semir.content;
import :support.invariant;
import std;

auto plan_failure_abi(const SemIRProgram& semantic) noexcept -> FailureABI {
    auto mapping = std::vector<std::vector<TypeID>>(semantic.failure_sets().size());
    auto identities = std::map<TypeID, std::string>();
    for (const auto entry : semantic.failure_sets().entries()) {
        auto members = entry.value.members;
        for (const auto type : members) {
            const auto& value = semantic.types().type(type).value;
            if (!std::holds_alternative<StructTypeValue>(value)
                && !std::holds_alternative<EnumTypeValue>(value)) {
                invariant_violation("failure set contains a non-nominal type");
            }
            if (!identities.contains(type)) {
                identities.emplace(type, type_content_key(semantic, type));
            }
        }
        std::ranges::sort(members, [&](TypeID left, TypeID right) noexcept {
            return identities.at(left) < identities.at(right);
        });
        for (const auto [previous, current] : members | std::views::adjacent<2>) {
            if (identities.at(previous) == identities.at(current)) {
                invariant_violation("distinct failure nominals have the same target identity");
            }
        }
        mapping[entry.id.index()] = std::move(members);
    }
    return FailureABI(semantic.identity(), std::move(mapping));
}
