module carven:semantic.analysis.coverage;

import :semantic.analysis.declaration_construction;
import :semantic.hir;
import :semantic.hir.expr;
import :semantic.hir.ids;
import std;

struct CoverageDuplicateAlternative final {
    std::size_t arm;
    std::size_t alternative;
};

struct PatternCoverage final {
    std::vector<bool> arm_usefulness;
    std::vector<CoverageDuplicateAlternative> duplicate_alternatives;
    bool exhaustive;
    std::string missing_witness;
};

auto compute_pattern_coverage(
    const SemanticConstruction& hir,
    HIRTypeID subject_type,
    std::span<const HIRMatchArm> arms
) noexcept -> std::expected<PatternCoverage, std::string>;

auto compute_pattern_coverage(
    const SemanticConstruction& hir,
    const DeclarationSessionView& declarations,
    HIRTypeID subject_type,
    std::span<const HIRMatchArm> arms
) noexcept -> std::expected<PatternCoverage, std::string>;
