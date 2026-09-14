module carven:semantic.analysis.constant.literal;

import :frontend.ast.literal;
import :semantic.analysis.program;
import :semantic.evaluation.operation;
import :semantic.semir.constant;
import :semantic.semir.type;
import std;

enum class LiteralSign {
    Positive,
    Negative,
};

auto normalize_literal(
    ProgramDraft& draft,
    const ASTLiteral& literal,
    std::optional<ConstructionTypeRef> expected = std::nullopt,
    LiteralSign sign = LiteralSign::Positive
) noexcept -> std::expected<ConstantFact, ConstantEvaluationFailure>;
