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

const TestSuite suite([] static noexcept {
    "SemIR trees: deep mixed storage survives partial moves and replacement"_test =
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto draft = begin_semir_test_compilation(sources, diagnostics, "semir.mixed_storage");
            const auto origin = make_semir_test_module_origin(draft).origin;
            const auto integer = draft.builtin_type(BuiltinType::I32);
            const auto boolean = draft.builtin_type(BuiltinType::Bool);
            const auto constant =
                draft.intern_constant({.type = integer, .value = IntegerConstant::zero()});
            const auto condition =
                draft.intern_constant({.type = boolean, .value = BooleanConstant {.value = true}});
            auto body = BodyBuilder(draft.reserve_body(BodyKind::Test), draft);
            const auto lifetime =
                body.add_lifetime_region(std::nullopt, LifetimeRegionKind::Lexical, origin);
            const auto leaf = [&]() noexcept {
                return body
                    .make_expression(integer, lifetime, origin, SemConstant {.constant = constant});
            };
            auto expression = leaf();
            constexpr auto depth = 10'000uz;
            for (auto index = 0uz; index < depth; ++index) {
                if (index % 2uz == 0uz) {
                    auto region = SemanticRegion {
                        .lifetime = lifetime,
                        .origin = origin,
                        .statements = {},
                        .result = leaf(),
                        .result_reachable = true,
                        .failures = BodyFailures(draft.add_empty_failure_term()),
                        .exits_test = false,


                    };
                    region.statements.push_back(
                        SemanticStatement {
                            .origin = origin,
                            .lifetime = lifetime,
                            .reachable = true,
                            .value = SemExpressionStatement {.expression = std::move(expression)},


                        }
                    );
                    auto branches = std::vector<SemConditionalBranch>();
                    branches.push_back({
                        .condition = body.make_expression(
                            boolean,
                            lifetime,
                            origin,
                            SemConstant {.constant = condition}
                        ),
                        .body = std::move(region),
                    });
                    expression = body.make_expression(
                        integer,
                        lifetime,
                        origin,
                        SemIf {
                            .branches = std::move(branches),
                            .otherwise = std::nullopt,
                            .is_static = false
                        }
                    );
                } else {
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
            }
            const auto check_structure = [&](const SemanticExpression& tree,
                                             std::size_t expression_count) noexcept {
                auto observed = std::array<std::size_t, 3uz> {};
                visit_semantic_nodes(tree, [&](const auto& node) noexcept {
                    using Node = std::remove_cvref_t<decltype(node)>;
                    if constexpr (std::same_as<Node, SemanticExpression>) {
                        ++observed[0];
                    } else if constexpr (std::same_as<Node, SemanticStatement>) {
                        ++observed[1];
                    } else {
                        ++observed[2];
                    }
                });
                expect_equal(observed[0], expression_count);
                expect_equal(observed[1], depth / 2uz);
                expect_equal(observed[2], depth / 2uz);
            };
            constexpr auto expression_count = 1uz + 2uz * depth;
            check_structure(expression, expression_count);
            auto moved = std::move(expression);
            auto* unary = std::get_if<SemUnary>(&moved.value);
            if (!expect(unary != nullptr)) {
                return;
            }
            auto detached = std::move(unary->operand);
            // Both roots contain moved owning edges. Replacing them must not
            // visit those empty edges or disturb the detached live subtree.
            expression = leaf();
            moved = leaf();
            check_structure(*detached, expression_count - 1uz);
            auto restored = body.make_expression(
                integer,
                lifetime,
                origin,
                SemUnary {.operation = UnaryOperator::Negate, .operand = std::move(detached)}
            );
            check_structure(restored, expression_count);
            auto copied = restored;
            expect(exits(restored) == ExitSet(Exit::Normal));
            expect(exits(copied) == ExitSet(Exit::Normal));
            visit_semantic_nodes(copied, [](SemanticExpression& node) static noexcept {
                node.exits_test = true;
            });
            auto unchanged = true;
            visit_semantic_nodes(restored, [&](const SemanticExpression& node) noexcept {
                unchanged &= !node.exits_test;
            });
            expect(unchanged);
            // Replacement destroys the original deep tree; its independent
            // copy remains complete and is destroyed normally at scope exit.
            restored = leaf();
            check_structure(copied, expression_count);
        };

    "SemIR children: direct ordered borrows preserve nested storage"_test = [] static noexcept {
        auto sources = SourceManager();
        auto diagnostics = DiagnosticSink();
        auto draft = begin_semir_test_compilation(sources, diagnostics, "semir.children");
        const auto origin = make_semir_test_module_origin(draft).origin;
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
        if (!expect(children.size() == 2)) {
            return;
        }
        expect(children[0] == &*operation.left);
        expect(children[1] == &*operation.right);
        visit_semantic_children(operation, [](SemanticExpression& child) static noexcept {
            child.exits_test = true;
        });
        expect(operation.left->exits_test);
        expect(operation.right->exits_test);
        const auto* nested = std::get_if<SemUnary>(&operation.left->value);
        if (!expect(nested != nullptr)) {
            return;
        }
        expect(!(nested->operand->exits_test));
    };
});

} // namespace
