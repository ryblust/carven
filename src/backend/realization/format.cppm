module carven:backend.realization.format;

import :backend.lowering.context;
import :backend.preparation.format;
import :backend.target.expr;
import :backend.target.stmt;
import :semantic.semir.structured;
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

// Inputs are stable after ordinary source sequencing. Sizes are observed only
// after every hole completes; these statements never evaluate a source operand twice.
auto realize_writer_statements(
    ModuleLowering& context,
    const WriterFormat& format,
    TargetIdentifier writer,
    TargetExpr output,
    std::vector<TargetExpr> operands,
    std::vector<TargetExpr> text_sizes
) noexcept -> std::vector<TargetStmt>;
