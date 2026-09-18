module;
#define DOCTEST_CONFIG_NO_EXCEPTIONS_BUT_WITH_ALL_ASSERTS
#include <doctest/doctest.h>

module carven:test.internal.semantic.evaluation.execution;

import :semantic.analysis.body.builder;
import :semantic.analysis.catalog;
import :semantic.analysis.construction;
import :semantic.evaluation.execution;
import :semantic.semir.decl;
import :source.batch;
import :source.text;
import :test.internal.semantic.evaluation.fixture;
import std;

namespace {

class ExecutionContext final : public SemanticExecutionContext {
public:
    ExecutionContext(
        ProgramDraft& draft,
        ProgramConstruction& construction,
        ProgramModuleID module
    ) noexcept;
    auto function_for_callable(CallableID callable) const noexcept
        -> std::optional<FunctionID> override;
    auto prepare_call(FunctionID function, ProgramOriginID origin) noexcept
        -> ContinuationTask<std::expected<ExecutionCallBody, ExecutionCallFailure>> override;
    auto report(const ExecutionDiagnostic& diagnostic) noexcept -> void override;

    auto write(ExecutionOutputStream, std::string_view) noexcept -> void override;

    std::string output;
    std::vector<FunctionID> calls;
    std::vector<ExecutionDiagnostic> diagnostics;

private:
    ProgramDraft& draft;
    ProgramConstruction& construction;
    ProgramModuleID module;
};

ExecutionContext::ExecutionContext(
    ProgramDraft& draft,
    ProgramConstruction& construction,
    ProgramModuleID module
) noexcept
    : draft(draft),
      construction(construction),
      module(module) {}

auto ExecutionContext::write(ExecutionOutputStream, std::string_view bytes) noexcept -> void {
    output += bytes;
}

auto ExecutionContext::function_for_callable(CallableID callable) const noexcept
    -> std::optional<FunctionID> {
    return draft.function_for_callable(callable);
}

auto ExecutionContext::prepare_call(FunctionID function, ProgramOriginID origin) noexcept
    -> ContinuationTask<std::expected<ExecutionCallBody, ExecutionCallFailure>> {
    calls.push_back(function);
    const auto body = co_await construction
                          .ensure_function_body(function, module, draft.source_origin(origin).span);
    REQUIRE(body.has_value());
    REQUIRE(draft.body_draft(*body).inputs.parameters.empty());
    co_return ExecutionCallBody {
        .body = ExecutionBody(draft.body_draft(*body)),
        .parameter_types = {}
    };
}

auto ExecutionContext::report(const ExecutionDiagnostic& diagnostic) noexcept -> void {
    diagnostics.push_back(diagnostic);
}

template<typename Action>
auto with_execution(std::string source_text, Action action) noexcept -> void {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("execution.cv", std::move(source_text));
    REQUIRE(source.has_value());
    const auto inputs = std::array {SourceModuleInput {
        .source_id = *source,
        .module_path = constant_test_module_path("execution")
    }};
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    REQUIRE(syntax.has_value());
    auto diagnostics = DiagnosticSink();
    auto draft = ProgramDraft::begin(std::move(*syntax), diagnostics);
    auto catalog = build_analysis_catalog(draft);
    REQUIRE(catalog.has_value());
    const auto view = catalog->view();
    auto usage = ImportUsage(view.imports().size());
    auto construction = ProgramConstruction(draft, view, usage);
    REQUIRE(construction.run().has_value());
    const auto module = view.modules().front().module_id;
    const auto origin = draft.append_source_origin(draft.module_source(module), Span::at(0u));
    auto context = ExecutionContext(draft, construction, module);
    const auto evaluate = [&](std::string_view name,
                              ExecutionLimits limits = constant_execution_limits()) noexcept {
        const auto found = std::ranges::find(view.symbols(), name, &CatalogSymbol::name);
        REQUIRE(found != view.symbols().end());
        const auto function = std::get<CatalogFunctionForm>(found->form);
        const auto contract = draft.construction_callable_contract_copy(function.callable);
        auto root = BodyBuilder(draft.reserve_body(BodyKind::Test), draft);
        const auto lifetime =
            root.add_lifetime_region(std::nullopt, LifetimeRegionKind::Lexical, origin);
        const auto expression = root.make_expression(
            contract.result,
            lifetime,
            origin,
            SemCall {
                .callee = OwnedSemanticExpression(root.make_expression(
                    draft.intern_type({.value = FunctionTypeValue {.callable = function.callable}}),
                    lifetime,
                    origin,
                    SemCallable {.callable = function.callable}
                )),
                .arguments = {},
                .callee_failures = BodyFailures(draft.add_empty_failure_term())
            }
        );
        context.output.clear();
        context.calls.clear();
        context.diagnostics.clear();
        return execute_constant_root(draft, context, expression, limits).run();
    };
    action(draft, context, evaluate);
}

auto check_limit(const ExecutionContext& context, std::string_view resource) noexcept -> void {
    CHECK(context.diagnostics.size() == 1uz);
    if (context.diagnostics.size() != 1uz) {
        return;
    }
    CHECK(context.diagnostics.front().code == DiagnosticCode::ConstLimit);
    CHECK(context.diagnostics.front().message.contains(resource));
}

auto require_integer(
    const ConstantValueReader& values,
    const ExecutionValue& result,
    std::int64_t expected
) noexcept -> void {
    const auto atom = execution_atom(values, result);
    REQUIRE(atom.has_value());
    CHECK(std::get<IntegerConstant>(atom->value) == IntegerConstant::from_signed(expected));
}

} // namespace

