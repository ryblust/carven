module carven:test.internal.semantic.analysis.constant_functions;

import :diagnostics.code;
import :frontend.program.parse;
import :semantic.analyze;
import :semantic.semir.body;
import :semantic.semir.constant;
import :semantic.semir.decl;
import :semantic.semir.ids;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :source.batch;
import :source.manager;
import :source.module_path;
import :test.harness.diagnostics;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import std;

namespace {

namespace ct = carven::testing;

auto module_constant(const SemIRProgram& program, std::string_view name) noexcept
    -> const ConstantFact& {
    auto found = std::optional<ConstantID>();
    for (const auto declaration : program.declarations().module_constants()) {
        if (program.provenance().spelling(declaration.value.name) == name) {
            found = declaration.value.value;
        }
    }
    ct::require(found.has_value());
    return program.constants().constant(*found);
}

auto function_body(const SemIRProgram& program, std::string_view name) noexcept
    -> const SemIRBody& {
    auto found = std::optional<BodyID>();
    for (const auto declaration : program.declarations().functions()) {
        if (program.provenance().spelling(declaration.value.name) == name) {
            found = callable_body_id(program.declarations().callable(declaration.value.callable));
        }
    }
    ct::require(found.has_value());
    return program.bodies().body(*found);
}

auto require_integer(const ConstantFact& fact, std::int64_t expected) noexcept -> void {
    const auto* integer = std::get_if<IntegerConstant>(&fact.value);
    if (!ct::expect(integer != nullptr)) {
        return;
    }
    ct::expect(integer->as_signed() == expected);
}

auto require_text(
    const SemIRProgram& program,
    const ConstantFact& fact,
    std::string_view expected
) noexcept -> void {
    const auto* type = std::get_if<BuiltinTypeValue>(&program.types().type(fact.type).value);
    if (!ct::expect(type != nullptr)) {
        return;
    }
    ct::expect(type->kind == BuiltinType::Str);
    const auto* text = std::get_if<StringConstant>(&fact.value);
    if (!ct::expect(text != nullptr)) {
        return;
    }
    ct::expect(program.provenance().spelling(text->value) == expected);
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test("Constant functions: execute for constants and array extents", [] static noexcept {
        const auto program = analyze_test_program(R"(
        const answer = increment(41);
        const repeated = increment(8);
        const fn increment(value: i32) -> i32 => value + 1;
        const fn extent() -> usize => 3usize;
        fn local() -> i32 { const result = increment(4); return result; }
        fn array() -> i32 {
            let values: [i32; extent()] = [1, 2, 3];
            return values[2];
        }
    )");
        require_integer(module_constant(program, "answer"), 42);
        require_integer(module_constant(program, "repeated"), 9);
        auto local_constants = 0uz;
        visit_semantic_nodes(
            function_body(program, "local").realized_region(),
            [&](const SemanticExpression& value) noexcept {
                ct::expect(!(std::holds_alternative<SemCall>(value.value)));
                if (const auto* constant = std::get_if<SemConstant>(&value.value)) {
                    require_integer(program.constants().constant(constant->constant), 5);
                    ++local_constants;
                }
            }
        );
        ct::expect(local_constants == 1uz);
        auto arrays = 0uz;
        visit_semantic_nodes(
            function_body(program, "array").region(),
            [&](const SemanticExpression& value) noexcept {
                ct::expect(!(std::holds_alternative<SemCall>(value.value)));
                if (std::holds_alternative<SemArray>(value.value)) {
                    const auto* type = std::get_if<ArrayTypeValue>(
                        &program.types().type(value.type.resolved()).value
                    );
                    if (!ct::expect(type != nullptr)) {
                        return;
                    }
                    ct::expect(type->extent == 3u);
                    ++arrays;
                }
            }
        );
        ct::expect(arrays == 1uz);
        ct::expect(program.bodies().size() == 4uz);
    });

    ct::test(
        "Constant functions: repeated recursive evaluation owns one typed body per function",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        const first = factorial(5);
        const second = factorial(6);
        const third = factorial(3);
        const fn factorial(value: i32) -> i32 {
            return match value {
                ..2 => 1,
                _ => value * factorial(value - 1),
            };
        }
        fn ordinary() -> i32 => factorial(4);
    )");
            require_integer(module_constant(program, "first"), 120);
            require_integer(module_constant(program, "second"), 720);
            require_integer(module_constant(program, "third"), 6);
            ct::expect(program.declarations().functions().size() == 2uz);
            ct::expect(program.bodies().size() == 2uz);
            auto bodies = std::flat_set<BodyID>();
            for (const auto declaration : program.declarations().functions()) {
                const auto body =
                    callable_body_id(program.declarations().callable(declaration.value.callable));
                if (!ct::expect(body.has_value())) {
                    return;
                }
                ct::expect(bodies.insert(*body).second);
            }
            auto calls = 0uz;
            visit_semantic_nodes(
                function_body(program, "ordinary").region(),
                [&](const SemanticExpression& value) noexcept {
                    calls += std::holds_alternative<SemCall>(value.value);
                }
            );
            ct::expect(calls == 1uz);
        }
    );

    ct::test(
        "Constant functions: owning text freezes at the initializer boundary and runtime calls stay owning",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        const frozen: str = decorate(make());
        const fn make() -> String {
            var result = String {};
            result.push('我');
            result.append("\0😀");
            return result;
        }
        const fn decorate(value: String) -> String => f"[{value}]";
        fn ordinary() -> String => decorate(make());
        fn local_text() -> str { const local = make(); return local; }
    )");
            require_text(
                program,
                module_constant(program, "frozen"),
                std::string_view("[我\0😀]", 10uz)
            );
            auto calls = 0uz;
            visit_semantic_nodes(
                function_body(program, "ordinary").region(),
                [&](const SemanticExpression& value) noexcept {
                    if (!std::holds_alternative<SemCall>(value.value)) {
                        return;
                    }
                    const auto* type = std::get_if<BuiltinTypeValue>(
                        &program.types().type(value.type.resolved()).value
                    );
                    if (!ct::expect(type != nullptr)) {
                        return;
                    }
                    ct::expect(type->kind == BuiltinType::String);
                    ct::expect(!(value.constant.has_value()));
                    ++calls;
                }
            );
            ct::expect(calls == 2uz);
            auto frozen_reads = 0uz;
            visit_semantic_nodes(
                function_body(program, "local_text").realized_region(),
                [&](const SemanticExpression& value) noexcept {
                    ct::expect(!(std::holds_alternative<SemCall>(value.value)));
                    if (const auto* constant = std::get_if<SemConstant>(&value.value)) {
                        require_text(
                            program,
                            program.constants().constant(constant->constant),
                            std::string_view("我\0😀", 8uz)
                        );
                        ++frozen_reads;
                    }
                }
            );
            ct::expect(frozen_reads == 1uz);
            ct::expect(program.bodies().size() == 4uz);
        }
    );

    ct::test(
        "Constant functions: declaration dependency cycles and result inference cycles stay distinct",
        [] static noexcept {
            struct Scenario final {
                std::string_view source;
                DiagnosticCode expected;
                DiagnosticCode absent;
            };

            const auto scenarios = std::to_array<Scenario>({
                {"const value = typed(); const fn typed() -> i32 { return value; }",
                 DiagnosticCode::ConstCycle,
                 DiagnosticCode::TypeResultInferenceCycle},
                {"const value = inferred(); const fn inferred() => inferred();",
                 DiagnosticCode::TypeResultInferenceCycle,
                 DiagnosticCode::ConstCycle},
            });
            ct::each(scenarios, &Scenario::source, [&](const auto& scenario) noexcept {
                const auto diagnostics = analyze_test_errors(std::string(scenario.source));
                const auto* finding = ct::find_diagnostic(diagnostics, scenario.expected);
                if (!(ct::expect(finding != nullptr))) {
                    return;
                }
                ct::expect(finding->attachment.primary.has_value());
                ct::expect_no_diagnostic(diagnostics, scenario.absent);
            });
        }
    );

    ct::test(
        "Constant functions: forward body requests cross the compilation module graph",
        [] static noexcept {
            auto sources = SourceManager();
            auto inputs = std::vector<SourceModuleInput>();
            const auto append = [&](std::string_view name, std::string text) noexcept {
                const auto source =
                    sources.append_virtual(std::format("{}.cv", name), std::move(text));
                ct::require(source.has_value());
                const auto path = CanonicalModulePath::from_value(name);
                ct::require(path.has_value());
                inputs.push_back({.source_id = *source, .module_path = *path});
            };
            append(
                "application",
                "import api using answer; const result = answer(); fn read() -> i32 => result;"
            );
            append(
                "api",
                "import values using { increment, seed }; export const fn answer() -> i32 => increment(seed);"
            );
            append(
                "values",
                "export const seed: i32 = 41; export const fn increment(value: i32) -> i32 => value + 1;"
            );
            auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
            if (!ct::expect(syntax.has_value())) {
                return;
            }
            const auto analyzed = analyze(std::move(*syntax));
            if (!ct::expect(analyzed.has_value())) {
                return;
            }
            const auto& program = analyzed->value;
            require_integer(module_constant(program, "result"), 42);
            ct::expect(program.bodies().size() == 3uz);
            auto reads = 0uz;
            visit_semantic_nodes(
                function_body(program, "read").region(),
                [&](const SemanticExpression& value) noexcept {
                    ct::expect(!(std::holds_alternative<SemCall>(value.value)));
                    if (const auto* constant = std::get_if<SemConstant>(&value.value)) {
                        require_integer(program.constants().constant(constant->constant), 42);
                        ++reads;
                    }
                }
            );
            ct::expect(reads == 1uz);
        }
    );

    ct::test(
        "Constant text: direct interpolation composes as owning text before final freezing",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        const title = f"build-{42:04}";
        const exact = f"{'我'}\0{'😀'}";
        const wrapped = wrap(f"{title}");
        const count = wrap(f"{title}").len();
        const copy = String::from_str(f"{title}");
        const view = f"{title}".as_str();
        const empty = String {};
        const fn wrap(value: String) -> String => f"[{value}]";
    )");
            require_text(program, module_constant(program, "title"), "build-0042");
            require_text(
                program,
                module_constant(program, "exact"),
                std::string_view("我\0😀", 8uz)
            );
            require_text(program, module_constant(program, "wrapped"), "[build-0042]");
            require_integer(module_constant(program, "count"), 12);
            require_text(program, module_constant(program, "copy"), "build-0042");
            require_text(program, module_constant(program, "view"), "build-0042");
            require_text(program, module_constant(program, "empty"), "");
        }
    );

    ct::test(
        "Constant text: incremental growth and length queries build persistent text",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        const fn build() -> String {
            var text = String {};
            while text.len() < 8000usize {
                text.push('a');
                text.append("bc");
                text.append_format(f"{7}");
            }
            return text;
        }
        const fn copy(value: String) -> String => value;
        const result = copy(build());
    )");
            auto expected = std::string();
            for (auto index = 0uz; index < 2000uz; ++index) {
                expected += "abc7";
            }
            require_text(program, module_constant(program, "result"), expected);
        }
    );

    ct::test(
        "Constant functions: temporary execution history does not enlarge retained constants",
        [] static noexcept {
            const auto count = [](int iterations) static noexcept {
                const auto program = analyze_test_program(
                    std::format(
                        R"(
            const fn compute() -> i32 {{
                var text = String {{}};
                var value = 0;
                for index in 0..{} {{
                    value = index * 7;
                    text.clear();
                    text.append_format(f"{{value}}");
                    let equal = text == text;
                    let length = text.as_str().len();
                }}
                return 0;
            }}
            const result = compute();
        )",
                        iterations
                    )
                );
                require_integer(module_constant(program, "result"), 0);
                return program.constants().size();
            };
            ct::expect(count(8) == count(256));
        }
    );

    ct::test(
        "Constant functions: nested retained children become independent mutable storage",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        const row = [1, 2];
        const fn change(input: [[i32; 2]; 2]) -> i32 {
            var values = input;
            values[1][0] = 9;
            return values[0][0] + values[1][0];
        }
        const result = change([row, row]);
        const unchanged = row[0];
    )");
            require_integer(module_constant(program, "result"), 10);
            require_integer(module_constant(program, "unchanged"), 1);
        }
    );

    ct::test(
        "Constant functions: floating formatting executes through native conversion",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        const fn label(value: f64) -> String => f"{value:.2f}";
        const text = label(1.5);
        const length = text.len();
    )");
            require_integer(module_constant(program, "length"), 4);
        }
    );

    ct::test(
        "Constant functions: invalid floating specifications and dimensions are diagnosed",
        [] static noexcept {
            const auto cases = std::to_array<std::string_view>({
                R"(const text = f"{1.25:.}";)",
                R"(const text = f"{1.25:.{-1}f}";)",
                R"(const text = f"{1.25:{-1}.2f}";)",
                R"(const text = f"{1.25:.{true}f}";)",
                R"(const text = f"{1.25:00}";)",
            });
            ct::each(cases, std::identity {}, [](std::string_view source) static noexcept {
                ct::expect_diagnostic(
                    analyze_test_errors(std::string(source)),
                    DiagnosticCode::ConstEvaluation
                );
            });
            ct::expect_diagnostic(
                analyze_test_errors(R"(const text = f"{1.25:.1048577f}";)"),
                DiagnosticCode::ConstLimit
            );
        }
    );

    ct::test(
        "Constant failures: root propagation uses inferred contracts and preserves static gates",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        struct Failure { code: i32 }
        private const fn inner(ok: bool) -> i32 {
            if ok { return 2; }
            throw Failure { 7 };
        }
        private const fn outer(value: i32) -> i32 {
            if value > 0 { return value + 1; }
            throw Failure { 9 };
        }
        const nested = outer(inner(true)?)?;
        const combined = (inner(true) + outer(2))?;
        const extent: [i32; inner(true)?] = [1, 2];
    )");
            require_integer(module_constant(program, "nested"), 3);
            require_integer(module_constant(program, "combined"), 5);

            struct Scenario final {
                std::string_view source;
                DiagnosticCode code;
            };

            const auto scenarios = std::to_array<Scenario>({
                {R"(struct Failure {} private const fn call(ok: bool) -> i32 {
            if ok { return 1; } throw Failure {};
        } const value = call(true);)",
                 DiagnosticCode::EffectUnmarked},
                {R"(private const fn call() -> i32 => 1; const value = call()?;)",
                 DiagnosticCode::EffectPropagateRedundant},
                {R"(struct Failure {} const fn call() -> i32 throw Failure {
            throw Failure {};
        } const value = call()?;)",
                 DiagnosticCode::ConstEvaluation},
                {R"(struct Failure {} const fn call(ok: bool) -> i32 throw Failure {
            if ok { return 1; } throw Failure {};
        } const fn add(a: i32, b: i32) -> i32 => a + b;
        const value = add(call(true)?, call(true));)",
                 DiagnosticCode::EffectUnmarked},
                {R"(struct Failure {} const fn call() -> i32 throw Failure { return 1; }
        const test "static root" { check(call()? == 1); })",
                 DiagnosticCode::EffectRootUnhandled},
                {R"(struct Failure {} const fn call() -> i32 throw Failure { return 1; }
        const fn wrapper() -> i32 => call(); const value = wrapper();)",
                 DiagnosticCode::EffectUnmarked},
                {R"(struct Failure {} const fn call() -> i32 { throw Failure {}; })",
                 DiagnosticCode::EffectThrowPublished},
                {R"(const fn call() { rethrow; })", DiagnosticCode::EffectRethrowContext},
            });
            ct::each(scenarios, &Scenario::source, [&](const auto& scenario) noexcept {
                ct::expect_diagnostic(
                    analyze_test_errors(std::string(scenario.source)),
                    scenario.code
                );
            });
        }
    );

    ct::test(
        "Constant failures: wrapping arithmetic is shared with runtime execution",
        [] static noexcept {
            const auto arithmetic = analyze_test_program(R"(
        struct Failure {}
        const fn increment(value: i32) -> i32 => value + 1;
        const fn recover() -> i32 {
            return increment(2147483647);
        }
        const value = recover();
    )");
            require_integer(module_constant(arithmetic, "value"), -2147483648ll);
        }
    );

    ct::test("Constant failures: language recovery cannot catch failed tests", [] static noexcept {
        const auto checks = analyze_test_errors(R"(
        struct Failure {}
        const fn fail() throw Failure { check(false); }
        const test "cannot catch assertions" {
            try { fail()?; } catch { _ => {}, }
        }
    )");
        ct::expect_diagnostic(checks, DiagnosticCode::ConstTest);
    });

    ct::test(
        "Constant aggregates: owning text executes but does not freeze into nominal str fields",
        [] static noexcept {
            const auto program = analyze_test_program(R"(
        struct Text { value: String }
        enum Message { Owned(String), Empty }
        const fn length() -> usize {
            let values: [String; 2] = ["one", "two"];
            let text = Text { values[0] };
            let message = Message::Owned(text.value);
            return match message { .Owned(value) => value.len(), .Empty => 0usize, };
        }
        const size = length();
    )");
            require_integer(module_constant(program, "size"), 3);
            const auto cases = std::to_array<std::string_view>({
                R"(struct Text { value: String }
            const fn make() -> Text => Text { "one" };
            const value = make();)",
                R"(enum Text { Owned(String) }
            const fn make() -> Text => Text::Owned("one");
            const value = make();)",
            });
            ct::each(cases, std::identity {}, [](std::string_view source) static noexcept {
                ct::expect_diagnostic(
                    analyze_test_errors(std::string(source)),
                    DiagnosticCode::ConstInitializer
                );
            });
        }
    );
});

} // namespace
