module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.backend.emission.render;

import :artifacts;
import :backend.emission.render.string;
import :backend.emit;
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
import :test.internal.backend.target.fixture;
import std;

namespace {

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

TEST_CASE("Emission: C++ string quoting owns escape syntax") {
    CHECK_EQ(cpp_string_token("a\\b\n\"c\t"), "\"a\\\\b\\012\\\"c\\011\"");
    CHECK_EQ(cpp_string_token(std::string_view("\0018\377", 3)), "\"\\0018\\377\"");
}

TEST_CASE("Emission: explicit directive groups are serialized in order") {
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

    CHECK(artifact.content.contains("#custom first\n#custom second"));
    CHECK_FALSE(artifact.content.contains("carven/runtime"));
}

TEST_CASE("Emission: verified unreachable uses the C++20 runtime leaf") {
    const auto artifact = emitted_statement(
        TargetUnreachableStmt {
            .reason = TargetUnreachableReason::SemIRProof,
        }
    );

    CHECK(artifact.content.contains("#include <carven/runtime/unreachable.hpp>"));
    CHECK(artifact.content.contains("carven::runtime::unreachable();"));
    CHECK_FALSE(artifact.content.contains("std::unreachable"));
    CHECK_FALSE(artifact.content.contains("std::abort();"));
}

TEST_CASE("Emission: runtime trap remains distinct from unreachable proof") {
    const auto artifact = emitted_statement(
        TargetRuntimeTrapStmt {
            .reason = TargetRuntimeTrapReason::SourceContract,
        }
    );

    CHECK(artifact.content.contains("#include <cstdlib>"));
    CHECK(artifact.content.contains("std::abort();"));
    CHECK_FALSE(artifact.content.contains("carven::runtime::unreachable();"));
}

TEST_CASE("Emission: value regions retain explicit result types and selective unused names") {
    for (const auto maybe_unused : {false, true}) {
        const auto artifact =
            emitted_statement([&](TargetUnitBuilder& builder) noexcept -> TargetStmtValue {
                const auto type = builder.intern_type({
                    .value =
                        TargetIntrinsicType {.symbol = TargetSymbol::Bool, .type_argument_ids = {}},
                    .const_qualified = false,
                });
                auto body = std::vector<TargetStmt>();
                body.push_back({
                    .value =
                        TargetReturnStmt {
                            .expression = TargetExpr {.value = TargetLiteralExpr {.value = true}}
                        },
                    .attribution = attribution(),
                });
                return TargetVariableStmt {
                    .binding = TargetVariableBinding::ConstValue,
                    .maybe_unused = maybe_unused,
                    .name = TargetIdentifier::from_spelling("value"),
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
                            .template_argument_type_ids = {},
                            .arguments = {}
                        }
                    },
                };
            });
        CHECK_EQ(artifact.content.contains("[[maybe_unused]]"), maybe_unused);
        CHECK(artifact.content.contains("const bool value"));
        CHECK(artifact.content.contains("[&]() noexcept -> bool"));
        CHECK(artifact.content.contains("return true;"));
    }
}

TEST_CASE("Emission: range-for preserves native binding and loop scope") {
    for (const auto binding :
         {TargetVariableBinding::ConstValue, TargetVariableBinding::ConstReference}) {
        const auto artifact = emitted_statement([&](TargetUnitBuilder& builder) noexcept {
            const auto type = builder.intern_type({
                .value = TargetIntrinsicType {.symbol = TargetSymbol::Int, .type_argument_ids = {}},
                .const_qualified = false,
            });
            return TargetRangeForStmt {
                .binding = binding,
                .maybe_unused = false,
                .name = TargetIdentifier::from_spelling("element"),
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
        CHECK(artifact.content.contains(
            binding == TargetVariableBinding::ConstValue ? "for (const int element : elements)"
                                                         : "for (const int& element : elements)"
        ));
    }
}

TEST_CASE(
    "Emission: expression type queries preserve references unless normalization is explicit"
) {
    for (const auto normalize : {false, true}) {
        const auto artifact = emitted_statement([&](TargetUnitBuilder& builder) noexcept {
            const auto member = []() static noexcept -> TargetExpr {
                return {
                    .value = TargetMemberExpr {
                        .operand = UniqueIndirect(
                            TargetExpr {
                                .value =
                                    TargetNameExpr {
                                        .name =
                                            TargetName(TargetIdentifier::from_spelling("source"))
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
                .name = TargetIdentifier::from_spelling("result"),
                .type = type,
                .initializer = member()
            };
        });
        CHECK(artifact.content.contains("decltype((source.value))"));
        CHECK(artifact.content.contains("std::remove_cvref_t<") == normalize);
        CHECK(artifact.content.contains("#include <type_traits>") == normalize);
        CHECK_FALSE(artifact.content.contains("#include <utility>"));
    }
}
