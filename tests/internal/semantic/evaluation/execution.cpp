module carven:test.internal.semantic.evaluation.execution;

import :semantic.analysis.body.builder;
import :semantic.analysis.catalog;
import :semantic.analysis.construction;
import :semantic.analysis.stage.session;
import :semantic.evaluation.display;
import :semantic.evaluation.execution;
import :semantic.semir.constant_access;
import :semantic.semir.decl;
import :semantic.semir.program;
import :source.batch;
import :source.text;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import :test.internal.semantic.evaluation.fixture;
import std;

namespace {

namespace ct = carven::testing;

class BodyExecutionContext final : public SemanticExecutionContext {
public:
    explicit BodyExecutionContext(const SemIRProgram& program) noexcept;
    auto function_for_callable(CallableID callable) const noexcept
        -> std::optional<FunctionID> override;
    auto prepare_call(CallableID callable, ProgramOriginID origin) noexcept
        -> ContinuationTask<std::expected<ExecutionBody, ExecutionCallFailure>> override;
    auto report(const ExecutionEvent& event) noexcept -> void override;
    auto write(ExecutionOutputStream stream, std::string_view bytes) noexcept -> void override;

    std::vector<ExecutionEvent> reports;
    std::string output;

private:
    const SemIRProgram& program;
};

BodyExecutionContext::BodyExecutionContext(const SemIRProgram& program) noexcept
    : program(program) {}

auto BodyExecutionContext::function_for_callable(CallableID callable) const noexcept
    -> std::optional<FunctionID> {
    return program.source_function(callable);
}

auto BodyExecutionContext::prepare_call(CallableID callable, ProgramOriginID) noexcept
    -> ContinuationTask<std::expected<ExecutionBody, ExecutionCallFailure>> {
    const auto body = callable_body_id(program.declarations().callable(callable));
    ct::require(body.has_value());
    co_return ExecutionBody(program.bodies().body(*body));
}

auto BodyExecutionContext::report(const ExecutionEvent& event) noexcept -> void {
    reports.push_back(event);
}

auto BodyExecutionContext::write(ExecutionOutputStream, std::string_view bytes) noexcept -> void {
    output += bytes;
}

class ExecutionContext final : public SemanticExecutionContext {
public:
    ExecutionContext(
        ProgramDraft& draft,
        ConstructionRequests& requests,
        ProgramModuleID module_id
    ) noexcept;
    auto function_for_callable(CallableID callable) const noexcept
        -> std::optional<FunctionID> override;
    auto prepare_call(CallableID callable, ProgramOriginID origin) noexcept
        -> ContinuationTask<std::expected<ExecutionBody, ExecutionCallFailure>> override;
    auto report(const ExecutionEvent& event) noexcept -> void override;

    auto write(ExecutionOutputStream, std::string_view) noexcept -> void override;

