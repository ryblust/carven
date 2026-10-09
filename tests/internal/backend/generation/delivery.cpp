module carven:test.internal.backend.generation.delivery;

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

const TestSuite suite([] static noexcept {
    "Generation: independent value branches compose without duplicating successors"_test =
        [] static noexcept {
            for (const auto suffix : {false, true}) {
                auto single_nodes = 0uz;
                for (const auto count : {1uz, 2uz, 4uz, 8uz}) {
                    auto parameters = std::string();
                    auto flags = std::string();
                    auto arguments = std::string();
                    auto initializers = std::string();
                    for (auto index = 0uz; index < count; ++index) {
                        if (index != 0uz) {
                            parameters += ", ";
                            flags += ", ";
                            arguments += ", ";
                        }
                        parameters += std::format("a{}: i32", index);
                        flags += std::format("x{}: bool, y{}: bool", index, index);
                        const auto choice = suffix
                            ? std::format(
                                  "if (x{0}) {{ 1 }} else if (y{0}) {{ 2 }} else {{ throw Failure::Stop; }}",
                                  index
                              )
                            : std::format("if (x{}) {{ 1 }} else {{ 2 }}", index);
                        initializers += std::format("let a{}: i32 = {};", index, choice);
                        arguments += suffix ? std::format("a{}", index) : choice;
                    }
                    const auto source = std::format(
                        "enum Failure {{ Stop, }} fn sum({}) -> i32 {{ return a0; }} "
                        "fn choose({}) -> i32 throw Failure {{ {} return sum({}); }}",
                        parameters,
                        flags,
                        suffix ? initializers : "",
                        arguments
                    );
                    const auto semantic_owner = analyze_test_program(source);
                    const auto compilation = PlannedCompilation::build(
                        semantic_owner,
                        {.test_mode = TestGenerationMode::None,
                         .linkage_domain = *LinkageDomain::explicit_value("linear_composition"),
                         .library = std::nullopt}
                    );

                    struct Query final {
                        std::size_t nodes;
                        std::size_t calls;

                        auto enter_expression(
                            const TargetExpr& expression,
                            TargetExpressionRole
                        ) noexcept -> bool {
                            ++nodes;
                            if (const auto* call = std::get_if<TargetCallExpr>(&expression.value)) {
                                if (const auto* name = std::get_if<TargetNameExpr>(
                                        &template_primary_expression(*call->callee).value
                                    )) {
                                    calls += name->name.components().back().spelling() == "sum";
                                }
                            }
                            return true;
                        }

                        auto enter_statement(const TargetStmt&) noexcept -> bool {
                            ++nodes;
                            return true;
                        }
                    };

                    auto query = Query {.nodes = 0uz, .calls = 0uz};
                    for (const auto artifact : compilation.target().artifacts()) {
                        const auto unit = lower_artifact(compilation, artifact.id);
                        expect(traverse_target_unit(unit.sections(), query));
                    }
                    expect(query.calls == 1uz);
                    if (count == 1uz) {
                        single_nodes = query.nodes;
                    }
                    expect(query.nodes <= count * single_nodes).note("query.nodes: ", query.nodes);
                }
            }
        };
});

} // namespace
