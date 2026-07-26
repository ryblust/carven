module carven:backend.lowering.patterns;

import :backend.lowering.program;
import :backend.target.ids;
import :backend.target.name;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.ids;
import std;

struct LoweredPatternBinding final {
    SymbolID symbol;
    TargetIdentifier name;
    TargetTypeID type;
    TargetExprID initializer;
};

struct LoweredPattern final {
    std::optional<TargetExprID> condition;
    std::vector<LoweredPatternBinding> bindings;
};

auto enumeration_for_case(const TargetCallableLowerer& context, EnumCaseID enum_case) noexcept
    -> std::pair<const HIREnumDecl*, std::size_t>;
auto pattern_requires_subject(TargetCallableLowerer& context, HIRPatternID id) noexcept -> bool;
auto lower_pattern(TargetCallableLowerer& context, TargetExprID subject, HIRPatternID id) noexcept
    -> std::vector<LoweredPattern>;
auto materialize_pattern_bindings(
    TargetCallableLowerer& context,
    std::span<const LoweredPatternBinding> bindings
) noexcept -> std::vector<TargetStmtID>;
auto lower_statement_match(
    TargetCallableLowerer& context,
    HIRExprID subject,
    std::span<const HIRMatchArm> arms,
    const TargetControlDestinations& control
) noexcept -> std::vector<TargetStmtID>;
auto lower_value_match(
    TargetCallableLowerer& context,
    HIRExprID subject,
    std::span<const HIRMatchArm> arms,
    const TargetControlDestinations& control
) noexcept -> std::vector<TargetStmtID>;
auto lower_outcome_match(
    TargetCallableLowerer& context,
    HIRExprID subject,
    std::span<const HIRMatchArm> arms,
    const TargetControlDestinations& control,
    HIRTypeID result,
    FailureSetID failure_set
) noexcept -> std::vector<TargetStmtID>;
