module carven:backend.target.verify;

import :backend.target;
import std;

struct TargetUnitError final {
    std::string message;
};

auto verify_target_unit(const TargetUnit& unit) noexcept -> std::expected<void, TargetUnitError>;
