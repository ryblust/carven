module carven:test.internal.backend.generation.constant_arrays;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.name;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target.type;
import :backend.target;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

struct ArrayFacts final {
    std::size_t arrays = 0uz;
    std::size_t calls = 0uz;
    std::vector<std::string> texts;

    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool;
};

auto ArrayFacts::enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
    -> bool {
    if (const auto* array = std::get_if<TargetArrayExpr>(&expression.value)) {
        ++arrays;
        const auto* literal = std::get_if<TargetLiteralExpr>(&array->extent->value);
        if (!expect(literal != nullptr)) {
            return false;
        }
        const auto* extent = std::get_if<TargetIntegerLiteral>(&literal->value);
        if (!expect(extent != nullptr)) {
            return false;
        }
        expect(extent->magnitude == array->elements.size());
    }
    calls += std::holds_alternative<TargetCallExpr>(expression.value);
    if (const auto* literal = std::get_if<TargetLiteralExpr>(&expression.value)) {
        if (const auto* text = std::get_if<TargetStringLiteral>(&literal->value)) {
            expect(text->kind == TargetStringLiteralKind::StringView);
            texts.push_back(text->bytes);
        }
    }
    return true;
}

struct FunctionQuery final {
    const TargetUnit& unit;
    std::size_t frozen = 0uz;
    std::size_t ordinary = 0uz;

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
    if (!name.starts_with("frozen_") && name != "ordinary") {
        return true;
    }
    auto facts = ArrayFacts();
    if (!expect(traverse_target_statements(definition->body, facts))) {
        return false;
    }
    const auto* result = std::get_if<TargetArrayType>(&unit.type(function->result).value);
    if (!expect(result != nullptr)) {
        return false;
    }
    if (name == "ordinary") {
        expect(facts.calls == 1uz);
        expect(facts.arrays == 0uz);
        ++ordinary;
        return true;
    }
    expect(facts.calls == 0uz);
    expect(facts.arrays == (name == "frozen_nested" ? 3uz : 1uz));
    if (name == "frozen_text") {
        const auto* element =
            std::get_if<TargetIntrinsicType>(&unit.type(result->element_type_id).value);
        if (!expect(element != nullptr)) {
            return false;
        }
        expect_equal(element->symbol, TargetSymbol::StdStringView);
        expect(facts.texts == std::vector<std::string> {std::string("我\0", 4uz), "😀"});
    }
    if (name == "frozen_empty") {
        expect(result->extent.magnitude == 0u);
    }
    ++frozen;
    return true;
}

const TestSuite suite([] static noexcept {
    "Generation: frozen arrays realize typed aggregates without construction calls"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
            const numbers = make_numbers();
            const nested = [[1, 2], [3, 4]];
            const flags = [true, false];
            const letters = ['我', '😀'];
            const texts = ["我\0", "😀"];
            const fn make_numbers() -> [i32; 3] => [1, 2, 3];
            const fn make_empty() -> [i32; 0] => [];
            fn frozen_numbers() -> [i32; 3] => numbers;
            fn frozen_nested() -> [[i32; 2]; 2] => nested;
            fn frozen_flags() -> [bool; 2] => flags;
            fn frozen_letters() -> [char; 2] => letters;
            fn frozen_text() -> [str; 2] => texts;
            fn frozen_empty() -> [i32; 0] { const value = make_empty(); return value; }
            fn ordinary() -> [i32; 3] => make_numbers();
        )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("constant_arrays")}
            );
            auto frozen = 0uz;
            auto ordinary = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = FunctionQuery {.unit = unit};
                expect(traverse_target_unit(unit.sections(), query));
                frozen += query.frozen;
                ordinary += query.ordinary;
            }
            expect(frozen == 6uz);
            expect(ordinary == 1uz);
        };

    "Generation: array loops use established types and direct stable storage"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
            fn fill(size: usize) {
                var data = [0, 0, 0, 0];
                for &element in data { element += size as i32; }
                return data;
            }
        )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("direct_array_loop")}
            );

            struct Query final {
                std::size_t locals = 0uz;
                std::size_t ranges = 0uz;

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    if (std::holds_alternative<TargetVariableStmt>(statement.value)) {
                        ++locals;
                    }
                    if (const auto* range = std::get_if<TargetRangeForStmt>(&statement.value)) {
                        ++ranges;
                        expect(std::holds_alternative<TargetLocalExpr>(range->range.value));
                        expect(range->binding == TargetVariableBinding::MutableReference);
                    }
                    return true;
                }
            };

            auto locals = 0uz;
            auto ranges = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query();
                expect(traverse_target_unit(unit.sections(), query));
                locals += query.locals;
                ranges += query.ranges;
            }
            expect(locals <= 1uz);
            expect(ranges <= 1uz);
        };
});

} // namespace
