module carven:test.internal.backend.generation.content;

import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :backend.target.traversal;
import :backend.target.type;
import :diagnostics.sink;
import :semantic.analysis.program;
import :semantic.semir.content;
import :semantic.semir.decl;
import :semantic.semir.delegation;
import :semantic.semir.operation;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.manager;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import :test.internal.semantic.semir.fixture;
import std;

namespace {

namespace ct = carven::testing;

const ct::Suite tests([] static noexcept {
    ct::test(
        "Content identity: deep canonical queries have bounded traversal and encoding",
        [] static noexcept {
            constexpr auto depth = 20'000uz;
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto builder =
                semir_test::begin_compilation(sources, diagnostics, "content.query_chain");
            const auto facts = semir_test::module_facts(builder);
            const auto module = builder.reserve_module_declaration();
            builder.define_declaration(
                module,
                ModuleDeclaration {
                    .provenance_module = facts.provenance_module,
                    .origin = facts.origin,
                    .cpp_headers = {},
                    .cpp_source_fragments = {},
                    .items = {},
                }
            );
            auto type = builder.builtin_type(BuiltinType::I32);
            for (auto index = 0uz; index < depth; ++index) {
                type = builder.intern_type(
                    CanonicalType {
                        .value = CppTypeValue {
                            .form = CppQueryType {
                                .expression = CppUnaryQuery {
                                    .operation = UnaryOperator::Negate,
                                    .operand = {.type = type, .access = AccessMode::Read},
                                },
                            },
                        },
                    }
                );
            }
            builder.finish_declaration_heads();
            const auto program = std::move(builder).finish();
            if (!ct::expect(program.has_value())) {
                return;
            }
            const auto key = type_content_key(*program, type);
            ct::expect_less(depth, key.size());
            ct::expect_less(key.size(), 128uz * (depth + 1uz));
            ct::expect(diagnostics.empty());
        }
    );

    ct::test(
        "Generation: shared native query types have bounded expanded target syntax",
        [] static noexcept {
            constexpr auto depth = 12uz;
            auto source = std::string(
                "import <probe.hpp> using probe::{seed};\nfn grow() { let x0 = seed();\n"
            );
            for (auto index = 1uz; index <= depth; ++index) {
                source += std::format("let x{} = x{} + x{};\n", index, index - 1, index - 1);
            }
            source += std::format("return x{}; }}\n", depth);
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(std::move(source)),
                {
                    .test_mode = TestGenerationMode::None,
                    .linkage_domain = *LinkageDomain::explicit_value("query_graph"),
                }
            );

            auto largest_key = 0uz;
            for (const auto type : compilation.semantic().types().entries()) {
                largest_key =
                    std::max(largest_key, type_content_key(compilation.semantic(), type.id).size());
            }
            ct::expect_less(largest_key, 256uz * (depth + 1uz));

            struct Query final {
                const TargetUnit& unit;
                std::size_t expanded_types;

                auto visit_type(TargetTypeID id) noexcept -> bool {
                    ++expanded_types;
                    return visit_target_type_children(unit.type(id).value, *this);
                }
            };

            auto expanded_types = 0uz;
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                auto query = Query {.unit = unit, .expanded_types = 0uz};
                ct::expect(traverse_target_unit(unit.sections(), query));
                expanded_types += query.expanded_types;
            }
            ct::expect_less(expanded_types, 128uz * (depth + 1uz));
        }
    );
});

} // namespace
