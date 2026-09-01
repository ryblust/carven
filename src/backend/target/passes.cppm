module carven:backend.target.passes;

import :backend.target.verify;
import std;

auto validate_target_integrity(TargetUnitValidationView unit) noexcept
    -> std::expected<void, TargetUnitViolation>;

auto validate_jumps(TargetUnitValidationView unit) noexcept
    -> std::expected<void, TargetUnitViolation>;
