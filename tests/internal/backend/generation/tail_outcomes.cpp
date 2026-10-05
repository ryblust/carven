module carven:test.internal.backend.generation.tail_outcomes;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target.type;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

struct OutcomeOperations final {
    std::size_t propagation = 0;
    std::size_t returned_propagation = 0;
    std::size_t success_projection = 0;

    static auto is_call(const TargetExpr& expression, std::string_view spelling) noexcept -> bool {
        const auto* call = std::get_if<TargetCallExpr>(&expression.value);
        if (call == nullptr) {
            return false;
        }
        const auto* member =
            std::get_if<TargetMemberExpr>(&template_primary_expression(*call->callee).value);
        if (member == nullptr) {
            return false;
        }
        const auto* name = std::get_if<TargetIdentifier>(&member->name);
        return name != nullptr && name->spelling() == spelling;
    }

    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool {
        propagation += is_call(expression, "propagate");
        success_projection += is_call(expression, "success_if");
        return true;
    }

    auto enter_statement(const TargetStmt& statement) noexcept -> bool {
        if (const auto* returned = std::get_if<TargetReturnStmt>(&statement.value);
            returned != nullptr && returned->expression) {
            returned_propagation += is_call(*returned->expression, "propagate");
        }
        return true;
    }
};

auto outcome_operations(std::string source) noexcept -> OutcomeOperations {
    const auto compilation = PlannedCompilation::build(
        analyze_test_program(std::move(source)),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("tail_outcomes")}
    );
    auto query = OutcomeOperations {};
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        expect(traverse_target_unit(unit.sections(), query));
    }
    return query;
}

const TestSuite suite([] static noexcept {
    "Generation: exact value and void tails propagate materialized outcomes"_test =
        [] static noexcept {
            const auto query = outcome_operations(
                "struct Error {}\n"
                "fn value() -> i32 throw Error { return 7; }\n"
                "fn forward() -> i32 throw Error { return value()?; }\n"
                "fn action() throw Error {}\n"
                "fn forward_void() throw Error { return action()?; }\n"
            );
            expect(query.propagation == 2uz);
            expect(query.returned_propagation == 2uz);
            expect(query.success_projection == 0uz);
        };

    "Generation: a tail demand does not propagate argument outcomes"_test = [] static noexcept {
        const auto query = outcome_operations(
            "struct Error {}\n"
            "fn value() -> i32 throw Error { return 7; }\n"
            "fn consume(value: i32) -> i32 throw Error { return value; }\n"
            "fn forward() -> i32 throw Error { return consume(value()?)?; }\n"
        );
        expect(query.propagation == 1uz);
        expect(query.returned_propagation == 1uz);
        expect(query.success_projection == 1uz);
    };

    "Generation: handlers widening and success computation retain projected outcomes"_test =
        [] static noexcept {
            const auto query = outcome_operations(
                "struct Error {}\n"
                "struct Other {}\n"
                "fn value() -> i32 throw Error { return 7; }\n"
                "fn wide() -> i32 throw Error + Other { return value()?; }\n"
                "fn add() -> i32 throw Error { return value()? + 1; }\n"
                "fn recover() -> i32 { return try { value()? } catch { Error(_) => 0, }; }\n"
            );
            expect(query.propagation == 0uz);
            expect(query.success_projection == 3uz);
        };

    "Generation: callable views propagate the exact carrier including test stops"_test =
        [] static noexcept {
            const auto query = outcome_operations(
                "struct Error {}\n"
                "fn invoke(callback: fn(i32) -> i32 throw Error, value: i32) -> i32 throw Error {\n"
                "  return callback(value)?;\n"
                "}\n"
            );
            expect(query.propagation == 1uz);
            expect(query.returned_propagation == 1uz);
            expect(query.success_projection == 0uz);
        };

    "Generation: discarded failing calls check success without projecting a payload"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(
                    "enum Error { Failed, }\n"
                    "fn produce(flag: bool) -> i32 throw Error {\n"
                    "  if flag { return 7; } else { throw Error::Failed; }\n"
                    "}\n"
                    "fn discard(flag: bool) throw Error { produce(flag)?; }\n"
                    "fn discard_wrapped(flag: bool) throw Error { (produce(flag)? as i32) == 0; }\n"
                    "fn discard_selected(flag: bool) throw Error { flag && (produce(flag)? == 0); }\n"
                    "fn deliver(flag: bool) -> i32 throw Error { return produce(flag)? + 1; }\n"
                ),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("result_consumption")}
            );

            struct Query final {
                std::size_t payloads;
                std::size_t success_checks;
                std::size_t saved_successes;

                static auto is_success(const TargetExpr& expression) noexcept -> bool {
                    const auto* call = std::get_if<TargetCallExpr>(&expression.value);
                    if (call == nullptr) {
                        return false;
                    }
                    const auto* member = std::get_if<TargetMemberExpr>(
                        &template_primary_expression(*call->callee).value
                    );
                    if (member == nullptr) {
                        return false;
                    }
                    const auto* name = std::get_if<TargetIdentifier>(&member->name);
                    return name != nullptr && name->spelling() == "success_if";
                }

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    success_checks += is_success(expression);
                    if (const auto* member = std::get_if<TargetMemberExpr>(&expression.value)) {
                        const auto* name = std::get_if<TargetIdentifier>(&member->name);
                        payloads += name != nullptr && name->spelling() == "value";
                    }
                    return true;
                }

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    expect(!(std::holds_alternative<TargetDiscardStmt>(statement.value)));
                    if (const auto* variable = std::get_if<TargetVariableStmt>(&statement.value)) {
                        saved_successes += is_success(variable->initializer);
                    }
                    return true;
                }
            };

            auto query = Query {.payloads = 0uz, .success_checks = 0uz, .saved_successes = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                expect(traverse_target_unit(unit.sections(), query));
            }
            expect(query.success_checks == 4uz);
            expect(query.saved_successes == 1uz);
            expect(query.payloads == 1uz);
        };

    "Generation: a sole failure needs no type selection in its handler or dispatch"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
            struct Failure {}
            fn source() -> i32 throw Failure { throw Failure {}; }
            fn recover() -> i32 => try { source()? } catch { Failure(_) => 7, };
        )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("sole_failure")}
            );

            struct Query final {
                const TargetUnit& unit;
                std::size_t branches = 0;

                auto visit_type(TargetTypeID id) const noexcept -> bool {
                    if (const auto* type = std::get_if<TargetIntrinsicType>(&unit.type(id).value)) {
                        expect(type->symbol != TargetSymbol::StdVariant);
                    }
                    return visit_target_type_children(unit.type(id).value, *this);
                }

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    branches += std::holds_alternative<TargetIfStmt>(statement.value);
                    return true;
                }
            };

            auto branches = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {.unit = unit};
                if (!expect(traverse_target_unit(unit.sections(), query))) {
                    return;
                }
                branches += query.branches;
            }
            expect(branches == 1uz); // The call's success/failure distinction remains.
        };
});

} // namespace