TEST_CASE("Constant execution: logical operators preserve results and required calls") {
    struct Scenario final {
        std::string_view expression;
        bool right;
        bool result;
        std::size_t calls;
    };

    const auto scenarios = std::array {
        Scenario {"false && right()", true, false, 1uz},
        Scenario {"true && right()", false, false, 2uz},
        Scenario {"true && right()", true, true, 2uz},
        Scenario {"false || right()", false, false, 2uz},
        Scenario {"false || right()", true, true, 2uz},
        Scenario {"true || right()", false, true, 1uz},
    };
    for (const auto& scenario : scenarios) {
        CAPTURE(scenario.expression);
        CAPTURE(scenario.right);
        with_execution(
            std::format(
                "const fn right() -> bool => {}; const fn run() -> bool => {};",
                scenario.right,
                scenario.expression
            ),
            [&](ProgramDraft& draft, ExecutionContext& context, const auto& evaluate) {
                const auto result = evaluate("run");
                REQUIRE(result.has_value());
                const auto atom = execution_atom(draft, *result);
                REQUIRE(atom.has_value());
                CHECK(std::get<BooleanConstant>(atom->value).value == scenario.result);
                CHECK(context.calls.size() == scenario.calls);
                CHECK(context.diagnostics.empty());
            }
        );
    }
}

TEST_CASE("Constant execution: aggregate work counts constructed and copied slots") {
    struct Scenario final {
        std::string_view source;
        std::size_t work;
        std::int64_t result;
    };

    const auto scenarios = std::array {
        Scenario {
            R"(const fn run() -> i32 {
            let initial = [[1, 2], [3, 4]];
            var copy = initial; copy[1][0] = 9;
            return initial[1][0] + copy[1][0];
        })",
            12uz,
            12
        },
        Scenario {
            R"(struct Entry { values: [i32; 2] }
        const fn run() -> i32 {
            let initial = Entry { [1, 2] };
            var copy = initial; copy.values[0] = 9;
            return initial.values[0] + copy.values[0];
        })",
            6uz,
            10
        },
        Scenario {
            R"(const fn run() -> i32 {
            let initial = [[1, 2], [3, 4]];
            let moved = &&initial; return moved[1][0];
        })",
            6uz,
            3
        },
        Scenario {"const data = [[1, 2], [3, 4]]; const fn run() -> i32 => data[1][0];", 0uz, 3},
    };
    for (const auto& scenario : scenarios) {
        CAPTURE(scenario.source);
        with_execution(
            std::string(scenario.source),
            [&](ProgramDraft& draft, ExecutionContext& context, const auto& evaluate) {
                if (scenario.work != 0uz) {
                    CHECK_FALSE(evaluate(
                                    "run",
                                    {.steps = maximum_constant_steps,
                                     .text_work = 8uz * maximum_constant_text_bytes,
                                     .aggregate_work = scenario.work - 1uz}
                    )
                                    .has_value());
                    check_limit(context, "aggregate");
                }
                const auto result = evaluate(
                    "run",
                    {.steps = maximum_constant_steps,
                     .text_work = 8uz * maximum_constant_text_bytes,
                     .aggregate_work = scenario.work}
                );
                REQUIRE(result.has_value());
                require_integer(draft, *result, scenario.result);
                CHECK(context.diagnostics.empty());
            }
        );
    }
}

TEST_CASE("Constant execution: calls share work within a root and new roots start independently") {
    with_execution(
        R"(
        const fn leaf() -> [i32; 2] => [1, 2];
        const fn run() -> i32 {
            let first = leaf(); let second = leaf(); return first[0] + second[0];
        }
    )",
        [](ProgramDraft& draft, ExecutionContext& context, const auto& evaluate) static {
            for (auto root = 0uz; root < 2uz; ++root) {
                REQUIRE(evaluate(
                            "leaf",
                            {.steps = maximum_constant_steps,
                             .text_work = 8uz * maximum_constant_text_bytes,
                             .aggregate_work = 2uz}
                )
                            .has_value());
                CHECK(context.diagnostics.empty());
            }
            CHECK_FALSE(evaluate(
                            "run",
                            {.steps = maximum_constant_steps,
                             .text_work = 8uz * maximum_constant_text_bytes,
                             .aggregate_work = 3uz}
            )
                            .has_value());
            check_limit(context, "aggregate");
            const auto result = evaluate(
                "run",
                {.steps = maximum_constant_steps,
                 .text_work = 8uz * maximum_constant_text_bytes,
                 .aggregate_work = 4uz}
            );
            REQUIRE(result.has_value());
            require_integer(draft, *result, 2);
        }
    );
}

