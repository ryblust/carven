module carven:semantic.analysis.expr.async;

import :diagnostics.code;
import :frontend.ast.expr;
import :semantic.analysis.diagnostics;
import :semantic.analysis.expr.operand;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.semir.async;
import :semantic.semir.structured;
import :semantic.semir.type;
import std;

template<typename Site>
auto construct_async_intrinsic(
    Site& site,
    const ASTCallExpr& source,
    AsyncIntrinsic kind,
    Span span
) noexcept -> AnalysisResult<typename Site::Value> {
    if (kind == AsyncIntrinsic::WaitReady || kind == AsyncIntrinsic::CloseChild) {
        return std::unexpected(site.fail(
            span,
            DiagnosticCode::AsyncAdmission,
            "child waiting and closing require a direct await with lexical children"
        ));
    }
    if (kind == AsyncIntrinsic::CancelChild) {
        return std::unexpected(site.fail(
            span,
            DiagnosticCode::AsyncOwnership,
            "cancel requires a complete lexical child binding"
        ));
    }
    if (!source.arguments.empty()) {
        return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeCallArity,
            "async query and cold leaves accept no arguments"
        ));
    }
    auto& draft = site.draft();
    const auto type = kind == AsyncIntrinsic::CancelRequested
        ? ConstructionTypeRef(draft.builtin_type(BuiltinType::Bool))
        : make_operation_type(
              draft,
              draft.builtin_type(BuiltinType::Void),
              draft.add_empty_failure_term()
          );
    return site.finish_constructed(
        type,
        SemAsyncIntrinsic {.kind = kind, .child = std::nullopt},
        Site::operand_state(),
        span
    );
}

template<typename Site>
auto construct_async_composition(
    Site& site,
    const ASTCallExpr& source,
    AsyncCompositionPolicy policy,
    Span span
) noexcept -> AnalysisTask<typename Site::Value> {
    if (policy != AsyncCompositionPolicy::WhenAll && source.arguments.empty()) {
        co_return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeCallArity,
            "selection requires at least one operation"
        ));
    }
    auto& draft = site.draft();
    auto state = Site::operand_state();
    auto inputs = std::vector<SemCallArgument>();
    auto successes = std::vector<ConstructionTypeRef>();
    const auto failures = draft.add_empty_failure_term();
    for (const auto id : source.arguments) {
        const auto selected = call_argument_operand(site.syntax(), id);
        const auto input_span = site.syntax().expression(id).span;
        auto input = co_await site.read(selected.expression, std::nullopt);
        if (!input) {
            co_return std::unexpected(input.error());
        }
        const auto operation = operation_shape(draft, site.type(*input));
        if (!operation) {
            co_return std::unexpected(site.fail(
                input_span,
                DiagnosticCode::AsyncAdmission,
                "composition inputs must be cold operations"
            ));
        }
        auto value = site.consume_cold_owner(state, std::move(*input), selected.access, input_span);
        if (!value) {
            co_return std::unexpected(value.error());
        }
        inputs.push_back({.access = AccessMode::Take, .expression = std::move(*value)});
        successes.push_back(operation->success);
        draft.add_failure_contribution(failures, operation->failures);
    }
    auto success = ConstructionTypeRef(draft.builtin_type(BuiltinType::Void));
    if (policy == AsyncCompositionPolicy::WhenAll) {
        if (!successes.empty()) {
            success = make_tuple_type(draft, std::move(successes));
        }
    } else {
        for (const auto type : successes) {
            if (auto checked = site.require_matching_type(type, successes.front(), span);
                !checked) {
                co_return std::unexpected(checked.error());
            }
        }
        success =
            make_tuple_type(draft, {draft.builtin_type(BuiltinType::Usize), successes.front()});
    }
    co_return site.finish_constructed(
        make_operation_type(draft, success, failures),
        SemColdCompose {
            .policy = policy,
            .success = BodyType(success),
            .inputs = std::move(inputs)
        },
        std::move(state),
        span
    );
}

template<typename Site>
auto construct_async_timeout(
    Site& site,
    const ASTCallExpr& source,
    const TimeoutContract& contract,
    Span span
) noexcept -> AnalysisTask<typename Site::Value> {
    if (source.arguments.size() != 2uz) {
        co_return std::unexpected(site.fail(
            span,
            DiagnosticCode::TypeCallArity,
            "timeout requires an operation and a Duration"
        ));
    }
    auto& draft = site.draft();
    auto state = Site::operand_state();
    const auto input_id = source.arguments[0];
    const auto selected = call_argument_operand(site.syntax(), input_id);
    const auto input_span = site.syntax().expression(input_id).span;
    auto input = co_await site.read(selected.expression, std::nullopt);
    if (!input) {
        co_return std::unexpected(input.error());
    }
    const auto operation = operation_shape(draft, site.type(*input));
    if (!operation) {
        co_return std::unexpected(site.fail(
            input_span,
            DiagnosticCode::AsyncAdmission,
            "timeout requires a cold operation"
        ));
    }
    auto owned = site.consume_cold_owner(state, std::move(*input), selected.access, input_span);
    if (!owned) {
        co_return std::unexpected(owned.error());
    }
    auto duration = co_await read_value_argument(site, source.arguments[1], contract.duration);
    if (!duration) {
        co_return std::unexpected(duration.error());
    }
    const auto duration_span = site.syntax().expression(source.arguments[1]).span;
    const auto owner =
        std::get<StructTypeValue>(draft.type_copy(contract.duration).value).structure;
    auto ticks = AnalysisResult<typename Site::Value>(site.finish_field(
        draft.builtin_type(BuiltinType::I64),
        {owner, 0},
        std::move(*duration),
        duration_span
    ));
    if (!ticks) {
        co_return std::unexpected(ticks.error());
    }
    auto ticks_value = site.consume_read(state, std::move(*ticks), duration_span);
    if (!ticks_value) {
        co_return std::unexpected(ticks_value.error());
    }
    const auto failures = draft.add_empty_failure_term();
    draft.add_failure_contribution(failures, operation->failures);
    draft.add_failure_member(failures, contract.timeout_failure);
    draft.add_failure_member(failures, contract.range_failure);
    co_return site.finish_constructed(
        make_operation_type(draft, operation->success, failures),
        SemColdTimeout {
            .operation = OwnedSemanticExpression(std::move(*owned)),
            .duration_ticks = OwnedSemanticExpression(std::move(*ticks_value)),
            .timeout_failure = contract.timeout_failure,
            .range_failure = contract.range_failure
        },
        std::move(state),
        span
    );
}
