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
import :semantic.semir.generic;
import :semantic.semir.operation;
import :semantic.semir.program;
import :semantic.semir.type;
import :source.manager;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import :test.internal.semantic.semir.fixture;
import std;

namespace {

const TestSuite suite([] static noexcept {
    "Generic content: normalized arguments determine distinct stable target names"_test =
        [] static noexcept {
            const auto build = [](bool reverse) noexcept {
                const auto parameters =
                    reverse ? "a: Box<u32>, b: Box<i32>" : "a: Box<i32>, b: Box<u32>";
                return PlannedCompilation::build(
                    analyze_test_program(
                        std::format("struct Box<T> {{ value: T }}\nfn hold({}) {{}}\n", parameters)
                    ),
                    {.test_mode = TestGenerationMode::None,
                     .linkage_domain = *LinkageDomain::explicit_value("generic_content")}
                );
            };
            const auto first = build(false);
            const auto second = build(true);
            const auto names = [](const PlannedCompilation& compilation) noexcept {
                auto result = std::map<std::string, std::string>();
                const auto& semantic = compilation.semantic();
                for (const auto& instance : semantic.generic_nominal_instances()) {
                    const auto argument = instance.arguments.front();
                    result.emplace(
                        type_content_key(semantic, argument),
                        std::string(
                            compilation.target()
                                .names()
                                .structure_identifier(std::get<StructID>(instance.declaration))
                                .spelling()
                        )
                    );
                }
                return result;
            };
            const auto first_names = names(first);
            expect_equal(first_names.size(), 2uz);
            expect(first_names == names(second));
            expect(
                std::ranges::none_of(first.target().artifacts(), [](const auto artifact) noexcept {
                    return std::holds_alternative<TargetCppAPIHeaderArtifact>(artifact.value);
                })
            );
            if (first_names.size() == 2uz) {
                expect(first_names.begin()->second != std::next(first_names.begin())->second);
            }
        };

    "Failure content: generic arguments determine stable transport order"_test =
        [] static noexcept {
            const auto build = [](bool reverse) noexcept {
                return PlannedCompilation::build(
                    analyze_test_program(
                        std::format(
                            "struct Error<T> {{ value: T }} struct Box<T> {{ value: T }} "
                            "enum Failure<T> {{ Item(T) }} "
                            "fn fail() throw {} {{}}",
                            reverse
                                ? "Failure<Box<u32>> + Failure<Box<i32>> + Error<u32> + Error<i32>"
                                : "Error<i32> + Error<u32> + Failure<Box<i32>> + Failure<Box<u32>>"
                        )
                    ),
                    {.test_mode = TestGenerationMode::None,
                     .linkage_domain = *LinkageDomain::explicit_value("failure_content")}
                );
            };
            const auto keys = [](const PlannedCompilation& compilation) noexcept {
                auto result = std::vector<std::string>();
                for (const auto entry : compilation.semantic().failure_sets().entries()) {
                    if (entry.value.members.size() != 4uz) {
                        continue;
                    }
                    for (const auto type : compilation.target().failure_abi().members(entry.id)) {
                        result.push_back(type_content_key(compilation.semantic(), type));
                    }
                }
                return result;
            };
            const auto first = build(false);
            const auto second = build(true);
            const auto identities = keys(first);
            expect_equal(identities.size(), 4uz);
            expect(std::ranges::is_sorted(identities));
            expect(std::ranges::adjacent_find(identities) == identities.end());
            expect(identities == keys(second));
        };

    "Content identity: deep canonical queries have bounded traversal and encoding"_test =
        [] static noexcept {
            constexpr auto depth = 20'000uz;
            auto sources = SourceManager();
            auto diagnostics = DiagnosticSink();
            auto builder =
                begin_semir_test_compilation(sources, diagnostics, "content.query_chain");
            const auto module_origin = make_semir_test_module_origin(builder);
            const auto module = builder.reserve_module_declaration();
            builder.define_declaration(
                module,
                ModuleDeclaration {
                    .provenance_module = module_origin.provenance_module,
                    .origin = module_origin.origin,
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
            if (!expect(program.has_value())) {
                return;
            }
            const auto key = type_content_key(*program, type);
            expect_less(depth, key.size());
            expect_less(key.size(), 128uz * (depth + 1uz));
            expect(diagnostics.empty());
        };

    "Generation: shared native query types have bounded expanded target syntax"_test =
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
            expect_less(largest_key, 256uz * (depth + 1uz));

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
                expect(traverse_target_unit(unit.sections(), query));
                expanded_types += query.expanded_types;
            }
            expect_less(expanded_types, 128uz * (depth + 1uz));
        };
});

} // namespace
