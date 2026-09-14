module carven:backend.realization.format;

import :backend.lowering.context;
import :backend.preparation.format;
import :backend.target.expr;
import :semantic.semir;
import std;

// Operands have completed the ordinary Read/Write preparation. Parsed fields
// describe the retained pack; no source-format parsing occurs in realization.
auto realize_format(
    ModuleLowering& context,
    const SemanticExpression& source,
    const SemFormat& value,
    const PreparedFormat& preparation,
    std::vector<TargetExpr> operands
) noexcept -> TargetExpr;
