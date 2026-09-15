module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.target.static_declarations;

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

TEST_CASE("Target static declarations: nullopt collects optional without a typed optional") {
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
            .initializer =
                TargetExpr {.value = TargetIntrinsicNameExpr {.symbol = TargetSymbol::StdNullopt}},
            .inline_specifier = true,
            .constexpr_specifier = true,
        }},
        .attribution =
            TargetCompilerOwnedAttribution {.reason = TargetCompilerReason::ArtifactScaffolding},
    });
    const auto sections =
        TargetUnitSections {.preamble = {}, .body = std::move(body), .epilogue = {}};
    const auto dependencies = collect_target_dependencies(owner, types, sections);
    REQUIRE(dependencies.size() == 1uz);
    CHECK(dependencies.front().bytes == "#include <optional>");
}
