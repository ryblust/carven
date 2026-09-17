module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.constant_structs;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.traversal;
import :backend.target;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

struct ExpressionFacts final {
    std::size_t constructions = 0uz;
    std::size_t calls = 0uz;

    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;
};

auto ExpressionFacts::enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
    -> bool {
    constructions += std::holds_alternative<TargetConstructionExpr>(expression.value);
    calls += std::holds_alternative<TargetCallExpr>(expression.value);
    return true;
}

struct DeclarationFacts final {
    bool record_defined = false;
    std::size_t storage = 0uz;
    std::size_t frozen = 0uz;
    std::size_t ordinary = 0uz;

    auto enter_declaration(const TargetDecl& declaration) noexcept -> bool;
};

auto DeclarationFacts::enter_declaration(const TargetDecl& declaration) noexcept -> bool {
    if (const auto* record = std::get_if<TargetStructDecl>(&declaration)) {
        record_defined |= record->name.spelling() == "Entry";
    }
    if (const auto* variable = std::get_if<TargetVariableDecl>(&declaration)) {
        CHECK(record_defined);
        CHECK(variable->constexpr_specifier);
        const auto* array = std::get_if<TargetArrayExpr>(&variable->initializer.value);
        REQUIRE(array != nullptr);
        REQUIRE(array->elements.size() == 2uz);
        for (const auto& element : array->elements) {
            auto facts = ExpressionFacts();
            REQUIRE(traverse_target_expression(element, facts));
            CHECK(facts.constructions == 1uz);
            CHECK(facts.calls == 0uz);
        }
        ++storage;
    }
    const auto* function = std::get_if<TargetFunctionDecl>(&declaration);
    if (function == nullptr) {
        return true;
    }
    const auto* definition = std::get_if<TargetFreeFunctionDefinition>(&function->form);
    if (definition == nullptr) {
        return true;
    }
    const auto name = function->name.components().back().spelling();
    if (name != "frozen" && name != "ordinary") {
        return true;
    }
    auto facts = ExpressionFacts();
    REQUIRE(traverse_target_statements(definition->body, facts));
    if (name == "frozen") {
        CHECK(facts.constructions == 1uz);
        CHECK(facts.calls == 0uz);
        ++frozen;
    } else {
        CHECK(facts.calls == 1uz);
        ++ordinary;
    }
    return true;
}

} // namespace

TEST_CASE(
    "Generation: frozen user records use native aggregate data after complete type definitions"
) {
    const auto compilation = PlannedCompilation::build(
        analyze_test_program(R"(
        private struct Entry { key: i32 }
        private const fn make(value: i32) -> Entry => Entry { value };
        private fn frozen() -> Entry { const entry = make(7); return entry; }
        private fn ordinary(value: i32) -> Entry => make(value);
        private fn read(index: i32) -> i32 {
            const entries: [Entry] = [Entry { 1 }, Entry { 2 }];
            return entries[index].key;
        }
        fn exercise() -> i32 => frozen().key + ordinary(2).key + read(0);
    )"),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("constant_structs")}
    );
    auto totals = DeclarationFacts();
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        auto facts = DeclarationFacts();
        REQUIRE(traverse_target_unit(unit.sections(), facts));
        totals.storage += facts.storage;
        totals.frozen += facts.frozen;
        totals.ordinary += facts.ordinary;
    }
    CHECK(totals.storage == 1uz);
    CHECK(totals.frozen == 1uz);
    CHECK(totals.ordinary == 1uz);
}
