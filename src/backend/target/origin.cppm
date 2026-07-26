module carven:backend.target.origin;

import std;

struct TargetSourceOrigin final {
    std::string display_origin;
    std::uint32_t line;
};

enum class TargetSyntheticReason {
    ArtifactScaffolding,
    ControlNormalization,
    EvaluationOrder,
    FailureTransport,
    LoweringSupport,
    TestHarness,
};

enum class TargetAttributionKind {
    SourceOwned,
    SourceExpansion,
    CompilerOwned,
    RawSource,
};

struct TargetAttribution final {
    TargetAttributionKind kind;
    std::optional<TargetSourceOrigin> origin;
    std::optional<TargetSyntheticReason> reason;
};

enum class TargetMaterializationReason {
    EvaluationOrder,
    Lifetime,
    ValueCategory,
    Ownership,
    FailureTransport,
    RawBoundary,
};
