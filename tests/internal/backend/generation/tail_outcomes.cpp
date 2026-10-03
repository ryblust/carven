module carven:test.internal.backend.generation.tail_outcomes;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target.name;
import :backend.target.traversal;
import :backend.target;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

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
        ct::expect(traverse_target_unit(unit.sections(), query));
    }
    return query;
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "Generation: exact value and void tails propagate materialized outcomes",
        [] static noexcept {
            const auto query = outcome_operations(
                "struct Error {}\n"
                "fn value() -> i32 throw Error { return 7; }\n"
                "fn forward() -> i32 throw Error { return value()?; }\n"
                "fn action() throw Error {}\n"
                "fn forward_void() throw Error { return action()?; }\n"
            );
            ct::expect(query.propagation == 2uz);
            ct::expect(query.returned_propagation == 2uz);
            ct::expect(query.success_projection == 0uz);
        }
    );

    ct::test("Generation: a tail demand does not propagate argument outcomes", [] static noexcept {
        const auto query = outcome_operations(
            "struct Error {}\n"
            "fn value() -> i32 throw Error { return 7; }\n"
            "fn consume(value: i32) -> i32 throw Error { return value; }\n"
            "fn forward() -> i32 throw Error { return consume(value()?)?; }\n"
        );
        ct::expect(query.propagation == 1uz);
        ct::expect(query.returned_propagation == 1uz);
        ct::expect(query.success_projection == 1uz);
    });

    ct::test(
        "Generation: handlers widening and success computation retain projected outcomes",
        [] static noexcept {
            const auto query = outcome_operations(
                "struct Error {}\n"
                "struct Other {}\n"
                "fn value() -> i32 throw Error { return 7; }\n"
                "fn wide() -> i32 throw Error + Other { return value()?; }\n"
                "fn add() -> i32 throw Error { return value()? + 1; }\n"
                "fn recover() -> i32 { return try { value()? } catch { Error(_) => 0, }; }\n"
            );
            ct::expect(query.propagation == 0uz);
            ct::expect(query.success_projection == 3uz);
        }
    );

    ct::test(
        "Generation: callable views propagate the exact carrier including test stops",
        [] static noexcept {
            const auto query = outcome_operations(
                "struct Error {}\n"
                "fn invoke(callback: fn(i32) -> i32 throw Error, value: i32) -> i32 throw Error {\n"
                "  return callback(value)?;\n"
                "}\n"
            );
            ct::expect(query.propagation == 1uz);
            ct::expect(query.returned_propagation == 1uz);
            ct::expect(query.success_projection == 0uz);
        }
    );
});

} // namespace
