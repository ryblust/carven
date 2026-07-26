module carven:backend.target.finalize;

import :backend.target;
import :backend.target.unit;

class TargetUnitFinalizer final {
public:
    static auto finalize(TargetStorage storage, TargetUnitRoot root) noexcept -> TargetUnit;
};
