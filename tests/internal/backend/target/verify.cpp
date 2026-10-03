module carven:test.internal.backend.target.verify;

import :backend.target.builder;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.item;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.stmt;
import :backend.target.symbol;
import :backend.target.type;
import :backend.target.unit;
import :backend.target.verify;
import :backend.target;
import :support.unique_indirect;
import :test.harness.framework;
import :test.internal.backend.target.fixture;
import :test.internal.harness.death;
import std;

namespace {

namespace ct = carven::testing;

auto identifier(std::string_view spelling) noexcept -> TargetIdentifier {
    return TargetIdentifier::from_spelling(spelling);
}

auto attribution() noexcept -> TargetAttribution {
    return TargetGeneratedExpansionAttribution {
        .reason = TargetExpansionReason::LoweringSupport,
    };
}

auto literal(bool value = true) noexcept -> TargetExpr {
    return {.value = TargetLiteralExpr {.value = value}};
}

auto one_statement(TargetStmt statement) noexcept -> std::vector<TargetStmt> {
    auto result = std::vector<TargetStmt>();
    result.push_back(std::move(statement));
    return result;
}

auto one_item(TargetItem item) noexcept -> std::vector<TargetItem> {
    auto result = std::vector<TargetItem>();
    result.push_back(std::move(item));
    return result;
}

auto sections(std::vector<TargetItem> body = {}) noexcept -> TargetUnitSections {
    return {.preamble = {}, .body = std::move(body), .epilogue = {}};
}

auto bool_type() noexcept -> TargetType {
    return {
        .value =
            TargetIntrinsicType {
                .symbol = TargetSymbol::Bool,
                .type_argument_ids = {},
            },
        .const_qualified = false,
    };
}

auto function(TargetTypeID result, std::vector<TargetStmt> body) noexcept -> TargetItem {
    return {
        .value = TargetDecl {TargetFunctionDecl {
            .name = TargetName(identifier("fixture")),
            .parameters = {},
            .result = result,
            .form = TargetFreeFunctionDefinition {.body = std::move(body)},
            .constexpr_specifier = false,
            .static_specifier = false,
            .inline_specifier = false,
        }},
        .attribution = attribution(),
    };
}

auto require_violation(
    TargetUnitIdentity identity,
    std::span<const TargetType> types,
    const TargetUnitSections& unit_sections,
    TargetSealViolationKind kind,
    std::size_t local_count = 0
) noexcept -> void {
    const auto result =
        TargetTestingFixture::validate_unit(identity, types, unit_sections, local_count);
    if (!ct::expect(!(result.has_value()))) {
        return;
    }
    ct::expect_equal(result.error().kind, kind);
}

template<typename T>
concept PrimaryExpressionBorrowable = requires (T&& expression) {
    { template_primary_expression(std::forward<T>(expression)) } -> std::same_as<const TargetExpr&>;
};

static_assert(PrimaryExpressionBorrowable<TargetExpr&>);
static_assert(PrimaryExpressionBorrowable<const TargetExpr&>);
static_assert(!PrimaryExpressionBorrowable<TargetExpr>);
static_assert(!PrimaryExpressionBorrowable<const TargetExpr>);

static_assert(!std::copy_constructible<TargetExpr>);
static_assert(!std::copy_constructible<TargetStmt>);
static_assert(std::move_constructible<TargetExpr>);
static_assert(std::move_constructible<TargetStmt>);
static_assert(std::move_constructible<TargetItem>);
static_assert(!std::constructible_from<TargetUnitBuilder, TargetArtifactID>);
static_assert(!std::constructible_from<TargetTypeID, TargetUnitIdentity, std::uint32_t>);
static_assert(!std::is_move_assignable_v<TargetUnit>);
static_assert(std::ranges::range<TargetPlanTableEntries<int, TargetArtifactID>>);

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test("Target builder: template query identity retains every argument", [] static noexcept {
        auto builder = TargetTestingFixture::unit_builder();
        const auto type = builder.intern_type(bool_type());
        const auto query =
            [&](std::vector<TargetTemplateArgument> arguments) noexcept -> TargetType {
            return {
                .value = TargetDecltypeType(template_name_expression(
                    TargetExpr {.value = TargetNameExpr {.name = TargetName(identifier("policy"))}},
                    std::move(arguments)
                )),
                .const_qualified = false,
            };
        };
        struct Scenario final {
            std::string_view name;
            std::vector<TargetTemplateArgument> arguments;
        };
        const auto scenarios = std::array {
            Scenario {"type and true", {type, true}},
            Scenario {"type and false", {type, false}},
            Scenario {
                "integer argument",
                {TargetIntegerLiteral {
                    .negative = false,
                    .magnitude = 3,
                    .suffix = TargetIntegerSuffix::None,
                }}
            },
            Scenario {"explicit empty arguments", {}},
        };
        auto identities = std::vector<TargetTypeID>();
        ct::each(scenarios, &Scenario::name, [&](const Scenario& scenario) noexcept {
            const auto first = builder.intern_type(query(scenario.arguments));
            const auto repeated = builder.intern_type(query(scenario.arguments));
            ct::expect_equal(first.index(), repeated.index());
            ct::expect(std::ranges::find(identities, first) == identities.end());
            identities.push_back(first);
        });
    });

