module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.semir.children;

import :semantic.analysis.body.builder;
import :semantic.semir.children;
import :semantic.semir.structured;
import :test.internal.semantic.semir.fixture;
import std;

TEST_CASE("SemIR children: direct ordered borrows preserve nested storage") {
    auto sources = SourceManager();
    auto diagnostics = DiagnosticSink();
    auto draft = semir_test::begin_compilation(sources, diagnostics, "semir.children");
    const auto origin = semir_test::module_facts(draft).origin;
    const auto integer = draft.builtin_type(BuiltinType::I32);
    const auto constant =
        draft.intern_constant({.type = integer, .value = IntegerConstant::zero()});
    auto body = BodyBuilder(draft.reserve_body(BodyKind::Test), draft);
    const auto lifetime =
        body.add_lifetime_region(std::nullopt, LifetimeRegionKind::Lexical, origin);
    const auto leaf = [&]() noexcept {
        return body.make_expression(integer, lifetime, origin, SemConstant {.constant = constant});
    };
    auto operation = SemBinary {
        .left = UniqueIndirect(body.make_expression(
            integer,
            lifetime,
            origin,
            SemUnary {
                .operation = UnaryOperator::Negate,
                .operand = UniqueIndirect(leaf()),
            }
        )),
        .operation = BinaryOperator::Add,
        .right = UniqueIndirect(leaf()),
    };
    auto children = std::vector<const SemanticExpression*>();
    visit_semantic_children(
        std::as_const(operation),
        [&](const SemanticExpression& child) noexcept { children.push_back(&child); }
    );
    REQUIRE(children.size() == 2);
    CHECK(children[0] == &*operation.left);
    CHECK(children[1] == &*operation.right);
    visit_semantic_children(operation, [](SemanticExpression& child) static noexcept {
        child.exits_test = true;
    });
    CHECK(operation.left->exits_test);
    CHECK(operation.right->exits_test);
    const auto* nested = std::get_if<SemUnary>(&operation.left->value);
    REQUIRE(nested != nullptr);
    CHECK_FALSE(nested->operand->exits_test);
}
