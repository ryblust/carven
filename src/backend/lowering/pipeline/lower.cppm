module carven:backend.lower;

import :backend.generation.plan;
import :backend.target;
import :backend.target.ids;

auto lower_artifact(const PlannedCompilation& compilation, TargetArtifactID artifact_id) noexcept
    -> TargetUnit;
