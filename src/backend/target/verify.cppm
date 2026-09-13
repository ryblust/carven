module carven:backend.target.verify;

import :backend.target.expr;
import :backend.target.item;
import :backend.target.stmt;
import :backend.target.type;
import :backend.target.unit;
import std;

class TargetTestingFixture;
class TargetUnitBuilder;

enum class TargetSealViolationKind {
    InvalidTypeReference,
    InvalidControl,
};

struct TargetSealViolation final {
    TargetSealViolationKind kind;
    std::string message;
};

class TargetVerificationInput final {
public:
    auto identity() const noexcept -> TargetUnitIdentity;
    auto types() const noexcept -> std::span<const TargetType>;
    auto sections() const noexcept -> const TargetUnitSections&;

private:
    TargetVerificationInput(
        TargetUnitIdentity identity,
        std::span<const TargetType> types,
        const TargetUnitSections& sections
    ) noexcept;

    TargetUnitIdentity unit_identity;
    std::span<const TargetType> type_rows;
    const TargetUnitSections* unit_sections;

    friend class TargetTestingFixture;
    friend class TargetUnitBuilder;
};

auto validate_target_unit(const TargetVerificationInput& input) noexcept
    -> std::expected<void, TargetSealViolation>;

auto validate_jumps(const TargetUnitSections& sections) noexcept
    -> std::expected<void, TargetSealViolation>;
