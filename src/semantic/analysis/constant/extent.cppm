module carven:semantic.analysis.constant.extent;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.storage;
import :semantic.analysis.diagnostics;
import :semantic.analysis.program;
import :semantic.semir.constant;
import :source.text;
import std;

template<typename Scope>
auto evaluate_array_extent(
    ProgramDraft& draft,
    ASTView syntax,
    Scope& scope,
    ASTExprID expression
) noexcept -> AnalysisTask<std::uint64_t> {
    auto value = co_await scope.evaluate_constant_expression(expression);
    if (!value) {
        co_return std::unexpected(value.error());
    }
    const auto* integer =
        *value ? std::get_if<IntegerConstant>(&draft.constant(**value).value) : nullptr;
    const auto fail = [&](Span span, DiagnosticCode code, std::string message) noexcept {
        return draft.diagnostics().error(DiagnosticBuilder(code, std::move(message))
                                             .primary(locate(syntax.source_id(), span))
                                             .build());
    };
    const auto span = syntax.expression(expression).span;
    if (integer == nullptr) {
        co_return std::unexpected(
            fail(span, DiagnosticCode::ConstArrayExtent, "array extent must be a constant integer")
        );
    }
    if (integer->negative()) {
        co_return std::unexpected(
            fail(span, DiagnosticCode::ConstNegativeArrayExtent, "array extent cannot be negative")
        );
    }
    co_return integer->magnitude();
}
