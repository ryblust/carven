module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.semir.slices;

import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.resolve;
import :semantic.analysis.ownership;
import :semantic.analysis.program;
import :semantic.format;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.contents;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.type;
import :semantic.visibility;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.internal.harness.death;
import :test.internal.semantic.format.fixture;
import :test.internal.semantic.semir.fixture;
import std;

using namespace semir_test;

TEST_CASE("SemIR publication: slice extents describe sequence results and match array types") {
    struct Scenario final {
        std::string_view name;
        std::optional<std::uint64_t> extent;
        bool query;
        bool valid;
    };

    const auto scenarios = std::to_array<Scenario>({
        {"slice zero array extent", 0u, false, true},
        {"slice incorrect array extent", 1u, false, false},
        {"slice missing array extent", std::nullopt, false, false},
        {"slice query no sequence extent", std::nullopt, true, true},
        {"slice query sequence extent", 0u, true, false},
    });
    for (const auto& scenario : scenarios) {
        CAPTURE(scenario.name);
        const auto publish_slice = [&]() noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto builder = begin_compilation(sources, diagnostics, "semir.publication.slice");
            const auto facts = module_facts(builder);
            const auto module_id = builder.reserve_module_declaration();
            const auto test = builder.reserve_test();
            builder.define_declaration(
                module_id,
                ModuleDeclaration {
                    .provenance_module = facts.provenance_module,
                    .origin = facts.origin,
                    .cpp_headers = {},
                    .cpp_source_fragments = {},
                    .items = {test},
                }
            );
            const auto element_type = builder.builtin_type(BuiltinType::I32);
            const auto array_type = builder.intern_type({
                .value = ArrayTypeValue {.element = element_type, .extent = 0u},
            });
            const auto slice_type = builder.intern_type({
                .value = SliceTypeValue {.element = element_type},
            });
            const auto size_type = builder.builtin_type(BuiltinType::Usize);
            builder.finish_declaration_heads();
            auto reservation = builder.reserve_body(BodyKind::Test);
            builder.define_test(
                test,
                TestDeclaration {
                    .is_const = false,
                    .module_id = module_id,
                    .name = builder.intern_spelling("slice"),
                    .origin = facts.origin,
                    .body = reservation.id(),
                }
            );
            auto body = BodyBuilder(std::move(reservation), builder);
            const auto lifetime =
                body.add_lifetime_region(std::nullopt, LifetimeRegionKind::Lexical, facts.origin);
            auto operands = std::vector<SemCallArgument>();
            operands.push_back({
                .access = AccessMode::Read,
                .expression = body.make_expression(
                    array_type,
                    lifetime,
                    facts.origin,
                    SemArray {.elements = {}}
                ),
            });
            auto expression = body.make_expression(
                slice_type,
                lifetime,
                facts.origin,
                SemSliceIntrinsic {
                    .intrinsic = SliceIntrinsic::FromArray,
                    .operands = std::move(operands),
                    .result_extent =
                        scenario.query ? std::optional<std::uint64_t>(0u) : scenario.extent,
                }
            );
            if (scenario.query) {
                auto query_operands = std::vector<SemCallArgument>();
                query_operands.push_back(
                    {.access = AccessMode::Read, .expression = std::move(expression)}
                );
                expression = body.make_expression(
                    size_type,
                    lifetime,
                    facts.origin,
                    SemSliceIntrinsic {
                        .intrinsic = SliceIntrinsic::Len,
                        .operands = std::move(query_operands),
                        .result_extent = scenario.extent,
                    }
                );
            }
            auto statements = std::vector<SemanticStatement>();
            statements.push_back({
                .origin = facts.origin,
                .lifetime = lifetime,
                .value = SemExpressionStatement {.expression = std::move(expression)},
            });
            publish(
                std::move(body).finish({
                    .lifetime = lifetime,
                    .origin = facts.origin,
                    .statements = std::move(statements),
                    .result = std::nullopt,
                    .failures = BodyFailures(builder.add_empty_failure_term()),
                    .exits_test = false,
                }),
                builder
            );
            const auto result = std::move(builder).finish();
            REQUIRE(result.has_value());
        };
        if (scenario.valid) {
            publish_slice();
        } else {
            CHECK(expect_termination(scenario.name, publish_slice));
        }
    }
}
