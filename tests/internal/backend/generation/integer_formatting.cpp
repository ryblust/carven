module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.integer_formatting;

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

struct IntegerFormatQuery final {
    std::vector<std::vector<TargetTemplateArgument>> policies;
    std::size_t generic_calls = 0uz;

    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;
};

auto IntegerFormatQuery::enter_expression(
    const TargetExpr& expression,
    TargetExpressionRole
) noexcept -> bool {
    const auto* call = std::get_if<TargetCallExpr>(&expression.value);
    if (call == nullptr) {
        return true;
    }
    if (const auto* intrinsic = std::get_if<TargetIntrinsicNameExpr>(&call->callee->value)) {
        generic_calls += intrinsic->symbol == TargetSymbol::RuntimeFormat
            || intrinsic->symbol == TargetSymbol::RuntimeFormatValidUTF8
            || intrinsic->symbol == TargetSymbol::RuntimeAppendFormat
            || intrinsic->symbol == TargetSymbol::RuntimeAppendFormatValidUTF8;
    }
    const auto* member = std::get_if<TargetMemberExpr>(&call->callee->value);
    if (member != nullptr) {
        const auto* name = std::get_if<TargetIdentifier>(&member->name);
        if (name != nullptr && name->spelling() == "integer") {
            policies.push_back(call->template_arguments);
            CHECK(call->arguments.size() == 2uz);
        }
    }
    return true;
}

auto inspect(std::string source) noexcept -> IntegerFormatQuery {
    const auto compilation = PlannedCompilation::build(
        analyze_test_program(std::move(source)),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("integer_format")}
    );
    auto query = IntegerFormatQuery();
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        REQUIRE(traverse_target_unit(unit.sections(), query));
    }
    return query;
}

} // namespace

TEST_CASE("Generation: parsed integer formats select direct writes with scalar template policies") {
    const auto query = inspect(R"(
        fn format(value: u64) -> String => f"value={value:016X};";
        fn append(&output: String, first: i32, second: i32) {
            output.append_format(f"{first:011d}/{second:011d}");
        }
        fn wide(value: i32) -> String => f"{value:65537}";
    )");
    CHECK(query.generic_calls == 0uz);
    REQUIRE(query.policies.size() == 4uz);
    const auto bases = std::array {16u, 10u, 10u, 10u};
    for (const auto& [index, policy] : std::views::enumerate(query.policies)) {
        REQUIRE(policy.size() == 3uz);
        REQUIRE(std::holds_alternative<TargetIntegerLiteral>(policy[0]));
        CHECK(std::get<TargetIntegerLiteral>(policy[0]).magnitude == bases[index]);
        REQUIRE(std::holds_alternative<bool>(policy[1]));
        CHECK(std::get<bool>(policy[1]) == (index == 0uz));
        REQUIRE(std::holds_alternative<bool>(policy[2]));
        CHECK(std::get<bool>(policy[2]) == (index != 3uz));
    }
}

TEST_CASE(
    "Generation: unsupported integer specifications and native protocols retain complete formatting"
) {
    const auto query = inspect(R"(
        import "probe.hpp";
        fn native(value: i32) -> String => f"{value:08x}/{::probe::value()}";
        fn dynamic(value: i32, width: i32) -> String => f"{value:0{width}d}";
        fn locale(value: i32) -> String => f"{value:L}";
        fn large(value: i32) -> String => f"{value:2147483648}";
        fn text(value: i32, text: str) -> String => f"{value:08x}/{text}";
    )");
    CHECK(query.policies.empty());
    CHECK(query.generic_calls == 5uz);
}

TEST_CASE("Generation: known dynamic width selects integer writes") {
    const auto query = inspect(R"(
        const width = 16;
        fn format(value: u64) -> String => f"{value:0{width}X}";
        fn zero(value: i32) -> String => f"{value:0{0}}";
        fn discarded(value: i32) { f"{value:0{4}}"; }
    )");
    CHECK(query.generic_calls == 0uz);
    REQUIRE(query.policies.size() == 3uz);
    for (const auto& policy : query.policies) {
        REQUIRE(policy.size() == 3uz);
        CHECK(std::get<bool>(policy[2]));
    }
}
