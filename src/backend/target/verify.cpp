module carven:backend.target.verify.impl;

import :backend.target.passes;
import :backend.target.verify;

auto validate_target_unit(TargetUnitValidationView unit) noexcept
    -> std::expected<void, TargetUnitViolation> {
    const auto integrity = validate_target_integrity(unit);
    if (!integrity.has_value()) {
        return integrity;
    }
    return validate_jumps(unit);
}