    ct::test(
        "Target verifier: standalone template arguments belong to the unit",
        [] static noexcept {
            const auto owner = TargetTestingFixture::unit_identity();
            const auto result_type = TargetTestingFixture::type_id(owner, 0);
            const auto foreign =
                TargetTestingFixture::type_id(TargetTestingFixture::unit_identity(), 0);
            const auto types = std::array {bool_type()};
            struct Scenario final {
                std::string_view name;
                TargetTypeID argument;
                bool accepted;
            };
            const auto scenarios = std::array {
                Scenario {"unit-owned argument", result_type, true},
                Scenario {"foreign argument", foreign, false},
            };
            ct::each(scenarios, &Scenario::name, [&](const Scenario& scenario) noexcept {
                auto body = one_statement({
                    .value =
                        TargetExprStmt {
                            .expression = template_name_expression(
                                TargetExpr {
                                    .value =
                                        TargetNameExpr {.name = TargetName(identifier("value"))}
                                },
                                {scenario.argument}
                            ),
                        },
                    .attribution = attribution(),
                });
                const auto result = TargetTestingFixture::validate_unit(
                    owner,
                    types,
                    sections(one_item(function(result_type, std::move(body))))
                );
                if (scenario.accepted) {
                    ct::expect(result.has_value());
                } else if (ct::expect(!result.has_value())) {
                    ct::expect_equal(
                        result.error().kind,
                        TargetSealViolationKind::InvalidTypeReference
                    );
                }
            });
        }
    );

    ct::test("Target type construction: children already belong to the unit", [] static noexcept {
        auto builder = TargetUnitBuilder();
        const auto foreign = TargetTestingFixture::unit_identity();
        const auto invalid = std::array {
            TargetTestingFixture::type_id(foreign, 0),
            TargetTestingFixture::type_id(builder.identity(), 0),
            TargetTestingFixture::type_id(builder.identity(), 7),
        };
        for (const auto [index, child] : invalid | std::views::enumerate) {
            ct::expect(expect_termination(std::format("target-type-child-{}", index), [&] noexcept {
                static_cast<void>(builder.intern_type(
                    TargetType {
                        .value =
                            TargetArrayType {
                                .element_type_id = child,
                                .extent = TargetArrayExtent {.magnitude = 2}
                            },
                        .const_qualified = false,
                    }
                ));
            }));
        }
        const auto element = builder.intern_type(bool_type());
        [[maybe_unused]] const auto array = builder.intern_type(
            TargetType {
                .value =
                    TargetArrayType {
                        .element_type_id = element,
                        .extent = TargetArrayExtent {.magnitude = 2}
                    },
                .const_qualified = false,
            }
        );
    });

