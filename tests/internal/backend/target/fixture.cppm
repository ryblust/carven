module carven:test.internal.backend.target.fixture;

import :backend.target.builder;
import :backend.target.ids;
import :backend.target.type;
import :backend.target.unit;
import :backend.target.verify;
import std;

class TargetTestingFixture final {
public:
    static auto unit_builder() noexcept -> TargetUnitBuilder { return TargetUnitBuilder(); }

    static auto unit_identity() noexcept -> TargetUnitIdentity {
        return TargetUnitIdentity::fresh();
    }

    static auto type_id(TargetUnitIdentity owner, std::uint32_t index) noexcept -> TargetTypeID {
        return TargetTypeID(owner, index);
    }

    static auto validate_unit(
        TargetUnitIdentity owner,
        std::span<const TargetType> types,
        const TargetUnitSections& sections
    ) noexcept -> std::expected<void, TargetSealViolation> {
        return validate_target_unit(TargetVerificationInput(owner, types, sections));
    }

    static auto plan_identity() noexcept -> TargetPlanIdentity {
        return TargetPlanIdentity::fresh();
    }
};
