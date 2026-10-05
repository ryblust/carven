module carven:test.internal.backend.generation.staged_calls;

import :artifacts;
import :backend.generate;
import :backend.generation.linkage;
import :backend.generation.plan;
import :backend.generation.request;
import :backend.lower;
import :backend.target;
import :backend.target.decl;
import :backend.target.expr;
import :backend.target.stmt;
import :backend.target.traversal;
import :backend.target.type;
import :compiler.compile;
import :frontend.program.parse;
import :semantic.analyze;
import :semantic.semir.decl;
import :semantic.semir.program;
import :source.batch;
import :source.manager;
import :source.module_path;
import :source.provenance;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

auto instance_names(const PlannedCompilation& compilation, std::string_view source_name) noexcept
    -> std::flat_set<std::string> {
    auto names = std::flat_set<std::string>();
    for (const auto& instance : compilation.semantic().static_instances()) {
        const auto& function = compilation.semantic().declarations().function(instance.function);
        if (compilation.semantic().provenance().spelling(function.name) == source_name) {
            names.emplace(
                compilation.target().names().callable_identifier(instance.callable).spelling()
            );
        }
    }
    return names;
}

constexpr auto provider_source = std::string_view(
    "private fn scale(value: i32) -> i32 => value * 2;\n"
    "private fn unused() -> i32 => 1;\n"
    "export fn add(value: i32, const control: i32) -> i32 {\n"
    "    let offset = [](input: i32) => input + 1;\n"
    "    return offset(scale(value)) + control;\n"
    "}\n"
    "export fn direct() -> i32 => unused();\n"
);

auto analyze_modules(
    std::string_view provider_text,
    std::string_view calls,
    bool unrelated = false
) noexcept -> SemIRProgram {
    auto sources = SourceManager();
    const auto provider = sources.append_virtual("provider.cv", std::string(provider_text));
    const auto consumer = sources.append_virtual(
        "consumer.cv",
        std::format("import provider using add;\nexport fn use(value: i32) -> i32 => {};\n", calls)
    );
    require(provider.has_value());
    require(consumer.has_value());
    const auto provider_path = CanonicalModulePath::from_value("provider");
    const auto consumer_path = CanonicalModulePath::from_value("consumer");
    require(provider_path.has_value());
    require(consumer_path.has_value());
    auto inputs = std::vector<SourceModuleInput>();
    if (unrelated) {
        const auto source = sources.append_virtual(
            "earlier.cv",
            "struct Earlier { value: i32 } const values: [Earlier] = [{ value: 1 }]; "
            "export fn earlier() -> i32 => ([](x: i32) => x + 1)(values[0].value);"
        );
        const auto path = CanonicalModulePath::from_value("earlier");
        require(source.has_value());
        require(path.has_value());
        inputs.push_back({.source_id = *source, .module_path = *path});
    }
    inputs.push_back({.source_id = *provider, .module_path = *provider_path});
    inputs.push_back({.source_id = *consumer, .module_path = *consumer_path});
    auto parsed = parse_program(sources, SourceBatch {.modules = inputs});
    require(parsed.has_value());
    auto analyzed = analyze(std::move(*parsed));
    require(analyzed.has_value());
    return std::move(analyzed->value);
}

// Function names one artifact declares, defines and calls.
struct UnitNames final {
    const std::flat_set<std::string>& instances;
    std::flat_set<std::string> declarations;
    std::flat_set<std::string> definitions;
    std::flat_set<std::string> calls;
    std::size_t inline_instances;

    auto enter_declaration(const TargetDecl& declaration) noexcept -> bool {
        const auto* function = std::get_if<TargetFunctionDecl>(&declaration);
        if (function == nullptr) {
            return true;
        }
        const auto spelling = std::string(function->name.components().back().spelling());
        auto& destination = std::holds_alternative<TargetFreeFunctionDeclaration>(function->form)
            ? declarations
            : definitions;
        destination.insert(spelling);
        if (instances.contains(spelling)) {
            expect_equal(function->parameters.size(), 1uz);
            expect(!function->static_specifier);
            inline_instances += function->inline_specifier;
        }
        return true;
    }

    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool {
        const auto* call = std::get_if<TargetCallExpr>(&expression.value);
        const auto* name = call == nullptr
            ? nullptr
            : std::get_if<TargetNameExpr>(&template_primary_expression(*call->callee).value);
        if (name != nullptr) {
            calls.emplace(std::string(name->name.components().back().spelling()));
        }
        return true;
    }
};

