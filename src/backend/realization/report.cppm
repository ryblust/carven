module carven:backend.realization.report;

import :backend.lowering.context;
import :backend.target.expr;
import :semantic.semir.structured;
import std;

auto realize_observed_comparison(
    ModuleLowering& context,
    const SemBinary& operation,
    std::vector<TargetExpr> operands,
    TargetLocalID writer,
    std::array<ProgramSpellingID, 2> sources
) noexcept -> TargetExpr;

auto realize_observed_short_circuit(
    const ModuleLowering& context,
    TargetLocalID writer,
    std::array<ProgramSpellingID, 2> sources,
    bool left,
    std::optional<TargetExpr> right
) noexcept -> TargetExpr;
