module carven:backend.emit;

import :artifacts;
import :backend.target;

auto emit(TargetUnit unit) noexcept -> GeneratedArtifact;
