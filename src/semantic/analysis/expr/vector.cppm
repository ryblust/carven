module carven:semantic.analysis.expr.vector;

import :diagnostics.code;
import :frontend.ast.expr;
import :frontend.ast.storage;
import :semantic.analysis.diagnostics;
import :semantic.analysis.expr.operand;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.semir.structured;
import :semantic.semir.vector;
import :support.invariant;
import std;

template<typename Site>
auto construct_vector_call(
    Site& site,
    VectorIntrinsic intrinsic,
    typename Site::Value receiver,
    std::span<const ASTExprID> arguments,
    Span span
) noexcept -> AnalysisTask<typename Site::Value> {
    const auto contract = vector_intrinsic_contract(intrinsic);
    const auto receiver_type = site.type(receiver);
    const auto shape = sequence_shape(site.draft(), receiver_type);
    if (!shape || shape->kind != SequenceShapeKind::Vector) {
        invariant_violation("Vector call requires an owning sequence receiver");
    }
    auto state = Site::operand_state();
    auto operands = std::vector<SemCallArgument>();
    auto owner = contract.receiver_access == AccessMode::Write
        ? site.consume_write(state, std::move(receiver), span)
        : site.consume_read_argument(state, std::move(receiver), span);
    if (!owner) {
        co_return std::unexpected(owner.error());
    }
    operands.push_back({
        .access = contract.receiver_access,
        .expression = std::move(*owner),
    });
    if (contract.argument) {
        const auto source = arguments.front();
        const auto argument_span = site.syntax().expression(source).span;
        const auto selected = call_argument_operand(site.syntax(), source);
        if (selected.access == AccessMode::Write
            || (selected.access == AccessMode::Take
                && *contract.argument != VectorIntrinsicArgument::Element)) {
            co_return std::unexpected(site.fail(
                argument_span,
                DiagnosticCode::AccessCallMismatch,
                "argument access marker differs from the Vector operation"
            ));
        }
        const auto expected = *contract.argument == VectorIntrinsicArgument::Index
            ? std::optional<ConstructionTypeRef>()
            : std::optional(
                  *contract.argument == VectorIntrinsicArgument::Element
                      ? shape->element
                      : ConstructionTypeRef(site.draft().builtin_type(BuiltinType::Usize))
              );
        const auto execution = site.enter_operand_execution(state.completes);
        auto argument = (co_await site.read(selected.expression, expected));
        if (!argument) {
            co_return std::unexpected(argument.error());
        }
        if (expected) {
            if (auto converted = site.convert_argument(*argument, *expected, argument_span);
                !converted) {
                co_return std::unexpected(converted.error());
            }
        } else {
            const auto index_type = site.type(*argument);
            const auto* concrete = std::get_if<TypeID>(&index_type);
            const auto canonical =
                concrete ? std::optional(site.draft().type_copy(*concrete)) : std::nullopt;
            const auto* builtin =
                canonical ? std::get_if<BuiltinTypeValue>(&canonical->value) : nullptr;
            if (!builtin || !builtin_is_integer(builtin->kind)) {
                co_return std::unexpected(site.fail(
                    argument_span,
                    DiagnosticCode::TypeIndexInteger,
                    "Vector removal requires an integer index"
                ));
            }
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
        vector_intrinsic_result_type(
            intrinsic,
            receiver_type,
            shape->element,
            [&](BuiltinType type) noexcept -> ConstructionTypeRef {
                return site.draft().builtin_type(type);
            }
        ),
        SemIntrinsic {
            .operation = VectorIntrinsicOperation {.intrinsic = intrinsic},
            .operands = std::move(operands),
        },
        std::move(state),
        span
    );
}

template<typename Site>
auto construct_vector_factory(
    Site& site,
    ConstructionTypeRef owner,
    std::string_view name,
    std::span<const ASTExprID> arguments,
    Span span
) noexcept -> AnalysisTask<typename Site::Value> {
    const auto intrinsic =
        name == "from_slice" ? VectorIntrinsic::FromSlice : VectorIntrinsic::WithCapacity;
    if ((name != "from_slice" && name != "with_capacity") || arguments.size() != 1uz) {
        co_return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeMethodCall,
            "Vector factory requires from_slice(view) or with_capacity(minimum)"
        ));
    }
    const auto shape = sequence_shape(site.draft(), owner);
    const auto contract = vector_intrinsic_contract(intrinsic);
    const auto expected = contract.argument == VectorIntrinsicArgument::Slice
        ? make_slice_type(site.draft(), shape->element)
        : ConstructionTypeRef(site.draft().builtin_type(BuiltinType::Usize));
    auto state = Site::operand_state();
    auto value = co_await site.read_argument(arguments.front(), expected);
    if (!value) {
        co_return std::unexpected(value.error());
    }
    if (auto checked = site.convert_argument(*value, expected, span); !checked) {
        co_return std::unexpected(checked.error());
    }
    auto operand = site.consume_read_argument(state, std::move(*value), span);
    if (!operand) {
        co_return std::unexpected(operand.error());
    }
    auto operands = std::vector<SemCallArgument>();
    operands.push_back({.access = AccessMode::Read, .expression = std::move(*operand)});
    co_return site.finish_constructed(
        vector_intrinsic_result_type(
            intrinsic,
            owner,
            shape->element,
            [&](BuiltinType type) noexcept -> ConstructionTypeRef {
                return site.draft().builtin_type(type);
            }
        ),
        SemIntrinsic {
            .operation = VectorIntrinsicOperation {.intrinsic = intrinsic},
            .operands = std::move(operands)
        },
        std::move(state),
        span
    );
}
