module carven:backend.realization.report.impl;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.context;
import :backend.preparation.body;
import :backend.realization.display;
import :backend.realization.operation;
import :backend.realization.realizer;
import :backend.realization.report;
import :backend.target.expr;
import :backend.target.origin;
import :backend.target.stmt;
import :backend.target.symbol;
import :semantic.semir.body;
import :semantic.semir.decl;
import :semantic.semir.evaluation;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.provenance.ids;
import :source.provenance;
import :support.invariant;
import :support.task;
import :support.visit;
import std;

auto BodyRealizer::lower_report(
    const SemReport& value,
    ProgramOriginID origin,
    LoweringStmtBuilder& destination
) noexcept -> ContinuationTask<std::monostate> {
    const auto known = value.condition
        ? known_boolean(context.semantic(), preparation.operation(**value.condition))
        : std::optional(false);
    if (known == true) {
        static_cast<void>(destination.accept((co_await discard(**value.condition))));
        co_return {};
    }
    auto should_report = std::optional<TargetExpr>();
    const auto explanation = fresh_local(TargetTemporaryNameKind::Explanation);
    if (value.operand_sources) {
        destination.emit(generated_statement(
            TargetVariableStmt {
                .binding = TargetVariableBinding::MutableValue,
                .maybe_unused = false,
                .local = explanation,
                .type = context.intrinsic_type(TargetSymbol::RuntimeDisplayWriter),
                .initializer = TargetExpr {
                    .value = TargetConstructionExpr {
                        .type = context.intrinsic_type(TargetSymbol::RuntimeDisplayWriter),
                        .initializer = std::monostate {}
                    }
                }
            }
        ));
    }
    if (value.condition && known == false && !value.operand_sources) {
        if (!destination.accept((co_await discard(**value.condition)))) {
            co_return {};
        }
    } else if (value.condition) {
        const auto previous = condition_observation;
        if (value.operand_sources) {
            condition_observation = ConditionObservation {
                .expression = std::addressof(**value.condition),
                .writer = explanation,
                .sources = *value.operand_sources
            };
        }
        auto condition = destination.accept((co_await operand(
            {.expression = std::addressof(**value.condition),
             .use = PreparedUse::OperandValue,
             .demand = PreparedDemand::Value}
        )));
        condition_observation = previous;
        if (!condition) {
            co_return {};
        }
        if (known == false) {
            destination.emit(
                generated_statement(TargetDiscardStmt {.expression = std::move(*condition)})
            );
        } else {
            should_report =
                prefix_expression(TargetPrefixOperator::LogicalNot, std::move(*condition));
        }
    }
    auto report = LoweringStmtBuilder();
    const auto source = target_source_origin(context.semantic().provenance(), origin);
    auto arguments = std::vector<TargetExpr>();
    arguments.push_back(string_expression(source.display_origin, TargetStringLiteralKind::String));
    arguments.push_back(integer_expression(source.line));
    arguments.push_back(
        integer_expression(context.semantic().provenance().location(origin).column)
    );
    const auto* operation = value.kind == ReportKind::Assert ? "assert"
        : value.kind == ReportKind::Check                    ? "check"
        : value.kind == ReportKind::Require                  ? "require"
                                                             : "fail";
    arguments.push_back(string_expression(operation, TargetStringLiteralKind::String));
    arguments.push_back(
        value.condition_source.has_value()
            ? string_expression(
                  std::string(context.semantic().provenance().spelling(*value.condition_source)),
                  TargetStringLiteralKind::String
              )
            : intrinsic_expression(TargetSymbol::StdNullopt)
    );
    const auto publish_report = [&]() noexcept {
        if (!should_report) {
            destination.append(std::move(report));
            return;
        }
        destination.record_exits(report.exits());
        auto branches = std::vector<TargetIfBranch>();
        branches.push_back(
            {.condition = std::move(*should_report), .body = std::move(report).finish()}
        );
        destination.emit(source_statement(
            context.semantic(),
            origin,
            TargetIfStmt {.branches = std::move(branches), .else_body = std::nullopt}
        ));
    };
    if (value.message) {
        auto message = report.accept((co_await operand(
            {.expression = std::addressof(**value.message),
             .use = PreparedUse::ReadBorrow,
             .demand = PreparedDemand::Value}
        )));
        if (!message) {
            publish_report();
            co_return {};
        }
        arguments.push_back(std::move(*message));
    } else {
        arguments.push_back(intrinsic_expression(TargetSymbol::StdNullopt));
    }
    if (value.operand_sources) {
        arguments.push_back(call_member(name_expression(explanation), "result", {}));
    } else {
        arguments.push_back(string_expression("", TargetStringLiteralKind::StringView));
    }
    if (value.kind == ReportKind::Assert) {
        report.terminate(
            statement_expression(call_expression(
                intrinsic_expression(TargetSymbol::RuntimeAssertionFailed),
                std::move(arguments)
            )),
            {.kind = LoweringExitKind::Unreachable, .identity = 0}
        );
    } else {
        report.emit(statement_expression(call_member(
            call_expression(intrinsic_expression(TargetSymbol::RuntimeCurrentTest), {}),
            "report_failure",
            std::move(arguments)
        )));
    }
    if (value.kind == ReportKind::Require || value.kind == ReportKind::Fail) {
        emit_test_exit(report);
    }
    publish_report();
    co_return {};
}