TEST_CASE("Constant execution: step limits include nested calls and recursive equality") {
    with_execution(
        R"(
        const data = [[1, 2], [3, 4]];
        const fn leaf() -> i32 => 1;
        const fn run() -> i32 => leaf() + leaf();
        const fn equal() -> bool => data == data;
    )",
        [](ProgramDraft&, ExecutionContext& context, const auto& evaluate) static {
            CHECK_FALSE(evaluate(
                            "leaf",
                            {.steps = 0uz,
                             .text_work = 8uz * maximum_constant_text_bytes,
                             .aggregate_work = maximum_constant_aggregate_work}
            )
                            .has_value());
            check_limit(context, "steps");
            CHECK_FALSE(evaluate(
                            "leaf",
                            {.steps = 4uz,
                             .text_work = 8uz * maximum_constant_text_bytes,
                             .aggregate_work = maximum_constant_aggregate_work}
            )
                            .has_value());
            check_limit(context, "steps");
            // Call, invocation, body region, return, and literal each use one step.
            for (auto root = 0uz; root < 2uz; ++root) {
                CHECK(evaluate(
                          "leaf",
                          {.steps = 5uz,
                           .text_work = 8uz * maximum_constant_text_bytes,
                           .aggregate_work = maximum_constant_aggregate_work}
                )
                          .has_value());
            }
            CHECK_FALSE(evaluate(
                            "run",
                            {.steps = 14uz,
                             .text_work = 8uz * maximum_constant_text_bytes,
                             .aggregate_work = maximum_constant_aggregate_work}
            )
                            .has_value());
            check_limit(context, "steps");
            CHECK(evaluate(
                      "run",
                      {.steps = 15uz,
                       .text_work = 8uz * maximum_constant_text_bytes,
                       .aggregate_work = maximum_constant_aggregate_work}
            )
                      .has_value());
            CHECK_FALSE(evaluate(
                            "equal",
                            {.steps = 13uz,
                             .text_work = 8uz * maximum_constant_text_bytes,
                             .aggregate_work = maximum_constant_aggregate_work}
            )
                            .has_value());
            check_limit(context, "comparison");
            CHECK(evaluate(
                      "equal",
                      {.steps = 14uz,
                       .text_work = 8uz * maximum_constant_text_bytes,
                       .aggregate_work = maximum_constant_aggregate_work}
            )
                      .has_value());
        }
    );
}

TEST_CASE("Constant execution: text work counts produced bytes across copies append and clear") {
    struct Scenario final {
        std::string_view body;
        std::size_t work;
        std::string_view result;
    };

    const auto scenarios = std::array {
        Scenario {R"(var text: String = "ab"; text.append("c"); return &&text;)", 3uz, "abc"},
        Scenario {R"(var text: String = "ab"; text.push('我'); return &&text;)", 5uz, "ab我"},
        Scenario {
            R"(var text: String = "ab"; text.append_format(f"{'我'}"); return &&text;)",
            8uz,
            "ab我"
        },
        Scenario {R"(let text: String = "ab"; let copy = text; return &&copy;)", 4uz, "ab"},
        Scenario {
            R"(var text: String = "ab"; text.clear(); text.append("cd"); return &&text;)",
            4uz,
            "cd"
        },
    };
    for (const auto& scenario : scenarios) {
        CAPTURE(scenario.body);
        with_execution(
            std::format("const fn run() -> String {{ {} }}", scenario.body),
            [&](ProgramDraft&, ExecutionContext& context, const auto& evaluate) {
                CHECK_FALSE(evaluate(
                                "run",
                                {.steps = maximum_constant_steps,
                                 .text_work = scenario.work - 1uz,
                                 .aggregate_work = maximum_constant_aggregate_work}
                )
                                .has_value());
                check_limit(context, "text");
                const auto result = evaluate(
                    "run",
                    {.steps = maximum_constant_steps,
                     .text_work = scenario.work,
                     .aggregate_work = maximum_constant_aggregate_work}
                );
                REQUIRE(result.has_value());
                CHECK(std::get<ExecutionOwnedText>(*result).bytes == scenario.result);
                CHECK(context.diagnostics.empty());
            }
        );
    }
}

TEST_CASE("Constant execution: print output consumes the text-work budget") {
    with_execution(
        R"(const fn run() { println("ab", 3); })",
        [](ProgramDraft&, ExecutionContext& context, const auto& evaluate) static noexcept {
            CHECK_FALSE(evaluate(
                            "run",
                            {.steps = maximum_constant_steps,
                             .text_work = 4uz,
                             .aggregate_work = maximum_constant_aggregate_work}
            )
                            .has_value());
            check_limit(context, "text");
            CHECK(context.output == "ab 3");
            CHECK(evaluate(
                      "run",
                      {.steps = maximum_constant_steps,
                       .text_work = 5uz,
                       .aggregate_work = maximum_constant_aggregate_work}
            )
                      .has_value());
            CHECK(context.output == "ab 3\n");
        }
    );
}
