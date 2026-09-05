module carven:backend.target.verify.impl;

import :backend.target.traversal;
import :backend.target.verify;
import :support.visit;
import std;

namespace {

class TargetTypeVerifier final {
public:
    TargetTypeVerifier(
        TargetUnitIdentity unit_identity,
        std::span<const TargetType> type_values,
        const TargetUnitSections& unit_sections
    ) noexcept
        : identity(unit_identity),
          types(type_values),
          sections(unit_sections) {}

    auto run() noexcept -> std::expected<void, TargetSealViolation> {
        if (!traverse_target_unit(sections, *this)) {
            return std::unexpected(std::move(*failure));
        }
        return {};
    }

    auto visit_type(TargetTypeID id) noexcept -> bool {
        if (id.owner() != identity || id.index() >= types.size()) {
            return fail(
                TargetSealViolationKind::InvalidTypeReference,
                std::format("target type ID {} is foreign or out of range", id.index())
            );
        }
        return true;
    }

private:
    auto fail(TargetSealViolationKind kind, std::string message) noexcept -> bool {
        failure = TargetSealViolation {.kind = kind, .message = std::move(message)};
        return false;
    }

    TargetUnitIdentity identity;
    std::span<const TargetType> types;
    const TargetUnitSections& sections;
    std::optional<TargetSealViolation> failure;
};

auto validate_target_types(
    TargetUnitIdentity identity,
    std::span<const TargetType> types,
    const TargetUnitSections& sections
) noexcept -> std::expected<void, TargetSealViolation> {
    return TargetTypeVerifier(identity, types, sections).run();
}

} // namespace

auto validate_target_unit(const TargetVerificationInput& input) noexcept
    -> std::expected<void, TargetSealViolation> {
    const auto type_result =
        validate_target_types(input.identity(), input.types(), input.sections());
    if (!type_result.has_value()) {
        return type_result;
    }
    return validate_jumps(input.sections());
}
