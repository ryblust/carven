module carven:test.internal.semantic.semir.text;

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
import :semantic.semir.structured;
import :semantic.semir.type;
import :semantic.visibility;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.text;
import :test.harness.framework;
import :test.internal.harness.death;
import :test.internal.semantic.format.fixture;
import :test.internal.semantic.semir.fixture;
import std;

using namespace semir_test;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "SemIR publication: String operations validate arity types access and results",
        [] static noexcept {
            struct Scenario final {
                std::string_view name;
                TextIntrinsic intrinsic;
                BuiltinType result;
                std::optional<AccessMode> operand;
                bool valid;
            };

            const auto scenarios = std::array {
                Scenario {
                    .name = "borrowed view",
                    .intrinsic = TextIntrinsic::AsStr,
                    .result = BuiltinType::Str,
                    .operand = AccessMode::Read,
                    .valid = true,
                },
                Scenario {
                    .name = "missing operand",
                    .intrinsic = TextIntrinsic::AsStr,
                    .result = BuiltinType::Str,
                    .operand = std::nullopt,
                    .valid = false,
                },
                Scenario {
                    .name = "wrong result",
                    .intrinsic = TextIntrinsic::AsStr,
                    .result = BuiltinType::String,
                    .operand = AccessMode::Read,
                    .valid = false,
                },
                Scenario {
                    .name = "wrong input type",
                    .intrinsic = TextIntrinsic::FromStr,
                    .result = BuiltinType::String,
                    .operand = AccessMode::Read,
                    .valid = false,
                },
                Scenario {
                    .name = "value Write receiver",
                    .intrinsic = TextIntrinsic::Clear,
                    .result = BuiltinType::Void,
                    .operand = AccessMode::Write,
                    .valid = false,
                },
                Scenario {
                    .name = "wrong receiver access",
                    .intrinsic = TextIntrinsic::Clear,
                    .result = BuiltinType::Void,
                    .operand = AccessMode::Read,
                    .valid = false,
                },
            };
            ct::each(scenarios, &Scenario::name, [&](const auto& scenario) noexcept {
                auto sources = SourceManager();
                auto diagnostics = DiagnosticSink();
                auto builder = begin_compilation(sources, diagnostics, "semir.publication.text");
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
                builder.finish_declaration_heads();
                auto reservation = builder.reserve_body(BodyKind::Test);
                builder.define_test(
                    test,
                    TestDeclaration {
                        .is_const = false,
                        .module_id = module_id,
                        .source =
                            {.label = builder.intern_spelling("text"), .origin = facts.origin},
                        .body = reservation.id(),
                    }
                );
                auto body = BodyBuilder(std::move(reservation), builder);
                const auto lifetime = body.add_lifetime_region(
                    std::nullopt,
                    LifetimeRegionKind::Lexical,
                    facts.origin
                );
                auto operands = std::vector<SemCallArgument>();
                if (scenario.operand) {
                    operands.push_back({
                        .access = *scenario.operand,
                        .expression =
                            body.make_expression(owning, lifetime, facts.origin, SemDefault {}),
                    });
                }
                auto statements = std::vector<SemanticStatement>();
                statements.push_back({
                    .origin = facts.origin,
                    .lifetime = lifetime,
                    .reachable = true,
                    .value = SemExpressionStatement {
                        .expression = body.make_expression(
                            result_type,
                            lifetime,
                            facts.origin,
                            SemIntrinsic {
                                .operation = scenario.intrinsic,
                                .operands = std::move(operands),
                            }
                        )
                    },
                });
                const auto publish_text = [&]() noexcept {
                    publish(
                        std::move(body).finish(
                            SemanticRegion {
                                .lifetime = lifetime,
                                .origin = facts.origin,
                                .statements = std::move(statements),
                                .result = std::nullopt,
                                .result_reachable = false,
                                .failures = BodyFailures(builder.add_empty_failure_term()),
                                .exits_test = false,
                            }
                        ),
                        builder
                    );
                    return std::move(builder).finish();
                };
                if (scenario.valid) {
                    ct::expect(publish_text().has_value());
                } else {
                    ct::expect(expect_termination(scenario.name, publish_text));
                }
            });
        }
    );

    ct::test(
        "SemIR text contracts: type constraints distinguish text bytes and characters",
        [] static noexcept {
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto draft = begin_compilation(sources, diagnostics, "semir.text.contracts");
            const auto lookup = [&](TypeID type) noexcept {
                return draft.type_copy(type);
            };
            const auto str = draft.builtin_type(BuiltinType::Str);
            const auto string = draft.builtin_type(BuiltinType::String);
            const auto character = draft.builtin_type(BuiltinType::Char);
            const auto integer = draft.builtin_type(BuiltinType::I32);
            const auto byte = draft.builtin_type(BuiltinType::U8);
            const auto bytes = draft.intern_type({.value = SliceTypeValue {.element = byte}});
            const auto integers = draft.intern_type({.value = SliceTypeValue {.element = integer}});
            const auto query = text_intrinsic_contract(TextIntrinsic::Bytes);
            if (!ct::expect(query.parameters.size() == 1)) {
                return;
            }
            ct::expect(matches_text_intrinsic_type(query.parameters[0].type, str, lookup));
            ct::expect(matches_text_intrinsic_type(query.parameters[0].type, string, lookup));
            ct::expect(!(matches_text_intrinsic_type(query.parameters[0].type, bytes, lookup)));
            ct::expect(matches_text_intrinsic_type(query.result, bytes, lookup));
            ct::expect(!(matches_text_intrinsic_type(query.result, integers, lookup)));
            ct::expect(resolve_text_intrinsic_type(draft, query.result) == bytes);
            const auto utf8 = text_intrinsic_contract(TextIntrinsic::FromUTF8Unchecked);
            if (!ct::expect(utf8.parameters.size() == 1)) {
                return;
            }
            ct::expect(matches_text_intrinsic_type(utf8.parameters[0].type, bytes, lookup));
            ct::expect(!(matches_text_intrinsic_type(utf8.parameters[0].type, str, lookup)));
            const auto append = text_intrinsic_contract(TextIntrinsic::Append);
            const auto push = text_intrinsic_contract(TextIntrinsic::Push);
            if (!ct::expect(append.parameters.size() == 2)) {
                return;
            }
            if (!ct::expect(push.parameters.size() == 2)) {
                return;
            }
            ct::expect(matches_text_intrinsic_type(append.parameters[1].type, str, lookup));
            ct::expect(
                !(matches_text_intrinsic_type(append.parameters[1].type, character, lookup))
            );
            ct::expect(matches_text_intrinsic_type(push.parameters[1].type, character, lookup));
            ct::expect(!(matches_text_intrinsic_type(push.parameters[1].type, str, lookup)));
        }
    );
});

} // namespace
