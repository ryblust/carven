module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

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
import :test.internal.backend.target.fixture;
import :test.internal.harness.death;
import std;

TEST_CASE("Target static declarations: verification traverses type and initializer") {
    const auto owner = TargetTestingFixture::unit_identity();
    const auto local = TargetTestingFixture::type_id(owner, 0);
    const auto foreign = TargetTestingFixture::type_id(TargetTestingFixture::unit_identity(), 0);
    const auto types = std::array {TargetType {
        .value = TargetIntrinsicType {.symbol = TargetSymbol::Bool, .type_argument_ids = {}},
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
        REQUIRE_FALSE(checked.has_value());
        CHECK(checked.error().kind == TargetSealViolationKind::InvalidTypeReference);
    }
}

TEST_CASE("Target static declarations: intrinsic names provide their own headers") {
    const auto symbols = std::array {
        std::pair(TargetSymbol::StdNullopt, "optional"),
        std::pair(TargetSymbol::RuntimeAsSlice, "carven/runtime/slice.hpp"),
        std::pair(TargetSymbol::RuntimeRange, "carven/runtime/range.hpp"),
    };
    for (const auto& [symbol, header] : symbols) {
        CAPTURE(header);
        const auto owner = TargetTestingFixture::unit_identity();
        const auto types = std::array {TargetType {
            .value = TargetIntrinsicType {.symbol = TargetSymbol::Auto, .type_argument_ids = {}},
            .const_qualified = false,
        }};
        auto body = std::vector<TargetItem>();
        body.push_back({
            .value = TargetDecl {TargetVariableDecl {
                .name = TargetIdentifier::from_spelling("empty"),
                .type = TargetTestingFixture::type_id(owner, 0),
                .initializer = TargetExpr {.value = TargetIntrinsicNameExpr {.symbol = symbol}},
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
        REQUIRE(dependencies.size() == 1uz);
        CHECK(dependencies.front().bytes == std::format("#include <{}>", header));
    }
}

TEST_CASE("Target type naming: placement rejects foreign and out-of-range references") {
    const auto cases = std::array {
        std::pair("namespace-type-foreign", true),
        std::pair("namespace-type-out-of-range", false),
    };
    for (const auto& [scenario, foreign] : cases) {
        CHECK(expect_termination(scenario, [&] noexcept {
            auto builder = TargetUnitBuilder();
            const auto type = builder.intern_type({
                .value =
                    TargetIntrinsicType {.symbol = TargetSymbol::Bool, .type_argument_ids = {}},
                .const_qualified = false,
            });
            builder.name_namespace_type(type, TargetIdentifier::from_spelling("Named"));
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
}

TEST_CASE("Target type naming: reopened qualified namespaces share occupied names") {
    const auto identifier = [](std::string_view name) static noexcept {
        return TargetIdentifier::from_spelling(name);
    };
    auto builder = TargetUnitBuilder();
    const auto type = builder.intern_type({
        .value = TargetIntrinsicType {.symbol = TargetSymbol::Bool, .type_argument_ids = {}},
        .const_qualified = false,
    });
    builder.name_namespace_type(type, identifier("Query"));
    auto first = std::vector<TargetItem>();
    first.push_back(target_lowering_item(
        TargetDecl {TargetTypeAlias {
            .name = identifier("Use"),
            .type = type,
        }}
    ));
    auto second = std::vector<TargetItem>();
    second.push_back(target_lowering_item(
        TargetDecl {TargetStructForwardDecl {
            .name = identifier("Query"),
        }}
    ));
    auto nested = std::vector<TargetItem>();
    nested.push_back(target_lowering_item(
        TargetNamespace {
            .name = TargetName(identifier("inner")),
            .items = std::move(second),
            .closing_comment = false,
        }
    ));
    auto body = std::vector<TargetItem>();
    body.push_back(target_lowering_item(
        TargetNamespace {
            .name = TargetName::globally_qualified({identifier("outer"), identifier("inner")}),
            .items = std::move(first),
            .closing_comment = false,
        }
    ));
    body.push_back(target_lowering_item(
        TargetNamespace {
            .name = TargetName(identifier("outer")),
            .items = std::move(nested),
            .closing_comment = false,
        }
    ));
    const auto unit =
        std::move(builder).finish({.preamble = {}, .body = std::move(body), .epilogue = {}});
    const auto* named = std::get_if<TargetNamedType>(&unit.type(type).value);
    REQUIRE(named != nullptr);
    CHECK(named->name.is_globally_qualified());
    const auto parts = named->name.components();
    REQUIRE(parts.size() == 3uz);
    CHECK(parts[0].spelling() == "outer");
    CHECK(parts[1].spelling() == "inner");
    CHECK(parts[2].spelling() != "Query");
}
