module carven:backend.target.finalize.impl;

import :backend.target.finalize;
import :backend.target.verify;
import :support.invariant;
import std;

auto TargetUnitFinalizer::finalize(TargetStorage storage, TargetUnitRoot root) noexcept
    -> TargetUnit {
    auto unit = TargetUnit(std::move(storage), std::move(root));
    const auto verification = verify_target_unit(unit);
    if (!verification.has_value()) {
        invariant_violation(verification.error().message);
    }
    return unit;
}