    ct::test("Target jump verifier: entering an empty nested scope is legal", [] static noexcept {
        const auto owner = TargetTestingFixture::unit_identity();
        const auto type = TargetTestingFixture::type_id(owner, 0);
        auto body = std::vector<TargetStmt>();
        body.push_back({
            .value =
                TargetGotoStmt {
                    .label = identifier("inside"),
                    .role = TargetJumpRole::RegionExit,
                },
            .attribution = attribution(),
        });
        body.push_back({
            .value =
                TargetBlockStmt {
                    .statements = one_statement(
                        TargetStmt {
                            .value =
                                TargetLabelStmt {
                                    .label = identifier("inside"),
                                    .role = TargetJumpRole::RegionExit,
                                },
                            .attribution = attribution(),
                        }
                    ),
                },
            .attribution = attribution(),
        });
        const auto types = std::array {bool_type()};
        ct::expect(
            TargetTestingFixture::validate_unit(
                owner,
                types,
                sections(one_item(function(type, std::move(body))))
            )
                .has_value()
        );
    });

    ct::test("Target jump verifier: region exit cannot jump backward", [] static noexcept {
        const auto owner = TargetTestingFixture::unit_identity();
        const auto type = TargetTestingFixture::type_id(owner, 0);
        auto body = std::vector<TargetStmt>();
        body.push_back({
            .value =
                TargetLabelStmt {
                    .label = identifier("loop"),
                    .role = TargetJumpRole::RegionExit,
                },
            .attribution = attribution(),
        });
        body.push_back({
            .value =
                TargetGotoStmt {
                    .label = identifier("loop"),
                    .role = TargetJumpRole::RegionExit,
                },
            .attribution = attribution(),
        });
        const auto types = std::array {bool_type()};
        ct::expect(!(TargetTestingFixture::validate_unit(
                         owner,
                         types,
                         sections(one_item(function(type, std::move(body))))
        )
                         .has_value()));
    });

    ct::test("Target jump verifier: entering past initialization is rejected", [] static noexcept {
        const auto owner = TargetTestingFixture::unit_identity();
        const auto type = TargetTestingFixture::type_id(owner, 0);
        auto nested = std::vector<TargetStmt>();
        nested.push_back({
            .value =
                TargetVariableStmt {
                    .binding = TargetVariableBinding::ConstValue,
                    .maybe_unused = false,
                    .local = TargetTestingFixture::local_id(owner, 0),
                    .type = type,
                    .initializer = literal(),
                },
            .attribution = attribution(),
        });
        nested.push_back({
            .value =
                TargetLabelStmt {
                    .label = identifier("inside"),
                    .role = TargetJumpRole::RegionExit,
                },
            .attribution = attribution(),
        });
        auto body = std::vector<TargetStmt>();
        body.push_back({
            .value =
                TargetGotoStmt {
                    .label = identifier("inside"),
                    .role = TargetJumpRole::RegionExit,
                },
            .attribution = attribution(),
        });
        body.push_back({
            .value =
                TargetBlockStmt {
                    .statements = std::move(nested),
                },
            .attribution = attribution(),
        });
        const auto types = std::array {bool_type()};
        require_violation(
            owner,
            types,
            sections(one_item(function(type, std::move(body)))),
            TargetSealViolationKind::InvalidControl,
            1
        );
    });

    ct::test(
        "Target builder: interning is unit-owned and unused types add no dependency",
        [] static noexcept {
            auto builder = TargetTestingFixture::unit_builder();
            static_cast<void>(builder.intern_type(
                TargetType {
                    .value =
                        TargetIntrinsicType {
                            .symbol = TargetSymbol::RuntimeFunctionRef,
                            .type_argument_ids = {},
                        },
                    .const_qualified = false,
                }
            ));
            const auto first = builder.intern_type(bool_type());
            const auto second = builder.intern_type(bool_type());
            const auto unit = std::move(builder).finish(sections(one_item(function(first, {}))));

            ct::expect((first == second));
            ct::expect_equal(unit.type_count(), 2uz);
            ct::expect(unit.directive_groups().empty());
        }
    );

