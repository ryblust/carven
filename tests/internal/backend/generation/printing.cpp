module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.generation.printing;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target.name;
import :backend.target.symbol;
import :backend.target.traversal;
import :backend.target.type;
import :backend.target;
import :test.internal.semantic.analysis.fixture;
import std;

TEST_CASE(
    "Generation: known printing arguments retain source effects and inner String construction"
) {
    const auto compilation = PlannedCompilation::build(
        analyze_test_program(
            "fn touch() -> bool { return true; } "
            "fn known() { println(42, touch() && false, f\"{7}\"); } "
            "fn dynamic(value: i32) { println(value); }"
        ),
        {.test_mode = TestGenerationMode::None,
         .linkage_domain = *LinkageDomain::explicit_value("known_printing")}
    );

    struct Query final {
        const TargetUnit& unit;
        std::size_t outputs = 0uz;
        std::size_t known_arguments = 0uz;
        std::size_t effects = 0uz;
        std::size_t constructions = 0uz;

        auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool {
            const auto* call = std::get_if<TargetCallExpr>(&expression.value);
            if (call == nullptr) {
                return true;
            }
            if (const auto* intrinsic =
                    std::get_if<TargetIntrinsicNameExpr>(&call->callee->value)) {
                outputs += intrinsic->symbol == TargetSymbol::RuntimePrintln;
                if (intrinsic->symbol == TargetSymbol::RuntimePrintln) {
                    for (const auto& argument : call->arguments) {
                        if (const auto* literal = std::get_if<TargetLiteralExpr>(&argument.value)) {
                            if (const auto* text =
                                    std::get_if<TargetStringLiteral>(&literal->value)) {
                                known_arguments += text->bytes == "42" || text->bytes == "false";
                            }
                        }
                    }
                }
            }
            if (const auto* name = std::get_if<TargetNameExpr>(&call->callee->value)) {
                effects += name->name.components().back().spelling() == "touch";
            }
            if (const auto* member = std::get_if<TargetStaticMemberExpr>(&call->callee->value)) {
                if (const auto* type =
                        std::get_if<TargetIntrinsicType>(&unit.type(member->owner).value)) {
                    constructions += type->symbol == TargetSymbol::RuntimeString;
                }
            }
            return true;
        }
    };

    auto outputs = 0uz;
    auto known_arguments = 0uz;
    auto effects = 0uz;
    auto constructions = 0uz;
    for (const auto artifact : compilation.target().artifacts()) {
        const auto unit = lower_artifact(compilation, artifact.id);
        auto query = Query {.unit = unit};
        CHECK(traverse_target_unit(unit.sections(), query));
        outputs += query.outputs;
        known_arguments += query.known_arguments;
        effects += query.effects;
        constructions += query.constructions;
    }
    CHECK(outputs == 2uz);
    CHECK(known_arguments == 2uz);
    CHECK(effects == 1uz);
    CHECK(constructions == 1uz);
}
