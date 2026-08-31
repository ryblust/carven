module carven:backend.lower;

import :backend.generation.program;
import :backend.target;

auto lower_target_unit(const TargetProgram& program, TargetArtifactID artifact_id) noexcept
    -> TargetUnit;
