module carven:backend.target.verify.impl;

import :backend.target.expr;
import :backend.target.item;
import :backend.target.stmt;
import :backend.target.traversal;
import :backend.target.type;
import :backend.target.unit;
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
    ) noexcept;
    auto run() noexcept -> std::expected<void, TargetSealViolation>;
    auto visit_type(TargetTypeID id) noexcept -> bool;

private:
    auto fail(TargetSealViolationKind kind, std::string message) noexcept -> bool;

    TargetUnitIdentity identity;
    std::span<const TargetType> types;
    const TargetUnitSections& sections;
    std::optional<TargetSealViolation> failure;
};

TargetTypeVerifier::TargetTypeVerifier(
    TargetUnitIdentity unit_identity,
    std::span<const TargetType> type_values,
    const TargetUnitSections& unit_sections
) noexcept
    : identity(unit_identity),
      types(type_values),
      sections(unit_sections) {}

auto TargetTypeVerifier::run() noexcept -> std::expected<void, TargetSealViolation> {
    if (!traverse_target_unit(sections, *this)) {
        return std::unexpected(std::move(*failure));
    }
    return {};
}

auto TargetTypeVerifier::visit_type(TargetTypeID id) noexcept -> bool {
    if (id.owner() != identity || id.index() >= types.size()) {
        return fail(
            TargetSealViolationKind::InvalidTypeReference,
            std::format("target type ID {} is foreign or out of range", id.index())
        );
    }
    return true;
}

auto TargetTypeVerifier::fail(TargetSealViolationKind kind, std::string message) noexcept -> bool {
    failure = TargetSealViolation {.kind = kind, .message = std::move(message)};
    return false;
}

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

auto TargetVerificationInput::identity() const noexcept -> TargetUnitIdentity {
    return unit_identity;
}

auto TargetVerificationInput::types() const noexcept -> std::span<const TargetType> {
    return type_rows;
}

auto TargetVerificationInput::sections() const noexcept -> const TargetUnitSections& {
    return *unit_sections;
}

TargetVerificationInput::TargetVerificationInput(
    TargetUnitIdentity identity,
    std::span<const TargetType> types,
    const TargetUnitSections& sections
) noexcept
    : unit_identity(identity),
      type_rows(types),
      unit_sections(std::addressof(sections)) {}
