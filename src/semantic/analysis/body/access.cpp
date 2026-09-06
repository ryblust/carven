module carven:semantic.analysis.body.access.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.context;
import :semantic.analysis.body.expression_site;
import :semantic.analysis.body.pipeline;
import :semantic.analysis.body.resolve;
import :semantic.analysis.constant.evaluate;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.interpret;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

namespace body_elaboration {


auto BodyElaborator::access_expression(const ASTAccessExpr& source, Span span) noexcept
    -> AnalysisResult<BuiltExpression> {
    auto operand = expression(source.operand_id);
    if (!operand.has_value()) {
        return std::unexpected(operand.error());
    }
    auto pending_failures = take_pending(*operand);
    const auto access = [&]() noexcept {
        switch (source.mode) {
            case ASTAccessMode::Read:  return AccessMode::Read;
            case ASTAccessMode::Write: return AccessMode::Write;
            case ASTAccessMode::Take:  return AccessMode::Take;
        }
        std::unreachable();
    }();
    if (access == AccessMode::Write) {
        auto place = consume_place(*operand, span);
        if (!place.has_value()) {
            return std::unexpected(place.error());
        }
        operand->storage = std::move(*place);
        operand->pending_failures = std::move(pending_failures);
        return std::move(*operand);
    }
    auto value =
        consume_value(*operand, access == AccessMode::Take ? source.marker_span : span, access);
    if (!value.has_value()) {
        return std::unexpected(value.error());
    }
    operand->storage = std::move(*value);
    operand->pending_failures = std::move(pending_failures);
    return std::move(*operand);
}


} // namespace body_elaboration
