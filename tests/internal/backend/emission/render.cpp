module carven:test.internal.backend.emission.render;

import :artifacts;
import :backend.emission.emit;
import :backend.emission.string;
import :backend.target.builder;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.item;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :backend.target.unit;
import :support.unique_indirect;
import :test.harness.framework;
import :test.internal.backend.target.fixture;
import std;

namespace {

namespace ct = carven::testing;

auto attribution() noexcept -> TargetAttribution {
    return TargetGeneratedExpansionAttribution {
        .reason = TargetExpansionReason::LoweringSupport,
    };
}

template<typename Statement>
auto emitted_statement(Statement statement) noexcept -> GeneratedArtifact {
    auto builder = TargetTestingFixture::unit_builder();
    const auto result = builder.intern_type({
        .value =
            TargetIntrinsicType {
                .symbol = TargetSymbol::Void,
                .type_argument_ids = {},
            },
        .const_qualified = false,
    });
    auto body = std::vector<TargetStmt>();
    if constexpr (std::invocable<Statement&, TargetUnitBuilder&>) {
        body.push_back({.value = statement(builder), .attribution = attribution()});
    } else {
        body.push_back({.value = std::move(statement), .attribution = attribution()});
    }
    auto items = std::vector<TargetItem>();
    items.push_back({
        .value = TargetDecl {TargetFunctionDecl {
            .name = TargetName(TargetIdentifier::from_spelling("fixture")),
            .parameters = {},
            .result = result,
            .form = TargetFreeFunctionDefinition {.body = std::move(body)},
            .constexpr_specifier = false,
            .static_specifier = false,
            .inline_specifier = false,
        }},
        .attribution = attribution(),
    });
    auto unit = std::move(builder).finish({
        .preamble = {},
        .body = std::move(items),
        .epilogue = {},
    });
    return emit(
        std::move(unit),
        "fixture.cpp",
        GeneratedArtifactRole::ModuleImplementation,
        SourceAttributedEmission {.generated_origin = "fixture.cpp"}
    );
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test("Emission: C++ string quoting owns escape syntax", [] static noexcept {
        ct::expect_equal(
            cpp_string_token("a\\b\n\"c\t"),
            std::string_view("\"a\\\\b\\012\\\"c\\011\"")
        );
        ct::expect_equal(
            cpp_string_token(std::string_view("\0018\377", 3)),
            std::string_view("\"\\0018\\377\"")
        );
    });

    ct::test("Emission: explicit directive groups are serialized in order", [] static noexcept {
        auto builder = TargetTestingFixture::unit_builder();
        auto unit = std::move(builder).finish(
            {.preamble = {}, .body = {}, .epilogue = {}},
            TargetDirectiveInputs {
                .prefix_groups = {{
                    .directives = {{.bytes = "#custom first"}, {.bytes = "#custom second"}},
                    .attribution = std::nullopt,
                }},
                .suffix_groups = {},
            }
        );
        const auto artifact = emit(
            std::move(unit),
            "custom.cpp",
            GeneratedArtifactRole::ModuleImplementation,
            SourceAttributedEmission {.generated_origin = "custom.cpp"}
        );

        ct::expect(artifact.content.contains("#custom first\n#custom second"));
        ct::expect(!(artifact.content.contains("carven/runtime")));
    });

    ct::test("Emission: verified unreachable uses the C++20 runtime leaf", [] static noexcept {
        const auto artifact = emitted_statement(
            TargetUnreachableStmt {
                .reason = TargetUnreachableReason::SemIRProof,
            }
        );

        ct::expect(artifact.content.contains("#include <carven/runtime/unreachable.hpp>"));
        ct::expect(artifact.content.contains("carven::runtime::unreachable();"));
        ct::expect(!(artifact.content.contains("std::unreachable")));
        ct::expect(!(artifact.content.contains("std::abort();")));
    });

    ct::test("Emission: runtime trap remains distinct from unreachable proof", [] static noexcept {
        const auto artifact = emitted_statement(
            TargetRuntimeTrapStmt {
                .reason = TargetRuntimeTrapReason::SourceContract,
            }
        );

        ct::expect(artifact.content.contains("#include <cstdlib>"));
        ct::expect(artifact.content.contains("std::abort();"));
        ct::expect(!(artifact.content.contains("carven::runtime::unreachable();")));
    });

    ct::test(
        "Emission: binary grouping preserves associativity and makes comparisons explicit",
        [] static noexcept {
            struct Case final {
                TargetBinaryOperator outer;
                TargetBinaryOperator inner;
                bool nested_left;
                std::string_view expected;
            };

            const auto cases = std::array {
                Case {
                    .outer = TargetBinaryOperator::LogicalOr,
                    .inner = TargetBinaryOperator::LogicalAnd,
                    .nested_left = true,
                    .expected = "(a && b) || c;"
                },
                Case {
                    .outer = TargetBinaryOperator::LogicalOr,
                    .inner = TargetBinaryOperator::LogicalAnd,
                    .nested_left = false,
                    .expected = "a || (b && c);"
                },
                Case {
                    .outer = TargetBinaryOperator::Equal,
                    .inner = TargetBinaryOperator::Equal,
                    .nested_left = true,
                    .expected = "(a == b) == c;"
                },
                Case {
                    .outer = TargetBinaryOperator::Equal,
                    .inner = TargetBinaryOperator::Equal,
                    .nested_left = false,
                    .expected = "a == (b == c);"
                },
                Case {
                    .outer = TargetBinaryOperator::Less,
                    .inner = TargetBinaryOperator::Less,
                    .nested_left = true,
                    .expected = "(a < b) < c;"
                },
                Case {
                    .outer = TargetBinaryOperator::Less,
                    .inner = TargetBinaryOperator::Less,
                    .nested_left = false,
                    .expected = "a < (b < c);"
                },
                Case {
                    .outer = TargetBinaryOperator::Equal,
                    .inner = TargetBinaryOperator::Less,
                    .nested_left = true,
                    .expected = "(a < b) == c;"
                },
                Case {
                    .outer = TargetBinaryOperator::Equal,
                    .inner = TargetBinaryOperator::Less,
                    .nested_left = false,
                    .expected = "a == (b < c);"
                },
                Case {
                    .outer = TargetBinaryOperator::Less,
                    .inner = TargetBinaryOperator::Equal,
                    .nested_left = true,
                    .expected = "(a == b) < c;"
                },
                Case {
                    .outer = TargetBinaryOperator::Less,
                    .inner = TargetBinaryOperator::Equal,
                    .nested_left = false,
                    .expected = "a < (b == c);"
                },
                Case {
                    .outer = TargetBinaryOperator::BitwiseAnd,
                    .inner = TargetBinaryOperator::Equal,
                    .nested_left = true,
                    .expected = "(a == b) & c;"
                },
                Case {
                    .outer = TargetBinaryOperator::BitwiseOr,
                    .inner = TargetBinaryOperator::Less,
                    .nested_left = false,
                    .expected = "a | (b < c);"
                },
                Case {
                    .outer = TargetBinaryOperator::Subtract,
                    .inner = TargetBinaryOperator::Subtract,
                    .nested_left = true,
                    .expected = "a - b - c;"
                },
                Case {
                    .outer = TargetBinaryOperator::Subtract,
                    .inner = TargetBinaryOperator::Subtract,
                    .nested_left = false,
                    .expected = "a - (b - c);"
                },
                Case {
                    .outer = TargetBinaryOperator::Multiply,
                    .inner = TargetBinaryOperator::Add,
                    .nested_left = true,
                    .expected = "(a + b) * c;"
                },
                Case {
                    .outer = TargetBinaryOperator::Add,
                    .inner = TargetBinaryOperator::Multiply,
                    .nested_left = false,
                    .expected = "a + b * c;"
                },
                Case {
                    .outer = TargetBinaryOperator::LeftShift,
                    .inner = TargetBinaryOperator::Add,
                    .nested_left = true,
                    .expected = "(a + b) << c;"
                },
                Case {
                    .outer = TargetBinaryOperator::RightShift,
                    .inner = TargetBinaryOperator::Add,
                    .nested_left = false,
                    .expected = "a >> (b + c);"
                },
            };
            const auto name = [](std::string_view spelling) static noexcept {
                return TargetExpr {
                    .value = TargetNameExpr {
                        .name = TargetName(TargetIdentifier::from_spelling(spelling))
                    }
                };
            };
            ct::each(
                cases,
                [](const Case& scenario) static noexcept -> std::string_view {
                    return scenario.expected;
                },
                [&](const Case& scenario) noexcept {
                    auto expression = scenario.nested_left
                        ? binary_expression(
                              binary_expression(name("a"), scenario.inner, name("b")),
                              scenario.outer,
                              name("c")
                          )
                        : binary_expression(
                              name("a"),
                              scenario.outer,
                              binary_expression(name("b"), scenario.inner, name("c"))
                          );
                    const auto artifact =
                        emitted_statement(TargetExprStmt {.expression = std::move(expression)});
                    ct::expect(artifact.content.contains(scenario.expected))
                        .note("scenario.expected: ", scenario.expected);
                }
            );
        }
    );

    ct::test("Emission: owning snapshots annotate only their binding name", [] static noexcept {
        struct Scenario final {
            std::string_view name;
            TargetVariableBinding binding;
            std::string_view declaration;
        };
        const auto scenarios = std::array {
            Scenario {
                .name = "ordinary value",
                .binding = TargetVariableBinding::ConstValue,
                .declaration = "const bool value"
            },
            Scenario {
                .name = "owning snapshot",
                .binding = TargetVariableBinding::ConstSnapshot,
                .declaration =
                    "const bool value /* NOLINT(performance-unnecessary-copy-initialization) */"
            }
        };
        ct::each(scenarios, &Scenario::name, [](const auto& scenario) static noexcept {
            const auto artifact = emitted_statement([&](TargetUnitBuilder& builder) noexcept {
                return TargetVariableStmt {
                    .binding = scenario.binding,
                    .maybe_unused = false,
                    .local = builder.add_local(TargetIdentifier::from_spelling("value")),
                    .type = builder.intern_type(
                        {.value =
                             TargetIntrinsicType {
                                 .symbol = TargetSymbol::Bool,
                                 .type_argument_ids = {}
                             },
                         .const_qualified = false}
                    ),
                    .initializer = TargetExpr {.value = TargetLiteralExpr {.value = true}}
                };
            });
            ct::expect(artifact.content.contains(scenario.declaration)).note(artifact.content);
            ct::expect(artifact.content.contains("= true;"));
            ct::expect_equal(
                artifact.content.contains("NOLINT"),
                scenario.binding == TargetVariableBinding::ConstSnapshot
            );
        });
    });

    ct::test(
        "Emission: value regions retain explicit result types and selective unused names",
        [] static noexcept {
            for (const auto maybe_unused : {false, true}) {
                const auto artifact =
                    emitted_statement([&](TargetUnitBuilder& builder) noexcept -> TargetStmtValue {
                        const auto type = builder.intern_type({
                            .value =
                                TargetIntrinsicType {
                                    .symbol = TargetSymbol::Bool,
                                    .type_argument_ids = {}
                                },
                            .const_qualified = false,
                        });
                        auto body = std::vector<TargetStmt>();
                        body.push_back({
                            .value =
                                TargetReturnStmt {
                                    .expression =
                                        TargetExpr {.value = TargetLiteralExpr {.value = true}}
                                },
                            .attribution = attribution(),
                        });
                        return TargetVariableStmt {
                            .binding = TargetVariableBinding::ConstValue,
                            .maybe_unused = maybe_unused,
                            .local = builder.add_local(TargetIdentifier::from_spelling("value")),
                            .type = type,
                            .initializer = TargetExpr {
                                .value = TargetCallExpr {
                                    .callee = UniqueIndirect(
                                        TargetExpr {
                                            .value =
                                                TargetLambdaExpr {
                                                    .parameters = {},
                                                    .result = type,
                                                    .body = std::move(body)
                                                }
                                        }
                                    ),
                                    .template_arguments = {},
                                    .arguments = {}
                                }
                            },
                        };
                    });
                ct::expect_equal(artifact.content.contains("[[maybe_unused]]"), maybe_unused);
                ct::expect(artifact.content.contains("const bool value"));
                ct::expect(artifact.content.contains("[&]() noexcept -> bool"));
                ct::expect(artifact.content.contains("return true;"));
            }
        }
    );

    ct::test("Emission: range-for preserves native binding and loop scope", [] static noexcept {
        for (const auto binding :
             {TargetVariableBinding::ConstValue, TargetVariableBinding::ConstReference}) {
            const auto artifact = emitted_statement([&](TargetUnitBuilder& builder) noexcept {
                const auto type = builder.intern_type({
                    .value =
                        TargetIntrinsicType {.symbol = TargetSymbol::Int, .type_argument_ids = {}},
                    .const_qualified = false,
                });
                return TargetRangeForStmt {
                    .binding = binding,
                    .maybe_unused = false,
                    .local = builder.add_local(TargetIdentifier::from_spelling("element")),
                    .type = type,
                    .range =
                        TargetExpr {
                            .value =
                                TargetNameExpr {
                                    .name = TargetName(TargetIdentifier::from_spelling("elements"))
                                }
                        },
                    .body = {},
                };
            });
            ct::expect(artifact.content.contains(
                binding == TargetVariableBinding::ConstValue ? "for (const int element : elements)"
                                                             : "for (const int& element : elements)"
            ));
        }
    });

    ct::test(
        "Emission: expression type queries preserve references unless normalization is explicit",
        [] static noexcept {
            for (const auto normalize : {false, true}) {
                const auto artifact = emitted_statement([&](TargetUnitBuilder& builder) noexcept {
                    const auto member = []() static noexcept -> TargetExpr {
                        return {
                            .value = TargetMemberExpr {
                                .operand = UniqueIndirect(
                                    TargetExpr {
                                        .value =
                                            TargetNameExpr {
                                                .name = TargetName(
                                                    TargetIdentifier::from_spelling("source")
                                                )
                                            }
                                    }
                                ),
                                .name = TargetIdentifier::from_spelling("value")
                            }
                        };
                    };
                    auto type = builder.intern_type(
                        {.value = TargetDecltypeType(member()), .const_qualified = false}
                    );
                    if (normalize) {
                        type = builder.intern_type(
                            {.value =
                                 TargetIntrinsicType {
                                     .symbol = TargetSymbol::StdRemoveCVRef,
                                     .type_argument_ids = {type}
                                 },
                             .const_qualified = false}
                        );
                    }
                    return TargetVariableStmt {
                        .binding = TargetVariableBinding::MutableValue,
                        .maybe_unused = false,
                        .local = builder.add_local(TargetIdentifier::from_spelling("result")),
                        .type = type,
                        .initializer = member()
                    };
                });
                ct::expect(artifact.content.contains("decltype((source.value))"));
                ct::expect(artifact.content.contains("std::remove_cvref_t<") == normalize);
                ct::expect(artifact.content.contains("#include <type_traits>") == normalize);
                ct::expect(!(artifact.content.contains("#include <utility>")));
            }
        }
    );

    ct::test(
        "Emission: deep owned expressions render and release without native recursion",
        [] static noexcept {
            constexpr auto depth = 12000uz;
            auto expression = TargetExpr {.value = TargetLiteralExpr {.value = true}};
            for (auto index = 0uz; index < depth; ++index) {
                expression = TargetExpr {
                    .value = TargetPrefixExpr {
                        .op = TargetPrefixOperator::LogicalNot,
                        .operand = UniqueIndirect(std::move(expression)),
                    }
                };
            }
            const auto artifact =
                emitted_statement(TargetExprStmt {.expression = std::move(expression)});
            ct::expect_equal(
                std::ranges::count(artifact.content, '!'),
                static_cast<std::ptrdiff_t>(depth)
            );
            ct::expect(artifact.content.contains("true"));
        }
    );

    ct::test(
        "Emission: deep type dependencies render without native recursion",
        [] static noexcept {
            constexpr auto depth = 12000uz;
            const auto artifact = emitted_statement([](TargetUnitBuilder& builder) static noexcept {
                auto type = builder.intern_type({
                    .value =
                        TargetIntrinsicType {.symbol = TargetSymbol::Void, .type_argument_ids = {}},
                    .const_qualified = false,
                });
                for (auto index = 0uz; index < depth; ++index) {
                    type = builder.intern_type({
                        .value = TargetPointerType {.pointee = type},
                        .const_qualified = false,
                    });
                }
                return TargetExprStmt {
                    .expression = TargetExpr {
                        .value = TargetStaticCastExpr {
                            .type = type,
                            .operand = UniqueIndirect(
                                TargetExpr {
                                    .value = TargetIntrinsicNameExpr {
                                        .symbol = TargetSymbol::StdNullptr,
                                    }
                                }
                            ),
                        }
                    }
                };
            });
            ct::expect_equal(
                std::ranges::count(artifact.content, '*'),
                static_cast<std::ptrdiff_t>(depth)
            );
            ct::expect(artifact.content.contains("nullptr"));
        }
    );
});

} // namespace