    ct::test(
        "Target builder: type queries retain call structure across table growth",
        [] static noexcept {
            auto builder = TargetTestingFixture::unit_builder();
            const auto query = [](bool grouped) static noexcept -> TargetType {
                const auto name = [](std::string_view text) static noexcept -> TargetExpr {
                    return {.value = TargetNameExpr {.name = TargetName(identifier(text))}};
                };
                auto arguments = std::vector<TargetExpr>();
                arguments.push_back(name("a"));
                if (grouped) {
                    arguments.push_back(name("b"));
                }
                auto nested = TargetExpr {
                    .value = TargetCallExpr {
                        .callee = UniqueIndirect(name("g")),
                        .arguments = std::move(arguments),
                    }
                };
                auto outer = std::vector<TargetExpr>();
                outer.push_back(std::move(nested));
                if (!grouped) {
                    outer.push_back(name("b"));
                }
                return {
                    .value = TargetDecltypeType(
                        TargetExpr {
                            .value =
                                TargetCallExpr {
                                    .callee = UniqueIndirect(name("f")),
                                    .arguments = std::move(outer),
                                }
                        }
                    ),
                    .const_qualified = false,
                };
            };
            const auto separate = builder.intern_type(query(false));
            const auto grouped = builder.intern_type(query(true));
            ct::expect((separate != grouped));
            for (auto index = 0uz; index < 128uz; ++index) {
                static_cast<void>(builder.intern_type(
                    TargetType {
                        .value =
                            TargetNamedType {
                                .name = TargetName(identifier(std::format("T{}", index))),
                                .type_argument_ids = {},
                                .nested = {},
                            },
                        .const_qualified = false,
                    }
                ));
            }
            ct::expect((builder.intern_type(query(false)) == separate));
            ct::expect((builder.intern_type(query(true)) == grouped));
            const auto unit = std::move(builder).finish(sections());
            ct::expect_equal(unit.type_count(), 130uz);
        }
    );

    ct::test(
        "Target locals: references require a unique visible declaration in their unit",
        [] static noexcept {
            enum class Reference { Visible, Foreign, OutOfRange, Duplicate, Escaped, Undeclared };

            struct Scenario final {
                std::string_view name;
                Reference reference;
            };

            const auto scenarios = std::array {
                Scenario {.name = "visible local", .reference = Reference::Visible},
                Scenario {.name = "foreign unit", .reference = Reference::Foreign},
                Scenario {.name = "out of range", .reference = Reference::OutOfRange},
                Scenario {.name = "duplicate declaration", .reference = Reference::Duplicate},
                Scenario {.name = "escaped scope", .reference = Reference::Escaped},
                Scenario {.name = "undeclared local", .reference = Reference::Undeclared},
            };
            ct::each(scenarios, &Scenario::name, [](const Scenario& scenario) static noexcept {
                const auto owner = TargetTestingFixture::unit_identity();
                const auto type = TargetTestingFixture::type_id(owner, 0);
                const auto types = std::array {bool_type()};
                const auto id = TargetTestingFixture::local_id(owner, 0);
                auto selected = id;
                if (scenario.reference == Reference::Foreign) {
                    selected =
                        TargetTestingFixture::local_id(TargetTestingFixture::unit_identity(), 0);
                } else if (scenario.reference == Reference::OutOfRange) {
                    selected = TargetTestingFixture::local_id(owner, 1);
                }
                auto body = std::vector<TargetStmt>();
                const auto declaration = [&]() noexcept -> TargetStmt {
                    return {
                        .value =
                            TargetVariableStmt {
                                .binding = TargetVariableBinding::ConstValue,
                                .maybe_unused = false,
                                .local = id,
                                .type = type,
                                .initializer = literal()
                            },
                        .attribution = attribution()
                    };
                };
                if (scenario.reference == Reference::Escaped) {
                    body.push_back(
                        {.value = TargetBlockStmt {.statements = one_statement(declaration())},
                         .attribution = attribution()}
                    );
                } else if (scenario.reference != Reference::Undeclared) {
                    body.push_back(declaration());
                }
                if (scenario.reference == Reference::Duplicate) {
                    body.push_back(declaration());
                }
                body.push_back(
                    {.value =
                         TargetReturnStmt {
                             .expression = TargetExpr {.value = TargetLocalExpr {.local = selected}}
                         },
                     .attribution = attribution()}
                );
                const auto result = TargetTestingFixture::validate_unit(
                    owner,
                    types,
                    sections(one_item(function(type, std::move(body)))),
                    1uz
                );
                if (scenario.reference == Reference::Visible) {
                    ct::expect(result.has_value());
                } else {
                    if (!(ct::expect(!(result.has_value())))) {
                        return;
                    }
                    ct::expect(
                        result.error().kind == TargetSealViolationKind::InvalidLocalReference
                    );
                }
            });
        }
    );
});

} // namespace