struct CallFacts final {
    const std::flat_set<std::string>& instances;
    std::size_t staged_calls;
    std::size_t compile_only_calls;

    auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept -> bool {
        const auto* call = std::get_if<TargetCallExpr>(&expression.value);
        if (call == nullptr) {
            return true;
        }
        const auto* name =
            std::get_if<TargetNameExpr>(&template_primary_expression(*call->callee).value);
        if (name == nullptr) {
            return true;
        }
        const auto spelling = name->name.components().back().spelling();
        if (instances.contains(std::string(spelling))) {
            ++staged_calls;
            expect_equal(call->arguments.size(), 1uz);
        }
        compile_only_calls += spelling == "index_value";
        return true;
    }
};

struct StagedDefinitions final {
    const std::flat_set<std::string>& instances;
    std::size_t count;
    std::flat_set<std::uint64_t> controls;

    auto enter_declaration(const TargetDecl& declaration) noexcept -> bool {
        const auto* function = std::get_if<TargetFunctionDecl>(&declaration);
        if (function == nullptr
            || !instances.contains(std::string(function->name.components().back().spelling()))) {
            return true;
        }
        const auto* definition = std::get_if<TargetFreeFunctionDefinition>(&function->form);
        if (definition == nullptr) {
            return true;
        }
        ++count;
        expect_equal(function->parameters.size(), 1uz);

        struct Literals final {
            std::flat_set<std::uint64_t>& values;

            auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                -> bool {
                const auto* literal = std::get_if<TargetLiteralExpr>(&expression.value);
                if (literal != nullptr) {
                    if (const auto* integer = std::get_if<TargetIntegerLiteral>(&literal->value)) {
                        values.insert(integer->magnitude);
                    }
                }
                return true;
            }
        } literals {.values = controls};

        require(traverse_target_statements(definition->body, literals));
        return true;
    }
};

