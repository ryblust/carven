module carven:test.internal.backend.generation.content;

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
});

} // namespace
