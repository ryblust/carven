module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.formatted_append;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

struct AppendQuery final {
    std::vector<TargetSymbol> entries;
    std::vector<std::size_t> argument_counts;
    std::vector<std::string> events;
    std::size_t owning_formats;

    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;
};

auto AppendQuery::enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
    -> bool {
    const auto* call = std::get_if<TargetCallExpr>(&expression.value);
    if (call == nullptr) {
        return true;
    }
    if (const auto* name = std::get_if<TargetIntrinsicNameExpr>(&call->callee->value)) {
        if (name->symbol == TargetSymbol::RuntimeAppendFormat
            || name->symbol == TargetSymbol::RuntimeAppendFormatValidUTF8) {
            entries.push_back(name->symbol);
            argument_counts.push_back(call->arguments.size());
            events.push_back("format_append");
            REQUIRE(call->arguments.size() >= 2uz);
            // The writable destination precedes the normalized format and holes.
            CHECK_FALSE(std::holds_alternative<TargetStaticCastExpr>(call->arguments[0].value));
            CHECK(std::holds_alternative<TargetLiteralExpr>(call->arguments[1].value));
        }
        owning_formats += name->symbol == TargetSymbol::RuntimeFormat
            || name->symbol == TargetSymbol::RuntimeFormatValidUTF8;
    }
    if (const auto* member = std::get_if<TargetMemberExpr>(&call->callee->value)) {
        if (const auto* name = std::get_if<TargetIdentifier>(&member->name);
            name != nullptr && name->spelling() == "append") {
            REQUIRE(call->arguments.size() == 1uz);
            events.push_back(
                std::holds_alternative<TargetLiteralExpr>(call->arguments.front().value)
                    ? "append"
                    : "append_value"
            );
        }
    }
    if (const auto* name = std::get_if<TargetNameExpr>(&call->callee->value)) {
        const auto spelling = name->name.components().back().spelling();
        if (spelling == "select" || spelling == "touch") {
            events.emplace_back(spelling);
        }
    }
    return true;
}

auto inspect_append(std::string source) noexcept -> AppendQuery {
    const auto compilation = PlannedCompilation::build(
        analyze_test_program(std::move(source)),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("formatted_append")}
    );
    auto query =
        AppendQuery {.entries = {}, .argument_counts = {}, .events = {}, .owning_formats = 0uz};
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        REQUIRE(traverse_target_unit(unit.sections(), query));
    }
    return query;
}

} // namespace

TEST_CASE("Generation: formatted append selects its entry and passes only residual hole values") {
    struct Scenario final {
        std::string_view expression;
        std::optional<TargetSymbol> entry;
        std::size_t arguments;
    };

    const auto scenarios = std::to_array<Scenario>({
        {.expression = R"(f"{number:04x}/{text}")", .entry = std::nullopt, .arguments = 0uz},
        {.expression = R"(f"{42:04x}/{text}")", .entry = std::nullopt, .arguments = 0uz},
        {.expression = R"(f"{number:c}")",
         .entry = TargetSymbol::RuntimeAppendFormat,
         .arguments = 3uz},
        {.expression = R"(f"{42}/{number:L}")",
         .entry = TargetSymbol::RuntimeAppendFormat,
         .arguments = 3uz},
        {.expression = R"(f"{42:04x}/{number:0{width}}/{text}")",
         .entry = std::nullopt,
         .arguments = 0uz},
        {.expression = R"(f"{42}/{::probe::value()}")",
         .entry = TargetSymbol::RuntimeAppendFormat,
         .arguments = 4uz},
    });
    for (const auto& scenario : scenarios) {
        CAPTURE(scenario.expression);
        const auto query = inspect_append(
            std::format(
                "import \"probe.hpp\"; "
                "fn append(&output: String, number: i32, width: i32, text: str) {{ "
                "output.append_format({}); }}",
                scenario.expression
            )
        );
        if (!scenario.entry) {
            CHECK(query.entries.empty());
            CHECK(query.owning_formats == 0uz);
            CHECK(std::ranges::find(query.events, "append_value") != query.events.end());
            continue;
        }
        REQUIRE(query.entries.size() == 1uz);
        CHECK(query.entries.front() == *scenario.entry);
        REQUIRE(query.argument_counts.size() == 1uz);
        CHECK(query.argument_counts.front() == scenario.arguments);
        CHECK(query.owning_formats == 0uz);
    }
}

TEST_CASE("Generation: known formatted append retains destination selection and hole effects") {
    const auto query = inspect_append(R"(
        fn select(&trace: i32) -> usize { trace += 1; return 0; }
        fn touch(&trace: i32) -> bool { trace += 1; return true; }
        fn append(&outputs: [String; 2], &trace: i32) {
            outputs[select(&trace)].append_format(f"{touch(&trace) && false}");
        }
    )");
    CHECK(query.entries.empty());
    CHECK(query.owning_formats == 0uz);
    CHECK(query.events == std::vector<std::string> {"select", "touch", "append"});
}

TEST_CASE("Generation: empty and fully known formatted append use static text directly") {
    const auto scenarios = std::to_array<std::string_view>({R"(f"")", R"(f"{42:04x}/{true}")"});
    for (const auto expression : scenarios) {
        CAPTURE(expression);
        const auto query = inspect_append(
            std::format("fn append(&output: String) {{ output.append_format({}); }}", expression)
        );
        CHECK(query.entries.empty());
        CHECK(query.owning_formats == 0uz);
        CHECK(query.events == std::vector<std::string> {"append"});
    }
}
