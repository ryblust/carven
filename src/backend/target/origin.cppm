module carven:backend.target.origin;

import std;

struct TargetSourceOrigin final {
    std::string display_origin;
    std::uint32_t line;
};

enum class TargetExpansionReason {
    EvaluationOrder,
    FailureTransport,
    LoweringSupport,
};

enum class TargetCompilerReason {
    ArtifactScaffolding,
    TestHarness,
};

struct TargetSourceOwnedAttribution final {
    TargetSourceOrigin origin;
};

struct TargetSourceExpansionAttribution final {
    TargetSourceOrigin origin;
};

struct TargetGeneratedExpansionAttribution final {
    TargetExpansionReason reason;
};

struct TargetCompilerOwnedAttribution final {
    TargetCompilerReason reason;
};

struct TargetRawSourceAttribution final {
    TargetSourceOrigin origin;
};

using TargetAttribution = std::variant<
    TargetSourceOwnedAttribution,
    TargetSourceExpansionAttribution,
    TargetGeneratedExpansionAttribution,
    TargetCompilerOwnedAttribution,
    TargetRawSourceAttribution>;