auto BodyRealizer::emit_test_exit(LoweringStmtBuilder& destination) noexcept -> void {
    auto result = std::optional<TargetExpr>();
    if (const auto* callable = std::get_if<CallableBodyExit>(&inputs.exit)) {
        if (!context.semantic().may_stop_test(callable->callable_id)) {
            invariant_violation("test exit absent from callable transport");
        }
        result = call_expression(
            static_member_expression(
                context.callable_result(callable->callable_id),
                TargetIdentifier::from_spelling("failure")
            ),
            target_expressions(
                TargetExpr {
                    .value = TargetConstructionExpr {
                        .type = context.intrinsic_type(TargetSymbol::RuntimeTestStopped),
                        .initializer = {}
                    }
                }
            )
        );
    }
    destination.terminate(
        generated_statement(TargetReturnStmt {.expression = std::move(result)}),
        LoweringExitTarget {
            std::holds_alternative<TestBodyExit>(inputs.exit) ? LoweringExitKind::Test
                                                              : LoweringExitKind::FunctionReturn,
            0
        }
    );
}

auto realize_observed_comparison(
    ModuleLowering& context,
    const SemBinary& operation,
    std::vector<TargetExpr> operands,
    TargetLocalID writer,
    std::array<ProgramSpellingID, 2> sources
) noexcept -> TargetExpr {
    auto names = context.make_callable_name_allocator();
    const auto left = context.target().add_local(names.fresh(TargetTemporaryNameKind::Operand));
    const auto right = context.target().add_local(names.fresh(TargetTemporaryNameKind::Operand));
    auto body = std::vector<TargetStmt>();
    body.push_back(generated_statement(
        TargetReturnStmt {
            .expression = realize_binary(
                context,
                name_expression(left),
                operation.operation,
                name_expression(right),
                context.semantic().types().builtin_type(BuiltinType::Bool)
            )
        }
    ));
    auto compare = TargetExpr {
        .value = TargetLambdaExpr {
            .parameters =
                {{.local = left,
                  .type = context.reference_type(
                      context.lower_type(operation.left->type.resolved()),
                      true
                  )},
                 {.local = right,
                  .type = context.reference_type(
                      context.lower_type(operation.right->type.resolved()),
                      true
                  )}},
            .result = context.intrinsic_type(TargetSymbol::Bool),
            .body = std::move(body)
        }
    };
    auto arguments = std::vector<TargetExpr>();
    arguments.push_back(name_expression(writer));
    arguments.push_back(
        realize_display(context, operation.left->type.resolved(), std::move(operands[0]))
    );
    arguments.push_back(
        realize_display(context, operation.right->type.resolved(), std::move(operands[1]))
    );
    arguments.push_back(std::move(compare));
    for (const auto source : sources) {
        arguments.push_back(string_expression(
            std::string(context.semantic().provenance().spelling(source)),
            TargetStringLiteralKind::StringView
        ));
    }
    return call_expression(
        intrinsic_expression(TargetSymbol::RuntimeObserveComparison),
        std::move(arguments)
    );
}

auto realize_observed_short_circuit(
    const ModuleLowering& context,
    TargetLocalID writer,
    std::array<ProgramSpellingID, 2> sources,
    bool left,
    std::optional<TargetExpr> right
) noexcept -> TargetExpr {
    auto arguments = target_expressions(
        name_expression(writer),
        bool_expression(left),
        right ? std::move(*right) : intrinsic_expression(TargetSymbol::StdNullopt)
    );
    for (const auto source : sources) {
        arguments.push_back(string_expression(
            std::string(context.semantic().provenance().spelling(source)),
            TargetStringLiteralKind::StringView
        ));
    }
    return call_expression(
        intrinsic_expression(TargetSymbol::RuntimeObserveShortCircuit),
        std::move(arguments)
    );
}
