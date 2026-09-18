module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.constant_functions;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target.type;
import :backend.target;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

struct FunctionFacts final {
    std::size_t label_calls = 0uz;
    std::size_t all_calls = 0uz;
    std::vector<TargetStringLiteral> literals;

    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;
};

auto FunctionFacts::enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
    -> bool {
    if (const auto* call = std::get_if<TargetCallExpr>(&expression.value)) {
        ++all_calls;
        if (const auto* name = std::get_if<TargetNameExpr>(&call->callee->value)) {
            label_calls += name->name.components().back().spelling() == "label";
        }
    }
    if (const auto* literal = std::get_if<TargetLiteralExpr>(&expression.value)) {
        if (const auto* text = std::get_if<TargetStringLiteral>(&literal->value)) {
            literals.push_back(*text);
        }
    }
    return true;
}

struct FunctionQuery final {
    const TargetUnit& unit;
    std::size_t frozen_definitions = 0uz;
    std::size_t ordinary_definitions = 0uz;
    std::size_t label_definitions = 0uz;

    auto enter_declaration(const TargetDecl& declaration) noexcept -> bool;
};

auto FunctionQuery::enter_declaration(const TargetDecl& declaration) noexcept -> bool {
    const auto* function = std::get_if<TargetFunctionDecl>(&declaration);
    if (function == nullptr) {
        return true;
    }
    const auto* definition = std::get_if<TargetFreeFunctionDefinition>(&function->form);
    if (definition == nullptr) {
        return true;
    }
    const auto name = function->name.components().back().spelling();
    if (name == "label") {
        ++label_definitions;
        return true;
    }
    if (name != "frozen_module"
        && name != "frozen_local"
        && name != "frozen_direct"
        && name != "ordinary") {
        return true;
    }
    const auto* result = std::get_if<TargetIntrinsicType>(&unit.type(function->result).value);
    REQUIRE(result != nullptr);
    auto facts = FunctionFacts();
    REQUIRE(traverse_target_statements(definition->body, facts));
    if (name == "ordinary") {
        CHECK(result->symbol == TargetSymbol::RuntimeString);
        CHECK(facts.label_calls == 1uz);
        CHECK(facts.literals.empty());
        ++ordinary_definitions;
        return true;
    }
    CHECK(result->symbol == TargetSymbol::StdStringView);
    CHECK(facts.all_calls == 0uz);
    REQUIRE(facts.literals.size() == 1uz);
    CHECK(facts.literals.front().kind == TargetStringLiteralKind::StringView);
    CHECK(facts.literals.front().bytes == std::string_view("我\0😀", 8uz));
    ++frozen_definitions;
    return true;
}

} // namespace

TEST_CASE(
    "Generation: const function initializers emit static bytes and ordinary calls return owning String"
) {
    const auto compilation = PlannedCompilation::build(
        analyze_test_program(R"(
            const frozen = label();
            const fn label() -> String {
                var result = String {};
                result.push('我');
                result.append("\0😀");
                return result;
            }
            fn frozen_module() -> str => frozen;
            fn frozen_local() -> str { const result = label(); return result; }
            fn frozen_direct() -> str { const result = f"{'我'}\0{'😀'}"; return result; }
            fn ordinary() -> String => label();
        )"),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("constant_functions")}
    );
    auto frozen_definitions = 0uz;
    auto ordinary_definitions = 0uz;
    auto label_definitions = 0uz;
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        auto query = FunctionQuery {.unit = unit};
        CHECK(traverse_target_unit(unit.sections(), query));
        frozen_definitions += query.frozen_definitions;
        ordinary_definitions += query.ordinary_definitions;
        label_definitions += query.label_definitions;
    }
    CHECK(frozen_definitions == 3uz);
    CHECK(ordinary_definitions == 1uz);
    CHECK(label_definitions == 1uz);
}
