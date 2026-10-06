module carven:semantic.analysis.expr.sequence;

import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.expr.operand;
import :semantic.analysis.expr.result;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.semir.sequence;
import :semantic.semir.structured;
import :support.invariant;
import std;

template<typename Site>
auto construct_sequence_call(
    Site& site,
    SequenceIntrinsic intrinsic,
    typename Site::Value receiver,
    std::span<const ASTCallArgument> arguments,
    Span span
) noexcept -> ExpressionTask<typename Site::Value> {
    if constexpr (Site::mode == ExpressionMode::StaticRoot) {
        co_return std::unexpected(ExpressionNotAdmitted {});
    } else {
        const auto contract = sequence_intrinsic_contract(intrinsic);
        const auto shape = sequence_shape(site.draft(), site.type(receiver));
        if (!shape || shape->kind != SequenceShapeKind::OwnedSequence) {
            invariant_violation("Sequence call requires an owning sequence receiver");
        }
        auto state = Site::operand_state();
        auto operands = std::vector<SemCallArgument>();
        auto owner = contract.receiver_access == AccessMode::Write
            ? site.consume_write(state, std::move(receiver), span)
            : site.consume_read(state, std::move(receiver), span);
        if (!owner) {
            co_return std::unexpected(owner.error());
        }
        operands.push_back({
            .access = contract.receiver_access,
            .expression = std::move(*owner),
        });
        if (contract.argument) {
            const auto source = arguments.front().expression;
            const auto argument_span = site.syntax().expression(source).span;
            const auto selected = call_argument_operand(site.syntax(), source);
            if (selected.access == AccessMode::Write
                || (selected.access == AccessMode::Take
                    && *contract.argument != SequenceIntrinsicArgument::Element)) {
                co_return std::unexpected(site.fail(
                    argument_span,
                    DiagnosticCode::AccessCallMismatch,
                    "argument access marker differs from the Sequence operation"
                ));
            }
            const auto expected = *contract.argument == SequenceIntrinsicArgument::Element
                ? shape->element
                : ConstructionTypeRef(site.draft().builtin_type(BuiltinType::Usize));
            const auto execution = site.enter_operand_execution(state.completes);
            auto argument = (co_await site.read(selected.expression, expected));
            if (!argument) {
                co_return std::unexpected(argument.error());
            }
            if (auto converted = site.convert_argument(*argument, expected, argument_span);
                !converted) {
                co_return std::unexpected(converted.error());
            }
            auto value = selected.access == AccessMode::Take
                ? site.consume_take(state, std::move(*argument), argument_span)
                : site.consume_read(state, std::move(*argument), argument_span);
            if (!value) {
                co_return std::unexpected(value.error());
            }
            operands.push_back({.access = selected.access, .expression = std::move(*value)});
        }
        co_return site.finish_constructed(
            site.draft().builtin_type(contract.result),
            SemIntrinsic {
                .operation = SequenceIntrinsicOperation {.intrinsic = intrinsic},
                .operands = std::move(operands),
            },
            std::move(state),
            span
        );
    }
}
