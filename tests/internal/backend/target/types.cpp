module carven:test.internal.backend.target.types;

import :backend.target.builder;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.symbol;
import :backend.target.type;
import :support.unique_indirect;
import :test.harness.framework;
import std;

namespace {

auto identifier(std::string_view spelling) noexcept -> TargetIdentifier {
    return TargetIdentifier::from_spelling(spelling);
}

auto bool_type() noexcept -> TargetType {
    return {
        .value = TargetIntrinsicType {.symbol = TargetSymbol::Bool, .type_argument_ids = {}},
        .const_qualified = false,
    };
}

const TestSuite suite([] static noexcept {
    "Target builder: template query identity retains every argument"_test = [] static noexcept {
        auto builder = TargetUnitBuilder();
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
            Scenario {.name = "type and true", .arguments = {type, true}},
            Scenario {.name = "type and false", .arguments = {type, false}},
            Scenario {
                .name = "integer argument",
                .arguments = {TargetIntegerLiteral {
                    .negative = false,
                    .magnitude = 3,
                    .suffix = TargetIntegerSuffix::None,
                }}
            },
            Scenario {.name = "explicit empty arguments", .arguments = {}},
        };
        auto identities = std::vector<TargetTypeID>();
        each(scenarios, &Scenario::name, [&](const Scenario& scenario) noexcept {
            const auto first = builder.intern_type(query(scenario.arguments));
            const auto repeated = builder.intern_type(query(scenario.arguments));
            expect_equal(first.index(), repeated.index());
            expect(std::ranges::find(identities, first) == identities.end());
            identities.push_back(first);
        });
    };

    "Target builder: type queries retain call structure across table growth"_test =
        [] static noexcept {
            auto builder = TargetUnitBuilder();
            constexpr auto query = [](bool grouped) static noexcept -> TargetType {
                constexpr auto name = [](std::string_view text) static noexcept -> TargetExpr {
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
            expect((separate != grouped));
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
            expect((builder.intern_type(query(false)) == separate));
            expect((builder.intern_type(query(true)) == grouped));
        };
});

} // namespace