    std::string output;
    std::vector<FunctionID> calls;
    std::vector<ExecutionEvent> reports;

private:
    ProgramDraft& draft;
    ConstructionRequests& requests;
    ProgramModuleID module_id;
};

ExecutionContext::ExecutionContext(
    ProgramDraft& draft,
    ConstructionRequests& requests,
    ProgramModuleID module_id
) noexcept
    : draft(draft),
      requests(requests),
      module_id(module_id) {}

auto ExecutionContext::write(ExecutionOutputStream, std::string_view bytes) noexcept -> void {
    output += bytes;
}

auto ExecutionContext::function_for_callable(CallableID callable) const noexcept
    -> std::optional<FunctionID> {
    return draft.function_for_callable(callable);
}

auto ExecutionContext::prepare_call(CallableID callable, ProgramOriginID origin) noexcept
    -> ContinuationTask<std::expected<ExecutionBody, ExecutionCallFailure>> {
    const auto function = *draft.function_for_callable(callable);
    calls.push_back(function);
    const auto body = co_await requests.ensure_function_body(
        function,
        module_id,
        draft.source_origin(origin).span
    );
    ct::require(body.has_value());
    ct::require(draft.body_draft(*body).inputs.parameters.empty());
    auto realized = co_await requests.stage().realize_body(*body);
    ct::require(realized.has_value());
    co_return ExecutionBody(draft.body_draft(*body));
}

auto ExecutionContext::report(const ExecutionEvent& event) noexcept -> void {
    reports.push_back(event);
}

template<typename Action>
auto with_execution(std::string source_text, Action action) noexcept -> void {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("execution.cv", std::move(source_text));
    if (!ct::expect(source.has_value())) {
        return;
    }
    const auto inputs = std::array {SourceModuleInput {
        .source_id = *source,
        .module_path = constant_test_module_path("execution")
    }};
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    if (!ct::expect(syntax.has_value())) {
        return;
    }
    auto diagnostics = DiagnosticSink();
    auto draft = ProgramDraft::begin(std::move(*syntax), diagnostics);
    auto catalog = build_analysis_catalog(draft);
    if (!ct::expect(catalog.has_value())) {
        return;
    }
    const auto view = catalog->view();
    auto usage = ImportUsage(view.imports().size());
    auto construction = ProgramConstruction(draft, view, usage);
    if (!ct::expect(construction.run().has_value())) {
        return;
    }
    const auto module_id = view.modules().front().module_id;
    const auto origin = draft.append_source_origin(draft.module_source(module_id), Span::at(0u));
    auto context = ExecutionContext(draft, construction.construction_requests(), module_id);
    const auto evaluate = [&](std::string_view name,
                              ExecutionLimits limits = static_execution_limits()) noexcept {
        const auto found = std::ranges::find(view.symbols(), name, &CatalogSymbol::name);
        ct::require(found != view.symbols().end());
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
                .target = std::nullopt,
                .arguments = {},
                .callee_failures = BodyFailures(draft.add_empty_failure_term())
            }
        );
        context.output.clear();
        context.calls.clear();
        context.reports.clear();
        return execute_static_root(draft, context, expression, limits).run();
    };
    action(draft, context, evaluate);
}

auto check_limit(const ExecutionContext& context, std::string_view resource) noexcept -> void {
    ct::expect(context.reports.size() == 1uz);
    if (context.reports.size() != 1uz) {
        return;
    }
    ct::expect(context.reports.front().reason() == ExecutionReason::Limit);
    ct::expect(context.reports.front().message().contains(resource));
}

auto check_integer(
    const ConstantValueReader& values,
    const ExecutionValue& result,
    std::int64_t expected
) noexcept -> void {
    const auto atom = execution_atom(values, result);
    if (!ct::expect(atom.has_value())) {
        return;
    }
    ct::expect(std::get<IntegerConstant>(atom->value) == IntegerConstant::from_signed(expected));
}

} // namespace

