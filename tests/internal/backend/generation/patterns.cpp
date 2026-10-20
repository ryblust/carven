module carven:test.internal.backend.generation.patterns;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.traversal;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Generation: independent nested pattern alternatives keep target size proportional"_test =
        [] static noexcept {
            auto counts = std::vector<std::size_t>();
            for (const auto width : {2uz, 4uz, 8uz}) {
                auto fields = std::string();
                auto patterns = std::string();
                for (auto index = 0uz; index < width; ++index) {
                    if (index != 0uz) {
                        fields += ", ";
                        patterns += ", ";
                    }
                    fields += "i32";
                    patterns += "0 | 1";
                }
                const auto semantic_owner = analyze_test_program(
                    std::format(
                        "enum Choices {{ Fields({}), }} "
                        "fn select(value: Choices) -> i32 {{ return match value {{ "
                        ".Fields({}) => 1, _ => 0, }}; }}",
                        fields,
                        patterns
                    )
                );
                const auto compilation = PlannedCompilation::build(
                    semantic_owner,
                    {
                        .test_mode = TestGenerationMode::None,
                        .linkage_domain = *LinkageDomain::explicit_value("pattern_size"),
                        .library = std::nullopt,
                    },
                    {}
                );

                struct Query final {
                    std::size_t nodes;

                    auto enter_expression(const TargetExpr&, TargetExpressionRole) noexcept
                        -> bool {
                        ++nodes;
                        return true;
                    }

                    auto enter_statement(const TargetStmt&) noexcept -> bool {
                        ++nodes;
                        return true;
                    }
                };

                auto query = Query {.nodes = 0uz};
                for (const auto artifact : compilation.target().artifacts()) {
                    const auto unit = lower_artifact(compilation, artifact.id);
                    expect(traverse_target_unit(unit.sections(), query));
                }
                counts.push_back(query.nodes);
            }
            if (!expect(counts.front() > 0uz)) {
                return;
            }
            for (auto index = 1uz; index < counts.size(); ++index) {
                // Doubling independent choices must not expand their Cartesian product.
                // Leave room for target scaffolding without fixing names or exact counts.
                expect(counts[index] <= 3uz * counts[index - 1uz]);
            }
        };
});

} // namespace
