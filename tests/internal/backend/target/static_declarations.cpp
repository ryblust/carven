module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.target.static_declarations;

import :backend.target.decl;
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
