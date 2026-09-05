module carven:semantic.analysis.coverage;

import :semantic.analysis.body.builder;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.type;
import std;

struct CoverageRedundantAlternative final {
    std::size_t arm;
    std::size_t alternative;
};

struct PatternCoverageArm final {
    std::vector<std::optional<PatternID>> alternatives;
    bool guarded;
};

struct PatternCoverage final {
    std::vector<bool> arm_usefulness;
    std::vector<std::vector<bool>> alternative_usefulness;
    std::vector<CoverageRedundantAlternative> redundant_alternatives;
    std::vector<bool> exhaustive_after_arm;
    bool exhaustive;
    std::string missing_witness;
};

auto compute_pattern_coverage(
    const ProgramDraft& draft,
    const BodyBuilder& body,
    ConstructionTypeRef subject_type,
    std::span<const PatternCoverageArm> arms
) noexcept -> std::expected<PatternCoverage, std::string>;

auto compute_pattern_coverage(
    const ProgramDraft& draft,
    const SemIRBody& body,
    TypeID subject_type,
    std::span<const PatternCoverageArm> arms
) noexcept -> std::expected<PatternCoverage, std::string>;
