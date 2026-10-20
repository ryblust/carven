module carven:test.internal.backend.generation.display;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.traversal;
import :backend.target;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

struct DisplayQuery final {
    std::size_t nodes;

    auto enter_expression(const TargetExpr&, TargetExpressionRole) noexcept -> bool {
        ++nodes;
        return true;
    }

    auto enter_statement(const TargetStmt&) noexcept -> bool {
        ++nodes;
        return true;
    }
};

auto count_nodes(std::string source) noexcept -> std::size_t {
    const auto semantic_owner = analyze_test_program(std::move(source));
    const auto compilation = PlannedCompilation::build(
        semantic_owner,
        {
            .test_mode = TestGenerationMode::None,
            .linkage_domain = *LinkageDomain::explicit_value("display"),
            .library = std::nullopt,
        },
        {}
    );
    auto query = DisplayQuery {.nodes = 0uz};
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        require(traverse_target_unit(unit.sections(), query));
    }
    return query.nodes;
}

const TestSuite suite([] static noexcept {
    "Generation: shared display types have bounded target syntax"_test = [] static noexcept {
        constexpr auto width = 4uz;
        constexpr auto depths = std::array {2uz, 4uz, 6uz};
        for (const auto depth : depths) {
            auto source = std::string("struct N0 { value: i32 }\n");
            for (auto level = 1uz; level <= depth; ++level) {
                source += std::format("struct N{} {{ ", level);
                for (auto field = 0uz; field < width; ++field) {
                    source += std::format("f{}: N{}, ", field, level - 1uz);
                }
                source += "}\n";
            }
            source += std::format("fn show(value: N{}) {{ println(value); }}", depth);
            const auto nodes = count_nodes(std::move(source));
            expect_less(nodes, 100uz * (depth + 1uz) * width).note("depth: ", depth);
        }
    };
});

} // namespace
