module carven:test.internal.backend.target.static_declarations;

import :backend.target.decl;
import :backend.target.dependencies;
import :backend.target.expr;
import :backend.target.header;
import :backend.target.item;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.symbol;
import :backend.target.type;
import :backend.target.unit;
import :test.harness.framework;
import :test.internal.backend.target.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Target static declarations: intrinsic names provide their own headers"_test =
        [] static noexcept {
            constexpr auto symbols = std::array {
                std::tuple(
                    TargetSymbol::StdNullopt,
                    "optional",
                    TargetHeaderGroup::StandardLibrary
                ),
                std::tuple(
                    TargetSymbol::RuntimeSliceView,
                    "carven/runtime/collection/slice.hpp",
                    TargetHeaderGroup::Runtime
                ),
            };
            for (const auto& [symbol, header, group] : symbols) {
                const auto owner = TargetTestingFixture::unit_identity();
                constexpr auto types = std::array {TargetType {
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
                expect(dependencies.front().header.path == header).note("header: ", header);
                expect(dependencies.front().group == group).note("header: ", header);
            }
        };
});

} // namespace