namespace {

const ct::Suite tests([] static noexcept {
    ct::test(
        "Semantic execution: checks complete and halts own the synchronously reported cause",
        [] static noexcept {
            struct Scenario final {
                std::string_view operation;
                ExecutionTermination termination;
                ExecutionReason reason;
                std::string_view message;
            };
            const auto scenarios = std::array {
                Scenario {
                    .operation = "check",
                    .termination = ExecutionTermination::Continue,
                    .reason = ExecutionReason::Test,
                    .message = "check failed",
                },
                Scenario {
                    .operation = "require",
                    .termination = ExecutionTermination::StopRoot,
                    .reason = ExecutionReason::Test,
                    .message = "requirement failed",
                },
                Scenario {
                    .operation = "fail",
                    .termination = ExecutionTermination::StopRoot,
                    .reason = ExecutionReason::Test,
                    .message = "explicit failure",
                },
                Scenario {
                    .operation = "assert",
                    .termination = ExecutionTermination::Abort,
                    .reason = ExecutionReason::Assertion,
                    .message = "assertion failed",
                },
            };
            ct::each(scenarios, &Scenario::operation, [](const auto& scenario) static noexcept {
                const auto program = analyze_test_program(
                    std::format(
                        "test {{ {}({}\"owned\"); println(\"after\"); }}",
                        scenario.operation,
                        scenario.operation == "fail" ? "" : "false, "
                    )
                );
                auto context = BodyExecutionContext(program);
                auto values = PublishedConstantValues(program);
                auto tests = program.tests().entries();
                if (!ct::expect(!std::ranges::empty(tests))) {
                    return;
                }
                const auto test = *tests.begin();
                if (!ct::expect(test.value.body.has_value())) {
                    return;
                }
                auto result = execute_body(
                                  values,
                                  context,
                                  ExecutionBody(program.bodies().body(*test.value.body))
                )
                                  .run();
                if (!ct::expect(context.reports.size() == 1uz)) {
                    return;
                }
                const auto& event = context.reports.front();
                ct::expect(event.reason() == scenario.reason);
                ct::expect(event.termination() == scenario.termination);
                ct::expect(event.message() == scenario.message);
                ct::expect(execution_message(event).contains("message: owned"));
                if (scenario.termination == ExecutionTermination::Continue) {
                    ct::expect(result.has_value());
                    ct::expect(context.output == "after\n");
                    return;
                }
                if (!ct::expect(!result.has_value())) {
                    return;
                }
                const auto* halt = std::get_if<ExecutionHalt>(&result.error());
                if (!ct::expect(halt != nullptr)) {
                    return;
                }
                ct::expect(halt->event.origin == event.origin);
                ct::expect(halt->event.reason() == event.reason());
                ct::expect(halt->event.termination() == event.termination());
                ct::expect(context.output.empty());
                context.reports.clear();
                ct::expect(halt->event.termination() == scenario.termination);
                ct::expect(halt->event.message() == scenario.message);
                ct::expect(execution_message(halt->event).contains("message: owned"));
            });
        }
    );

    ct::test(
        "Semantic execution: report text budget covers empty and multiline fields exactly",
        [] static noexcept {
            struct Scenario final {
                std::string_view name;
                std::string_view message;
                std::string_view rendered;
            };
            const auto scenarios = std::array {
                Scenario {
                    .name = "empty",
                    .message = "",
                    .rendered = "check failed\n  condition: false\n  message: \"\"",
                },
                Scenario {
                    .name = "multiline",
                    .message = "first\\nsecond",
                    .rendered =
                        "check failed\n  condition: false\n  message:\n    first\n    second",
                },
            };
            ct::each(scenarios, &Scenario::name, [](const auto& scenario) static noexcept {
                const auto program = analyze_test_program(
                    std::format("test {{ check(false, \"{}\"); }}", scenario.message)
                );
                auto context = BodyExecutionContext(program);
                auto values = PublishedConstantValues(program);
                auto tests = program.tests().entries();
                if (!ct::expect(!std::ranges::empty(tests))) {
                    return;
                }
                const auto test = *tests.begin();
                if (!ct::expect(test.value.body.has_value())) {
                    return;
                }
                const auto body = ExecutionBody(program.bodies().body(*test.value.body));
                auto limits = static_execution_limits();
                limits.text_work = scenario.rendered.size();
                const auto completed = execute_body(values, context, body, limits).run();
                ct::expect(completed.has_value());
                if (!ct::expect(context.reports.size() == 1uz)) {
                    return;
                }
                ct::expect(context.reports.front().termination() == ExecutionTermination::Continue);
                ct::expect(execution_message(context.reports.front()) == scenario.rendered);
                context.reports.clear();
                --limits.text_work;
                auto limited = execute_body(values, context, body, limits).run();
                if (!ct::expect(!limited.has_value())) {
                    return;
                }
                const auto* halt = std::get_if<ExecutionHalt>(&limited.error());
                if (!ct::expect(halt != nullptr)) {
                    return;
                }
                ct::expect(halt->event.reason() == ExecutionReason::Limit);
                ct::expect(halt->event.termination() == ExecutionTermination::StopRoot);
                if (!ct::expect(context.reports.size() == 1uz)) {
                    return;
                }
                ct::expect(context.reports.front().reason() == halt->event.reason());
            });
        }
    );

    ct::test(
        "Static execution: logical operators preserve results and their calls",
        [] static noexcept {
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
            ct::each(scenarios, &Scenario::expression, [&](const auto& scenario) noexcept {
                with_execution(
                    std::format(
                        "const fn right() -> bool => {}; const fn run() -> bool => {};",
                        scenario.right,
                        scenario.expression
                    ),
                    [&](ProgramDraft& draft,
                        ExecutionContext& context,
                        const auto& evaluate) noexcept {
                        const auto result = evaluate("run");
                        if (!(ct::expect(result.has_value())
                                  .note("scenario.right = ", scenario.right))) {
                            return;
                        }
                        const auto atom = execution_atom(draft, *result);
                        if (!(ct::expect(atom.has_value())
                                  .note("scenario.right = ", scenario.right))) {
                            return;
                        }
                        ct::expect(std::get<BooleanConstant>(atom->value).value == scenario.result)
                            .note("scenario.right = ", scenario.right);
                        ct::expect(context.calls.size() == scenario.calls)
                            .note("scenario.right = ", scenario.right);
                        ct::expect(context.reports.empty())
                            .note("scenario.right = ", scenario.right);
                    }
                );
            });
        }
    );

    ct::test(
        "Static execution: aggregate work counts constructed and copied slots",
        [] static noexcept {
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
                Scenario {
                    "const data = [[1, 2], [3, 4]]; const fn run() -> i32 => data[1][0];",
                    0uz,
                    3
                },
            };
            ct::each(scenarios, &Scenario::source, [&](const auto& scenario) noexcept {
                with_execution(
                    std::string(scenario.source),
                    [&](ProgramDraft& draft,
                        ExecutionContext& context,
                        const auto& evaluate) noexcept {
                        if (scenario.work != 0uz) {
                            ct::expect(!(evaluate(
                                             "run",
                                             {.steps = maximum_constant_steps,
                                              .text_work = 8uz * maximum_constant_text_bytes,
                                              .aggregate_work = scenario.work - 1uz}
                            )
                                             .has_value()));
                            check_limit(context, "aggregate");
                        }
                        const auto result = evaluate(
                            "run",
                            {.steps = maximum_constant_steps,
                             .text_work = 8uz * maximum_constant_text_bytes,
                             .aggregate_work = scenario.work}
                        );
                        if (!(ct::expect(result.has_value()))) {
                            return;
                        }
                        check_integer(draft, *result, scenario.result);
                        ct::expect(context.reports.empty());
                    }
                );
            });
        }
    );

    ct::test(
        "Static execution: calls share work within a root and new roots start independently",
        [] static noexcept {
            with_execution(
                R"(
        const fn leaf() -> [i32; 2] => [1, 2];
        const fn run() -> i32 {
            let first = leaf(); let second = leaf(); return first[0] + second[0];
        }
    )",
                [](ProgramDraft& draft,
                   ExecutionContext& context,
                   const auto& evaluate) static noexcept {
                    for (auto root = 0uz; root < 2uz; ++root) {
                        if (!ct::expect(evaluate(
                                            "leaf",
                                            {.steps = maximum_constant_steps,
                                             .text_work = 8uz * maximum_constant_text_bytes,
                                             .aggregate_work = 2uz}
                            )
                                            .has_value())) {
                            return;
                        }
                        ct::expect(context.reports.empty());
                    }
                    ct::expect(!(evaluate(
                                     "run",
                                     {.steps = maximum_constant_steps,
                                      .text_work = 8uz * maximum_constant_text_bytes,
                                      .aggregate_work = 3uz}
                    )
                                     .has_value()));
                    check_limit(context, "aggregate");
                    const auto result = evaluate(
                        "run",
                        {.steps = maximum_constant_steps,
                         .text_work = 8uz * maximum_constant_text_bytes,
                         .aggregate_work = 4uz}
                    );
                    if (!ct::expect(result.has_value())) {
                        return;
                    }
                    check_integer(draft, *result, 2);
                }
            );
        }
    );

