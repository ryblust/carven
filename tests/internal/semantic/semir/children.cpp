module carven:test.internal.semantic.semir.children;

import :semantic.analysis.body.builder;
import :semantic.semir.children;
import :semantic.semir.completion;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :test.harness.framework;
import :test.internal.semantic.semir.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "SemIR trees: deep copies preserve independent storage and completion",
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto draft = semir_test::begin_compilation(sources, diagnostics, "semir.copy");
            const auto origin = semir_test::module_facts(draft).origin;
            const auto integer = draft.builtin_type(BuiltinType::I32);
            const auto constant =
                draft.intern_constant({.type = integer, .value = IntegerConstant::zero()});
            auto body = BodyBuilder(draft.reserve_body(BodyKind::Test), draft);
            const auto lifetime =
                body.add_lifetime_region(std::nullopt, LifetimeRegionKind::Lexical, origin);
            auto expression =
                body.make_expression(integer, lifetime, origin, SemConstant {.constant = constant});
            constexpr auto depth = 20'000uz;
            for (auto index = 0uz; index < depth; ++index) {
                expression = body.make_expression(
                    integer,
                    lifetime,
                    origin,
                    SemUnary {
                        .operation = UnaryOperator::Negate,
                        .operand = UniqueIndirect(std::move(expression))
                    }
                );
            }
            auto copied = expression;
            ct::expect(exits(expression) == ExitSet(Exit::Normal));
            ct::expect(exits(copied) == ExitSet(Exit::Normal));
            auto observed_depth = 0uz;
            visit_semantic_nodes(copied, [&](SemanticExpression& node) noexcept {
                node.exits_test = true;
                observed_depth += std::holds_alternative<SemUnary>(node.value);
            });
            ct::expect_equal(observed_depth, depth);
            auto unchanged = true;
            visit_semantic_nodes(expression, [&](const SemanticExpression& node) noexcept {
                unchanged &= !node.exits_test;
            });
            ct::expect(unchanged);
        }
    );

    ct::test("SemIR children: direct ordered borrows preserve nested storage", [] static noexcept {
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
            return body
                .make_expression(integer, lifetime, origin, SemConstant {.constant = constant});
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
        if (!ct::expect(children.size() == 2)) {
            return;
        }
        ct::expect(children[0] == &*operation.left);
        ct::expect(children[1] == &*operation.right);
        visit_semantic_children(operation, [](SemanticExpression& child) static noexcept {
            child.exits_test = true;
        });
        ct::expect(operation.left->exits_test);
        ct::expect(operation.right->exits_test);
        const auto* nested = std::get_if<SemUnary>(&operation.left->value);
        if (!ct::expect(nested != nullptr)) {
            return;
        }
        ct::expect(!(nested->operand->exits_test));
    });
});

} // namespace