const TestSuite suite([] static noexcept {
    "Generation: callers realize cross-module instances in their own artifact"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_modules(provider_source, "add(value, 3) + add(value, 4)"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("cross_module_stages")}
            );
            const auto names_for_add = instance_names(compilation, "add");
            expect_equal(names_for_add.size(), 2uz);
            auto interface = UnitNames {
                .instances = names_for_add,
                .declarations = {},
                .definitions = {},
                .calls = {},
                .inline_instances = 0uz,
            };
            auto provider = UnitNames {
                .instances = names_for_add,
                .declarations = {},
                .definitions = {},
                .calls = {},
                .inline_instances = 0uz,
            };
            auto consumer = UnitNames {
                .instances = names_for_add,
                .declarations = {},
                .definitions = {},
                .calls = {},
                .inline_instances = 0uz,
            };
            for (const auto artifact : compilation.target().artifacts()) {
                const auto path = artifact_logical_path(artifact.value);
                auto* facts = path == "carven/generated/provider.hpp" ? &interface
                    : path == "provider.cpp"                          ? &provider
                    : path == "consumer.cpp"                          ? &consumer
                                                                      : nullptr;
                if (facts != nullptr) {
                    const auto unit = lower_artifact(compilation, artifact.id);
                    require(traverse_target_unit(unit.sections(), *facts));
                }
            }
            // The staged body reaches scale, so the provider publishes it; unused stays private.
            expect(interface.declarations.contains("scale"));
            expect(!interface.declarations.contains("unused"));
            expect(provider.definitions.contains("scale"));
            for (const auto& name : names_for_add) {
                expect(!interface.declarations.contains(name));
                expect(!provider.declarations.contains(name));
                expect(!provider.definitions.contains(name));
                expect(!consumer.declarations.contains(name));
                expect(consumer.definitions.contains(name));
                expect(consumer.calls.contains(name));
            }
            expect_equal(consumer.inline_instances, 2uz);
        };

    "Generation: provider artifacts do not depend on their callers"_test = [] static noexcept {
        const auto generate = [](std::string_view calls, bool unrelated = false) static noexcept {
            const auto source = std::string("import <vector> using std::vector;\n")
                + std::string(provider_source) + R"(
                private struct Record { value: i32 }
                export fn support(value: i32) -> i32 {
                    const values: [i32] = [2, 3];
                    let native = vector<i32> { value };
                    println(Record { value: values[0] });
                    return (native[0] as i32) + values[1];
                }
            )";
            return generate_artifacts(
                analyze_modules(source, calls, unrelated),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("provider_stability")}
            );
        };
        const auto content = [](const GeneratedArtifactSet& artifacts,
                                std::string_view path) static noexcept -> std::string {
            const auto entries = artifacts.entries();
            const auto found = std::ranges::find(entries, path, &GeneratedArtifact::logical_path);
            require(found != entries.end());
            return found->content;
        };
        const auto first = generate("add(value, 3) + add(value, 4)");
        // Caller instances and unrelated types leave provider support and closures stable.
        const auto second = generate("([](x: i32) => x + 1)(add(value, 5))", true);
        for (const auto path : {"provider.cpp", "carven/generated/provider.hpp"}) {
            expect_equal(content(first, path), content(second, path));
        }
        expect(content(first, "consumer.cpp") != content(second, "consumer.cpp"));
    };

    "Generation: shared bodies retain identical module references"_test = [] static noexcept {
        const auto compilation = PlannedCompilation::build(
            analyze_modules(
                R"(
                import <vector> using std::vector;
                private struct Record { value: i32 }
                export fn add(value: i32, const offset: i32) -> i32 {
                    const values: [i32] = [2, 3];
                    let native = vector<i32> { value };
                    println(Record { value: values[0] });
                    return (native[0] as i32) + values[1] + offset;
                }
                export fn local(value: i32) -> i32 => add(value, 5);
            )",
                "add(value, 5)"
            ),
            {.test_mode = TestGenerationMode::None,
             .linkage_domain = *LinkageDomain::explicit_value("shared_body_references")}
        );
        const auto names = instance_names(compilation, "add");
        if (!expect_equal(names.size(), 1uz)) {
            return;
        }
        struct References final {
            const TargetUnit& unit;
            std::vector<std::string> names;
            auto visit_type(TargetTypeID id) noexcept -> bool {
                const auto* type = std::get_if<TargetNamedType>(&unit.type(id).value);
                if (type != nullptr) {
                    auto spelling = std::string();
                    for (const auto& part : type->name.components()) {
                        spelling += "::";
                        spelling += part.spelling();
                    }
                    names.push_back(std::move(spelling));
                }
                return true;
            }
            auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                -> bool {
                const auto* name = std::get_if<TargetNameExpr>(&expression.value);
                if (name != nullptr) {
                    auto spelling = std::string();
                    for (const auto& part : name->name.components()) {
                        spelling += "::";
                        spelling += part.spelling();
                    }
                    names.push_back(std::move(spelling));
                }
                return true;
            }
        };
        struct Instances final {
            const TargetUnit* unit;
            const std::flat_set<std::string>& instances;
            std::vector<std::vector<std::string>> references;
            auto enter_declaration(const TargetDecl& declaration) noexcept -> bool {
                const auto* function = std::get_if<TargetFunctionDecl>(&declaration);
                if (function == nullptr
                    || !instances.contains(
                        std::string(function->name.components().back().spelling())
                    )) {
                    return true;
                }
                const auto* definition = std::get_if<TargetFreeFunctionDefinition>(&function->form);
                if (definition == nullptr) {
                    return true;
                }
                auto query = References {.unit = *unit, .names = {}};
                require(traverse_target_statements(definition->body, query));
                references.push_back(std::move(query.names));
                expect(function->inline_specifier);
                return true;
            }
        } query {.unit = nullptr, .instances = names, .references = {}};
        for (const auto artifact : compilation.target().artifacts()) {
            const auto unit = lower_artifact(compilation, artifact.id);
            query.unit = &unit;
            require(traverse_target_unit(unit.sections(), query));
        }
        if (!expect_equal(query.references.size(), 2uz)) {
            return;
        }
        expect(!query.references[0].empty());
        expect_equal(query.references[0], query.references[1]);
    };

    "Generation: static arguments select deduplicated native instances with runtime-only ABI"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
            const fn index_value() -> i32 => 3;
            fn add(value: i32, const control: i32) -> i32 => value + control;
            fn use(value: i32) -> i32 => add(value, index_value()) + add(value, 3) + add(value, 4);
        )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("staged_calls")}
            );
            const auto names = instance_names(compilation, "add");
            expect_equal(names.size(), 2uz);
            auto definitions = StagedDefinitions {.instances = names, .count = 0uz, .controls = {}};
            auto calls =
                CallFacts {.instances = names, .staged_calls = 0uz, .compile_only_calls = 0uz};
            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                require(traverse_target_unit(unit.sections(), definitions));
                require(traverse_target_unit(unit.sections(), calls));
            }
            expect_equal(definitions.count, 2uz);
            expect(definitions.controls.contains(3u));
            expect(definitions.controls.contains(4u));
            expect_equal(calls.staged_calls, 3uz);
            expect_equal(calls.compile_only_calls, 0uz);
        };

    "Generation: const for expands with outward loop exits"_test = [] static noexcept {
        const auto compilation = PlannedCompilation::build(
            analyze_test_program(R"(
            fn add(value: i32, const control: i32) -> i32 => value + control;
            fn accumulate(value: i32) -> i32 {
                var total: i32 = 0;
                const for index in 0..3 {
                    let digit = add(value, index);
                    if value < 0 { continue; }
                    if value == 0 { break; }
                    total += digit;
                }
                return total;
            }
        )"),
            {.test_mode = TestGenerationMode::None,
             .linkage_domain = *LinkageDomain::explicit_value("expanded_range")}
        );
        const auto names = instance_names(compilation, "add");
        expect_equal(names.size(), 3uz);

        struct RangeFacts final {
            const std::flat_set<std::string>& instances;
            std::size_t ranges;
            std::size_t calls;

            auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                ranges += std::holds_alternative<TargetRangeForStmt>(statement.value);
                return true;
            }

            auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                -> bool {
                const auto* call = std::get_if<TargetCallExpr>(&expression.value);
                const auto* name = call == nullptr
                    ? nullptr
                    : std::get_if<TargetNameExpr>(
                          &template_primary_expression(*call->callee).value
                      );
                if (name != nullptr
                    && instances.contains(std::string(name->name.components().back().spelling()))) {
                    ++calls;
                    expect_equal(call->arguments.size(), 1uz);
                }
                return true;
            }
        } facts {.instances = names, .ranges = 0uz, .calls = 0uz};

        for (const auto artifact : compilation.target().artifacts()) {
            const auto unit = lower_artifact(compilation, artifact.id);
            require(traverse_target_unit(unit.sections(), facts));
        }
        expect_equal(facts.ranges, 0uz);
        expect_equal(facts.calls, 3uz);
    };

    "Generation: empty const for emits no native iteration or keyed callee"_test =
        [] static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(R"(
            fn add(value: i32, const control: i32) -> i32 => value + control;
            fn probe(value: i32) -> i32 {
                const for index in 3..3 { add(value, index); }
                return value;
            }
        )"),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("empty_expanded_range")}
            );
            expect(instance_names(compilation, "add").empty());

            struct Query final {
                std::size_t ranges;
                std::size_t calls;

                auto enter_statement(const TargetStmt& statement) noexcept -> bool {
                    ranges += std::holds_alternative<TargetRangeForStmt>(statement.value);
                    return true;
                }

                auto enter_expression(const TargetExpr& expression, TargetExpressionRole) noexcept
                    -> bool {
                    const auto* call = std::get_if<TargetCallExpr>(&expression.value);
                    if (call != nullptr) {
                        if (std::holds_alternative<TargetNameExpr>(
                                template_primary_expression(*call->callee).value
                            )) {
                            ++calls;
                        }
                    }
                    return true;
                }
            } query {.ranges = 0uz, .calls = 0uz};

            for (const auto artifact : compilation.target().artifacts()) {
                const auto unit = lower_artifact(compilation, artifact.id);
                require(traverse_target_unit(unit.sections(), query));
            }
            expect_equal(query.ranges, 0uz);
            expect_equal(query.calls, 0uz);
        };

    "Generation: omitted native calls introduce no definition edges"_test = [] static noexcept {
        const auto compilation = PlannedCompilation::build(
            analyze_test_program(R"(
                fn lane(const index: i32) -> i32 => index;
                fn use(const amount: i32) -> bool => false && (lane(amount) == 7);
                fn caller() -> bool => use(7);
            )"),
            {.test_mode = TestGenerationMode::None,
             .linkage_domain = *LinkageDomain::explicit_value("omitted_native_edges")}
        );
        const auto names = instance_names(compilation, "lane");
        expect_equal(names.size(), 1uz);
        auto query = UnitNames {
            .instances = names,
            .declarations = {},
            .definitions = {},
            .calls = {},
            .inline_instances = 0uz,
        };
        for (const auto artifact : compilation.target().artifacts()) {
            const auto unit = lower_artifact(compilation, artifact.id);
            require(traverse_target_unit(unit.sections(), query));
        }
        for (const auto& name : names) {
            expect(!query.calls.contains(name));
            expect(!query.definitions.contains(name));
        }
    };

    "Generation: selected returns terminate recursive static expansion"_test = [] static noexcept {
        const auto compilation = PlannedCompilation::build(
            analyze_test_program(R"(
            fn countdown(const remaining: i32) -> i32 {
                const if remaining == 0 { return 0; }
                return countdown(remaining - 1);
            }
            fn use() -> i32 => countdown(3);
        )"),
            {.test_mode = TestGenerationMode::None,
             .linkage_domain = *LinkageDomain::explicit_value("terminating_static_recursion")}
        );
        expect_equal(instance_names(compilation, "countdown").size(), 4uz);
        for (const auto artifact : compilation.target().artifacts()) {
            static_cast<void>(lower_artifact(compilation, artifact.id));
        }
    };

    "Generation: unselected const if arms create no instances"_test = [] static noexcept {
        struct Input final {
            std::string_view name;
            std::string_view source;
        };
        const auto cases = std::to_array<Input>({
            {.name = "unselected arm", .source = R"(
            fn lane(const index: i32) -> i32 => index;
            fn use(count: i32, const enabled: bool) -> i32 {
                var total = 0;
                for index in 0..count {
                    const if enabled { total += lane(1); } else { total += index; }
                }
                return total;
            }
            fn caller(count: i32) -> i32 => use(count, false);
        )"},
            {.name = "static exit before the call", .source = R"(
            fn lane(const index: i32) -> i32 => index;
            fn use(const enabled: bool) -> i32 {
                const if !enabled { return 0; }
                return lane(1);
            }
            fn caller() -> i32 => use(false);
        )"},
            {.name = "static break before later iterations", .source = R"(
            fn lane(const index: i32) -> i32 => index;
            fn use() -> i32 {
                var total = 0;
                const for index in 0..4 {
                    const if index == 0 { break; }
                    total += lane(index);
                }
                return total;
            }
        )"},
        });
        each(cases, &Input::name, [](const auto& input) static noexcept {
            const auto compilation = PlannedCompilation::build(
                analyze_test_program(std::string(input.source)),
                {.test_mode = TestGenerationMode::None,
                 .linkage_domain = *LinkageDomain::explicit_value("unselected_static_arm")}
            );
            expect(instance_names(compilation, "lane").empty());
            for (const auto artifact : compilation.target().artifacts()) {
                static_cast<void>(lower_artifact(compilation, artifact.id));
            }
        });
    };
});

} // namespace
