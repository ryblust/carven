module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.analysis.interpolation_normalization;

import :semantic.analysis.expr.interpolation;
import :semantic.format;
import :test.internal.frontend.parse.fixture;
import std;

TEST_CASE("Semantic interpolation: normalization preserves text and depth-first operand order") {
    struct Scenario final {
        std::string_view expression;
        std::string format;
        std::vector<std::string_view> operands;
    };

    const auto scenarios = std::to_array<Scenario>({
        {.expression = R"(f"")", .format = "", .operands = {}},
        {
            .expression = R"(f"{{start}}\0我{value:0{width}.{precision}f}|{next:}")",
            .format = std::string("{{start}}") + '\0' + "我{0:0{1}.{2}f}|{3:}",
            .operands = {"value", "width", "precision", "next"},
        },
        {
            .expression = R"(f"{value:{width:{precision}}}/{last}")",
            .format = "{0:{1:{2}}}/{3}",
            .operands = {"value", "width", "precision", "last"},
        },
    });
    for (const auto& scenario : scenarios) {
        CAPTURE(scenario.expression);
        const auto source = std::format("fn source() {{ let text = {}; }}", scenario.expression);
        const auto tree = parse_valid(source);
        const auto ast = tree.view();
        const auto& statement = ast.statement(function_body(tree).statements.front());
        const auto& binding = get<ASTVariableDecl>(statement);
        REQUIRE(binding.initializer.has_value());
        const auto& interpolation = get<ASTInterpolationExpr>(ast.expression(*binding.initializer));
        const auto normalized = normalize_interpolation(interpolation);
        CHECK(serialize_format(normalized.specification) == scenario.format);
        REQUIRE(normalized.operands.size() == scenario.operands.size());
        for (auto index = 0uz; index < normalized.operands.size(); ++index) {
            CHECK(
                slice(source, ast.expression(normalized.operands[index]).span)
                == scenario.operands[index]
            );
        }
    }
}
