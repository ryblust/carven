module carven:backend.lowering.patterns;

import :backend.lowering.program;
import :backend.target.ids;
import :backend.target.name;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.ids;
import :semantic.hir.stmt;
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

auto pattern_requires_subject(TargetCallableLowerer& context, HIRPatternID pattern_id) noexcept
    -> bool;

auto lower_pattern(
    TargetCallableLowerer& context,
    std::optional<TargetExprID> subject_occurrence_id,
    HIRPatternID pattern_id
) noexcept -> std::vector<LoweredPattern>;

auto materialize_pattern_bindings(
    TargetCallableLowerer& context,
    std::span<const LoweredPatternBinding> bindings
) noexcept -> std::vector<TargetStmtID>;

auto lower_statement_match(
    TargetCallableLowerer& context,
    const HIRMatchStmt& match,
    const TargetControlDestinations& control
) noexcept -> std::vector<TargetStmtID>;

auto lower_value_match(
    TargetCallableLowerer& context,
    const HIRMatchExpr& match,
    const TargetControlDestinations& control
) noexcept -> std::vector<TargetStmtID>;

auto lower_outcome_match(
    TargetCallableLowerer& context,
    const HIRMatchExpr& match,
    const TargetControlDestinations& control,
    HIRTypeID result_type_id,
    FailureSetID failure_set_id
) noexcept -> std::vector<TargetStmtID>;
