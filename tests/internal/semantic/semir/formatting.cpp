module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.semir.formatting;

import :compiler.request;
import :diagnostics.sink;
import :frontend.program.parse;
import :semantic.analysis.body.builder;
import :semantic.analysis.program;
import :semantic.format;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.internal.harness.death;
import :test.internal.semantic.format.fixture;
import :test.internal.semantic.semir.fixture;
import std;

using namespace semir_test;

TEST_CASE("SemIR publication: source format result types and Read operands are required") {
    struct Scenario final {
        std::string_view name;
        BuiltinType result;
        AccessMode operand;
        bool valid;
        std::optional<std::size_t> native_operand = std::nullopt;
    };

    const auto scenarios = std::array {
        Scenario {"source formatting", BuiltinType::String, AccessMode::Read, true},
        Scenario {"borrowed result", BuiltinType::Str, AccessMode::Read, false},
        Scenario {"Write operand", BuiltinType::String, AccessMode::Write, false},
        Scenario {"Take operand", BuiltinType::String, AccessMode::Take, false},
        Scenario {"native operand", BuiltinType::String, AccessMode::Read, true, 1uz},
    };
    for (const auto& scenario : scenarios) {
        CAPTURE(scenario.name);
        const auto publish_format = [&]() noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto builder = begin_compilation(sources, diagnostics, "semir.publication.format");
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
            const auto owning = builder.builtin_type(BuiltinType::String);
            const auto result_type = builder.builtin_type(scenario.result);
            const auto native_type = builder.intern_type({
                .value = CppTypeValue {.form = CppConstCharPointerType {}},
            });
            const auto boolean_type = builder.builtin_type(BuiltinType::Bool);
            const auto boolean_constant = builder.intern_constant({
                .type = boolean_type,
                .value = BooleanConstant {.value = true},
            });
            builder.finish_declaration_heads();
            auto reservation = builder.reserve_body(BodyKind::Test);
            builder.define_test(
                test,
                TestDeclaration {
                    .is_const = false,
                    .module_id = module_id,
                    .name = builder.intern_spelling("text"),
                    .origin = facts.origin,
                    .body = reservation.id(),
                }
            );
            auto body = BodyBuilder(std::move(reservation), builder);
            const auto lifetime =
                body.add_lifetime_region(std::nullopt, LifetimeRegionKind::Lexical, facts.origin);
            auto operands = std::vector<SemCallArgument>();
            for (auto index = 0uz; index < 3uz; ++index) {
                operands.push_back({
                    .access = scenario.operand,
                    .expression = scenario.native_operand == index
                        ? body.make_expression(
                              native_type,
                              lifetime,
                              facts.origin,
                              SemCpp {
                                  .operation = CppCStringOperation {.bytes = "native"},
                                  .operands = {},
                              }
                          )
                        : index == 1uz
                        ? body.make_expression(
                              boolean_type,
                              lifetime,
                              facts.origin,
                              SemConstant {.constant = boolean_constant}
                          )
                        : body.make_expression(
                              owning,
                              lifetime,
                              facts.origin,
                              SemTextIntrinsic {.intrinsic = TextIntrinsic::New, .operands = {}}
                          ),
                });
            }
            auto statements = std::vector<SemanticStatement>();
            statements.push_back({
                .origin = facts.origin,
                .lifetime = lifetime,
                .value = SemExpressionStatement {
                    .expression = body.make_expression(
                        result_type,
                        lifetime,
                        facts.origin,
                        SemFormat {
                            .specification =
                                FormatSpec {
                                    .parts =
                                        {format_field(0uz), format_field(1uz), format_field(2uz)}
                                },
                            .operands = std::move(operands),
                            .receiver = std::nullopt,
                        }
                    )
                },
            });
            publish(
                std::move(body).finish(
                    SemanticRegion {
                        .lifetime = lifetime,
                        .origin = facts.origin,
                        .statements = std::move(statements),
                        .result = std::nullopt,
                        .failures = BodyFailures(builder.add_empty_failure_term()),
                        .exits_test = false,
                    }
                ),
                builder
            );
            const auto result = std::move(builder).finish();
            REQUIRE(result.has_value());
        };
        if (scenario.valid) {
            publish_format();
        } else {
            CHECK(expect_termination(scenario.name, publish_format));
        }
    }
}
