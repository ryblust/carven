module carven:test.internal.backend.target.static_declarations;

import :backend.target.builder;
import :backend.target.decl;
import :backend.target.dependencies;
import :backend.target.expr;
import :backend.target.item;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.symbol;
import :backend.target.type;
import :backend.target.unit;
import :backend.target.verify;
import :test.harness.framework;
import :test.internal.backend.target.fixture;
import :test.internal.harness.death;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Target static declarations: verification traverses type and initializer"_test =
        [] static noexcept {
            const auto owner = TargetTestingFixture::unit_identity();
            const auto local = TargetTestingFixture::type_id(owner, 0);
            const auto foreign =
                TargetTestingFixture::type_id(TargetTestingFixture::unit_identity(), 0);
            const auto types = std::array {TargetType {
                .value =
                    TargetIntrinsicType {.symbol = TargetSymbol::Bool, .type_argument_ids = {}},
                .const_qualified = false,
            }};
            for (const auto bad_initializer : {false, true}) {
                auto items = std::vector<TargetItem>();
                items.push_back({
                    .value = TargetDecl {TargetVariableDecl {
                        .name = TargetIdentifier::from_spelling("data"),
                        .type = bad_initializer ? local : foreign,
                        .initializer =
                            TargetExpr {
                                .value =
                                    TargetConstructionExpr {
                                        .type = bad_initializer ? foreign : local,
                                        .initializer = {},
                                    }
                            },
                        .inline_specifier = true,
                        .constexpr_specifier = true,
                    }},
                    .attribution = TargetCompilerOwnedAttribution {
                        .reason = TargetCompilerReason::ArtifactScaffolding,
                    },
                });
                const auto sections =
                    TargetUnitSections {.preamble = {}, .body = std::move(items), .epilogue = {}};
                const auto checked = TargetTestingFixture::validate_unit(owner, types, sections);
                if (!expect(!(checked.has_value()))) {
                    return;
                }
                expect(checked.error().kind == TargetSealViolationKind::InvalidTypeReference);
            }
        };

    "Target static declarations: intrinsic names provide their own headers"_test =
        [] static noexcept {
            const auto symbols = std::array {
                std::pair(TargetSymbol::StdNullopt, "optional"),
                std::pair(TargetSymbol::RuntimeAsSlice, "carven/runtime/slice.hpp"),
                std::pair(TargetSymbol::RuntimeRange, "carven/runtime/range.hpp"),
            };
            for (const auto& [symbol, header] : symbols) {
                const auto owner = TargetTestingFixture::unit_identity();
                const auto types = std::array {TargetType {
                    .value =
                        TargetIntrinsicType {.symbol = TargetSymbol::Auto, .type_argument_ids = {}},
                    .const_qualified = false,
                }};
                auto body = std::vector<TargetItem>();
                body.push_back({
                    .value = TargetDecl {TargetVariableDecl {
                        .name = TargetIdentifier::from_spelling("empty"),
                        .type = TargetTestingFixture::type_id(owner, 0),
                        .initializer =
                            TargetExpr {.value = TargetIntrinsicNameExpr {.symbol = symbol}},
                        .inline_specifier = true,
                        .constexpr_specifier = true,
                    }},
                    .attribution = TargetCompilerOwnedAttribution {
                        .reason = TargetCompilerReason::ArtifactScaffolding
                    },
                });
                const auto sections =
                    TargetUnitSections {.preamble = {}, .body = std::move(body), .epilogue = {}};
                const auto dependencies = collect_target_dependencies(owner, types, sections);
                if (!(expect(dependencies.size() == 1uz).note("header: ", header))) {
                    return;
                }
                expect(dependencies.front().bytes == std::format("#include <{}>", header))
                    .note("header: ", header);
            }
        };

    "Target type aliases: sealing rejects foreign and out-of-range references"_test =
        [] static noexcept {
            const auto cases = std::array {
                std::pair("alias-type-foreign", true),
                std::pair("alias-type-out-of-range", false),
            };
            each(
                cases,
                [](const auto& item) static noexcept -> std::string_view { return item.first; },
                [](const auto& item) static noexcept {
                    const auto& [scenario, foreign] = item;
                    expect(expect_termination(scenario, [&] noexcept {
                        auto builder = TargetUnitBuilder();
                        static_cast<void>(builder.intern_type({
                            .value =
                                TargetIntrinsicType {
                                    .symbol = TargetSymbol::Bool,
                                    .type_argument_ids = {}
                                },
                            .const_qualified = false,
                        }));
                        const auto invalid = TargetTestingFixture::type_id(
                            foreign ? TargetTestingFixture::unit_identity() : builder.identity(),
                            foreign ? 0u : 99u
                        );
                        auto body = std::vector<TargetItem>();
                        body.push_back(target_lowering_item(
                            TargetDecl {TargetTypeAlias {
                                .name = TargetIdentifier::from_spelling("Invalid"),
                                .type = invalid,
                            }}
                        ));
                        static_cast<void>(std::move(builder).finish({
                            .preamble = {},
                            .body = std::move(body),
                            .epilogue = {},
                        }));
                    }));
                }
            );
        };
});

} // namespace