    ct::test(
        "Static execution: step limits include nested calls and recursive equality",
        [] static noexcept {
            with_execution(
                R"(
        const data = [[1, 2], [3, 4]];
        const fn leaf() -> i32 => 1;
        const fn run() -> i32 => leaf() + leaf();
        const fn equal() -> bool => data == data;
    )",
                [](ProgramDraft&, ExecutionContext& context, const auto& evaluate) static noexcept {
                    ct::expect(!(evaluate(
                                     "leaf",
                                     {.steps = 0uz,
                                      .text_work = 8uz * maximum_constant_text_bytes,
                                      .aggregate_work = maximum_constant_aggregate_work}
                    )
                                     .has_value()));
                    check_limit(context, "steps");
                    ct::expect(!(evaluate(
                                     "leaf",
                                     {.steps = 5uz,
                                      .text_work = 8uz * maximum_constant_text_bytes,
                                      .aggregate_work = maximum_constant_aggregate_work}
                    )
                                     .has_value()));
                    check_limit(context, "steps");
                    // Call, callee value, invocation, body region, return, and literal each use one step.
                    for (auto root = 0uz; root < 2uz; ++root) {
                        ct::expect(evaluate(
                                       "leaf",
                                       {.steps = 6uz,
                                        .text_work = 8uz * maximum_constant_text_bytes,
                                        .aggregate_work = maximum_constant_aggregate_work}
                        )
                                       .has_value());
                    }
                    ct::expect(!(evaluate(
                                     "run",
                                     {.steps = 17uz,
                                      .text_work = 8uz * maximum_constant_text_bytes,
                                      .aggregate_work = maximum_constant_aggregate_work}
                    )
                                     .has_value()));
                    check_limit(context, "steps");
                    ct::expect(evaluate(
                                   "run",
                                   {.steps = 18uz,
                                    .text_work = 8uz * maximum_constant_text_bytes,
                                    .aggregate_work = maximum_constant_aggregate_work}
                    )
                                   .has_value());
                    ct::expect(!(evaluate(
                                     "equal",
                                     {.steps = 14uz,
                                      .text_work = 8uz * maximum_constant_text_bytes,
                                      .aggregate_work = maximum_constant_aggregate_work}
                    )
                                     .has_value()));
                    check_limit(context, "comparison");
                    ct::expect(evaluate(
                                   "equal",
                                   {.steps = 15uz,
                                    .text_work = 8uz * maximum_constant_text_bytes,
                                    .aggregate_work = maximum_constant_aggregate_work}
                    )
                                   .has_value());
                }
            );
        }
    );

    ct::test(
        "Static execution: text work counts produced bytes across copies append and clear",
        [] static noexcept {
            struct Scenario final {
                std::string_view body;
                std::size_t work;
                std::string_view result;
            };

            const auto scenarios = std::array {
                Scenario {
                    R"(var text: String = "ab"; text.append("c"); return &&text;)",
                    3uz,
                    "abc"
                },
                Scenario {
                    R"(var text: String = "ab"; text.push('我'); return &&text;)",
                    5uz,
                    "ab我"
                },
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
            ct::each(scenarios, &Scenario::body, [&](const auto& scenario) noexcept {
                with_execution(
                    std::format("const fn run() -> String {{ {} }}", scenario.body),
                    [&](ProgramDraft& draft,
                        ExecutionContext& context,
                        const auto& evaluate) noexcept {
                        ct::expect(!(evaluate(
                                         "run",
                                         {.steps = maximum_constant_steps,
                                          .text_work = scenario.work - 1uz,
                                          .aggregate_work = maximum_constant_aggregate_work}
                        )
                                         .has_value()));
                        check_limit(context, "text");
                        const auto result = evaluate(
                            "run",
                            {.steps = maximum_constant_steps,
                             .text_work = scenario.work,
                             .aggregate_work = maximum_constant_aggregate_work}
                        );
                        if (!(ct::expect(result.has_value()))) {
                            return;
                        }
                        ct::expect(execution_text(draft, *result) == scenario.result);
                        ct::expect(context.reports.empty());
                    }
                );
            });
        }
    );

    ct::test("Static execution: print output consumes the text-work budget", [] static noexcept {
        with_execution(
            R"(const fn run() { println("ab", 3); })",
            [](ProgramDraft&, ExecutionContext& context, const auto& evaluate) static noexcept {
                ct::expect(!(evaluate(
                                 "run",
                                 {.steps = maximum_constant_steps,
                                  .text_work = 4uz,
                                  .aggregate_work = maximum_constant_aggregate_work}
                )
                                 .has_value()));
                check_limit(context, "text");
                ct::expect(context.output == "ab 3");
                ct::expect(evaluate(
                               "run",
                               {.steps = maximum_constant_steps,
                                .text_work = 5uz,
                                .aggregate_work = maximum_constant_aggregate_work}
                )
                               .has_value());
                ct::expect(context.output == "ab 3\n");
            }
        );
    });

    ct::test("Static execution: slice elements retain their array addresses", [] static noexcept {
        with_execution(
            R"(
            fn direct() -> bool {
                var values = [10, 20, 30];
                let view = values.as_slice();
                return addressof(view[0]) == addressof(values[0]);
            }
            fn copied() -> bool {
                var values = [10, 20, 30];
                let original = values.as_slice();
                let copy = original;
                return addressof(copy[1]) == addressof(values[1]);
            }
            fn nested() -> bool {
                var values = [10, 20, 30];
                let view = values.as_slice().slice(1usize, 3usize).slice(1usize, 2usize);
                return addressof(view[0]) == addressof(values[2]);
            }
        )",
            [](ProgramDraft& draft,
               ExecutionContext& context,
               const auto& evaluate) static noexcept {
                for (const auto name : {"direct", "copied", "nested"}) {
                    const auto result = evaluate(name);
                    if (!(ct::expect(result.has_value()).note("name = ", name))) {
                        return;
                    }
                    const auto atom = execution_atom(draft, *result);
                    if (!(ct::expect(atom.has_value()).note("name = ", name))) {
                        return;
                    }
                    ct::expect(std::get<BooleanConstant>(atom->value).value).note("name = ", name);
                    ct::expect(context.reports.empty()).note("name = ", name);
                }
            }
        );
    });

    ct::test("Static execution: slice display borrows its elements", [] static noexcept {
        with_execution(
            R"(const fn run() -> bool {
            let values = [1, 2];
            let view = values.as_slice();
            println(view);
            return view[0] == 1;
        })",
            [](ProgramDraft& draft,
               ExecutionContext& context,
               const auto& evaluate) static noexcept {
                const auto result = evaluate(
                    "run",
                    {.steps = maximum_constant_steps,
                     .text_work = maximum_constant_text_bytes,
                     .aggregate_work = 2uz}
                );
                if (!ct::expect(result.has_value())) {
                    return;
                }
                const auto atom = execution_atom(draft, *result);
                if (!ct::expect(atom.has_value())) {
                    return;
                }
                ct::expect(std::get<BooleanConstant>(atom->value).value);
                ct::expect(context.output.contains('1'));
                ct::expect(context.output.contains('2'));
                ct::expect(context.reports.empty());
            }
        );
    });

    ct::test(
        "Static execution: text views query and project without constructing their contents",
        [] static noexcept {
            with_execution(
                R"(
            const fn owned() -> bool {
                let text = String::from_str("abc");
                let view = text.as_str();
                let bytes = view.bytes.slice(1usize, 3usize);
                return view == "abc" && bytes.len() == 2usize && bytes[0] == 98u8
                    && addressof(bytes[0]) == addressof(text.bytes[1]);
            }
            const fn retained() -> bool {
                let text = "abc";
                let bytes = text.bytes;
                let copy = bytes;
                return bytes.len() == 3usize && bytes[1] == 98u8
                    && addressof(bytes[1]) == addressof(copy[1]);
            }
            const fn frozen() -> [u8] => "ab".bytes;
        )",
                [](ProgramDraft& draft,
                   ExecutionContext& context,
                   const auto& evaluate) static noexcept {
                    for (const auto& [name, text_work] :
                         std::array {std::pair {"owned", 3uz}, std::pair {"retained", 0uz}}) {
                        const auto result = evaluate(
                            name,
                            {.steps = maximum_constant_steps,
                             .text_work = text_work,
                             .aggregate_work = 0uz}
                        );
                        if (!(ct::expect(result.has_value()).note("name = ", name))) {
                            return;
                        }
                        const auto atom = execution_atom(draft, *result);
                        if (!(ct::expect(atom.has_value()).note("name = ", name))) {
                            return;
                        }
                        ct::expect(std::get<BooleanConstant>(atom->value).value)
                            .note("name = ", name);
                        ct::expect(context.reports.empty()).note("name = ", name);
                    }
                    ct::expect(!(evaluate(
                                     "owned",
                                     {.steps = maximum_constant_steps,
                                      .text_work = 2uz,
                                      .aggregate_work = 0uz}
                    )
                                     .has_value()));
                    check_limit(context, "text");
                    ct::expect(!(evaluate(
                                     "frozen",
                                     {.steps = maximum_constant_steps,
                                      .text_work = 0uz,
                                      .aggregate_work = 1uz}
                    )
                                     .has_value()));
                    check_limit(context, "aggregate");
                    const auto frozen = evaluate(
                        "frozen",
                        {.steps = maximum_constant_steps, .text_work = 0uz, .aggregate_work = 2uz}
                    );
                    if (!ct::expect(frozen.has_value())) {
                        return;
                    }
                    const auto children = execution_compound_view(draft, *frozen);
                    if (!ct::expect(children.has_value())) {
                        return;
                    }
                    ct::expect(children->size() == 2uz);
                    ct::expect(context.reports.empty());
                }
            );
        }
    );
});

} // namespace
