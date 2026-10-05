module carven:test.internal.semantic.evaluation.execution;

import :semantic.analysis.body.builder;
import :semantic.analysis.catalog;
import :semantic.analysis.construction;
import :semantic.analysis.stage.session;
import :semantic.evaluation.display;
import :semantic.evaluation.execution;
import :semantic.evaluation.value;
import :semantic.semir.constant_access;
import :semantic.semir.decl;
import :semantic.semir.program;
import :source.batch;
import :source.text;
import :support.task;
import :test.harness.framework;
import :test.internal.semantic.analysis.fixture;
import :test.internal.semantic.evaluation.fixture;
import std;

namespace {

auto task_failure_type(ProgramDraft& draft, ProgramOriginID origin) noexcept -> TypeID {
    const auto module = draft.reserve_module_declaration();
    const auto structure = draft.reserve_struct_declaration();
    draft.define_declaration(
        module,
        ModuleDeclaration {
            .provenance_module = draft.provenance_module_at(0uz),
            .origin = origin,
            .cpp_headers = {},
            .cpp_source_fragments = {},
            .items = {structure},
        }
    );
    draft.define_declaration(
        structure,
        ConstructionStructDeclaration {
            .kind = RecordKind::Struct,
            .module_id = module,
            .name = draft.intern_spelling("TaskFailure"),
            .origin = origin,
            .visibility = DeclarationVisibility::Module,
            .fields = {{
                .name = draft.intern_spelling("message"),
                .type = draft.builtin_type(BuiltinType::String),
                .origin = origin,
            }},
        }
    );
    draft.finish_declaration_heads();
    return draft.intern_type({.value = StructTypeValue {.structure = structure}});
}

auto owned_text_task(ExecutionText& borrowed) noexcept -> ExecutionTask<ExecutionValue> {
    auto owner = ExecutionOwnedText("transport");
    borrowed = owner.borrow();
    co_return owner;
}

auto source_failure_task(ExecutionSourceFailure failure) noexcept -> ExecutionTask<ExecutionValue> {
    co_return std::unexpected(ExecutionFailure(std::move(failure)));
}

auto forward_value_task(ExecutionTask<ExecutionValue> child) noexcept
    -> ExecutionTask<ExecutionValue> {
    auto result = co_await std::move(child);
    co_return result;
}

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
    require(body.has_value());
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
    require(body.has_value());
    require(draft.body_draft(*body).inputs.parameters.empty());
    const auto realized = co_await requests.stage().realize_body(*body);
    require(realized.has_value());
    co_return ExecutionBody(draft.body_draft(*body));
}

auto ExecutionContext::report(const ExecutionEvent& event) noexcept -> void {
    reports.push_back(event);
}

template<typename Action>
auto with_execution(std::string source_text, Action action) noexcept -> void {
    auto sources = SourceManager();
    const auto source = sources.append_virtual("execution.cv", std::move(source_text));
    if (!expect(source.has_value())) {
        return;
    }
    const auto inputs = std::array {SourceModuleInput {
        .source_id = *source,
        .module_path = constant_test_module_path("execution")
    }};
    auto syntax = parse_program(sources, SourceBatch {.modules = inputs});
    if (!expect(syntax.has_value())) {
        return;
    }
    auto diagnostics = DiagnosticSink();
    auto draft = ProgramDraft::begin(std::move(*syntax), diagnostics);
    auto catalog = build_analysis_catalog(draft);
    if (!expect(catalog.has_value())) {
        return;
    }
    const auto view = catalog->view();
    auto usage = ImportUsage(view.imports().size());
    auto construction = ProgramConstruction(draft, view, usage);
    if (!expect(construction.run().has_value())) {
        return;
    }
    const auto module_id = view.modules().front().module_id;
    const auto origin = draft.append_source_origin(draft.module_source(module_id), Span::at(0u));
    auto context = ExecutionContext(draft, construction.construction_requests(), module_id);
    const auto evaluate = [&](std::string_view name,
                              ExecutionLimits limits = static_execution_limits()) noexcept {
        const auto found = std::ranges::find(view.symbols(), name, &CatalogSymbol::name);
        require(found != view.symbols().end());
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
    expect(context.reports.size() == 1uz);
    if (context.reports.size() != 1uz) {
        return;
    }
    expect(context.reports.front().reason() == ExecutionReason::Limit);
    expect(context.reports.front().message().contains(resource));
}

auto check_integer(
    const ConstantValueReader& values,
    const ExecutionValue& result,
    std::int64_t expected
) noexcept -> void {
    const auto atom = execution_atom(values, result);
    if (!expect(atom.has_value())) {
        return;
    }
    expect(std::get<IntegerConstant>(atom->value) == IntegerConstant::from_signed(expected));
}

const TestSuite suite([] static noexcept {
    "Semantic execution: unsupported operations preserve operand storage and copy costs"_test =
        [] static noexcept {
            struct Scenario final {
                std::string_view name;
                std::string_view source;
                std::size_t text_work;
                std::size_t aggregate_work;
                ExecutionReason reason;
            };
            const auto scenarios = std::array {
                Scenario {
                    .name = "text Read borrows String storage",
                    .source = R"(test { let text: String = "ab"; text.chars; })",
                    .text_work = 2uz,
                    .aggregate_work = 0uz,
                    .reason = ExecutionReason::Admission,
                },
                Scenario {
                    .name = "native Read borrows aggregate storage",
                    .source = R"(struct Payload { number: i32 }
test { let owner = Payload { number: 1 }; ::native(owner); })",
                    .text_work = 0uz,
                    .aggregate_work = 1uz,
                    .reason = ExecutionReason::Admission,
                },
                Scenario {
                    .name = "native Write selects String storage",
                    .source = R"(test { var text: String = "ab"; ::native(&text); })",
                    .text_work = 2uz,
                    .aggregate_work = 0uz,
                    .reason = ExecutionReason::Admission,
                },
                Scenario {
                    .name = "native Take transfers String storage",
                    .source = R"(test { let text: String = "ab"; ::native(&&text); })",
                    .text_work = 2uz,
                    .aggregate_work = 0uz,
                    .reason = ExecutionReason::Admission,
                },
                Scenario {
                    .name = "Write capture selects String storage",
                    .source = R"(test { var text: String = "ab"; let closure = [&text]() {}; })",
                    .text_work = 2uz,
                    .aggregate_work = 0uz,
                    .reason = ExecutionReason::Admission,
                },
                Scenario {
                    .name = "value capture exhausts its copy budget",
                    .source = R"(test { let text: String = "ab"; let closure = [text]() {}; })",
                    .text_work = 3uz,
                    .aggregate_work = 0uz,
                    .reason = ExecutionReason::Limit,
                },
                Scenario {
                    .name = "value capture pays for its String copy",
                    .source = R"(test { let text: String = "ab"; let closure = [text]() {}; })",
                    .text_work = 4uz,
                    .aggregate_work = 0uz,
                    .reason = ExecutionReason::Admission,
                },
            };
            each(scenarios, &Scenario::name, [](const Scenario& scenario) static noexcept {
                const auto program = analyze_test_program(std::string(scenario.source));
                auto context = BodyExecutionContext(program);
                const auto values = PublishedConstantValues(program);
                const auto tests = program.tests().entries();
                if (!expect(!std::ranges::empty(tests))) {
                    return;
                }
                const auto test = *tests.begin();
                if (!expect(test.value.body.has_value())) {
                    return;
                }
                const auto result = execute_body(
                                        values,
                                        context,
                                        ExecutionBody(program.bodies().body(*test.value.body)),
                                        {.steps = maximum_constant_steps,
                                         .text_work = scenario.text_work,
                                         .aggregate_work = scenario.aggregate_work}
                )
                                        .run();
                if (!expect(!result.has_value()) || !expect_equal(context.reports.size(), 1uz)) {
                    return;
                }
                const auto& event = context.reports.front();
                expect(event.reason() == scenario.reason);
                const auto* halt = std::get_if<ExecutionHalt>(&result.error());
                if (!expect(halt != nullptr)) {
                    return;
                }
                expect(halt->event.reason() == scenario.reason);
                expect(halt->event.origin == event.origin);
                expect(context.output.empty());
            });
        };

    "Semantic execution: operand failure precedes native admission and later operands"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(test {
    var zero = 0;
    var negative = -1;
    ::native(
        if true { println("first"); 10 / zero } else { 0 },
        if true { println("second"); 1 << negative } else { 0 }
    );
})");
            auto context = BodyExecutionContext(program);
            const auto values = PublishedConstantValues(program);
            const auto tests = program.tests().entries();
            if (!expect(!std::ranges::empty(tests))) {
                return;
            }
            const auto test = *tests.begin();
            if (!expect(test.value.body.has_value())) {
                return;
            }
            const auto result = execute_body(
                                    values,
                                    context,
                                    ExecutionBody(program.bodies().body(*test.value.body))
            )
                                    .run();
            if (!expect(!result.has_value()) || !expect_equal(context.reports.size(), 1uz)) {
                return;
            }
            expect(context.reports.front().reason() == ExecutionReason::DivideByZero);
            expect_equal(context.output, std::string("first\n"));
        };

    "Execution tasks: String owners survive child frames and expire with the result"_test =
        [] static noexcept {
            auto borrowed = ExecutionText(std::string());
            {
                auto result =
                    forward_value_task(forward_value_task(owned_text_task(borrowed))).run();
                if (!expect(result.has_value())) {
                    return;
                }
                const auto* owner = std::get_if<ExecutionOwnedText>(&*result);
                if (!expect(owner != nullptr)) {
                    return;
                }
                expect_equal(owner->bytes(), std::string_view("transport"));
                const auto observed = borrowed.bytes();
                if (!expect(observed.has_value())) {
                    return;
                }
                expect_equal(*observed, std::string_view("transport"));
            }
            expect_equal(borrowed.bytes().has_value(), false);
        };

    "Execution tasks: source failures retain their immutable payload through forwarding"_test =
        [] static noexcept {
            auto fixture = ConstantEvaluationFixture();
            auto& draft = fixture.compilation;
            const auto module_id = draft.provenance_module_at(0uz);
            const auto origin =
                draft.append_source_origin(draft.module_source(module_id), Span::at(0u));
            const auto call =
                draft.append_source_origin(draft.module_source(module_id), Span::at(0u));
            const auto type = task_failure_type(draft, origin);
            auto owner = ExecutionOwnedText("failure");
            const auto borrowed = owner.borrow();
            auto fields = std::vector<ExecutionValue>();
            fields.emplace_back(std::move(owner));
            auto payload = std::make_shared<const ExecutionValue>(ExecutionAggregateValue {
                .type = type,
                .elements = std::move(fields),
            });
            const auto* identity = payload.get();
            const auto lifetime = std::weak_ptr<const ExecutionValue>(payload);
            {
                auto failure = ExecutionSourceFailure {
                    .type = type,
                    .payload = std::move(payload),
                    .origin = origin,
                    .calls = {call},
                };
                auto result =
                    forward_value_task(forward_value_task(source_failure_task(std::move(failure))))
                        .run();
                if (!expect_equal(result.has_value(), false)) {
                    return;
                }
                const auto* source = std::get_if<ExecutionSourceFailure>(&result.error());
                if (!expect(source != nullptr)) {
                    return;
                }
                expect(source->type == type);
                expect(source->origin == origin);
                expect_equal(source->payload.get(), identity);
                if (!expect_equal(source->calls.size(), 1uz)) {
                    return;
                }
                expect(source->calls.front() == call);
                expect_equal(lifetime.expired(), false);
                const auto observed = borrowed.bytes();
                if (!expect(observed.has_value())) {
                    return;
                }
                expect_equal(*observed, std::string_view("failure"));
            }
            expect_equal(lifetime.expired(), true);
            expect_equal(borrowed.bytes().has_value(), false);
        };

    "Semantic execution: checks complete and halts own the synchronously reported cause"_test =
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
            each(scenarios, &Scenario::operation, [](const auto& scenario) static noexcept {
                const auto program = analyze_test_program(
                    std::format(
                        "test {{ {}({}\"owned\"); println(\"after\"); }}",
                        scenario.operation,
                        scenario.operation == "fail" ? "" : "false, "
                    )
                );
                auto context = BodyExecutionContext(program);
                const auto values = PublishedConstantValues(program);
                const auto tests = program.tests().entries();
                if (!expect(!std::ranges::empty(tests))) {
                    return;
                }
                const auto test = *tests.begin();
                if (!expect(test.value.body.has_value())) {
                    return;
                }
                auto result = execute_body(
                                  values,
                                  context,
                                  ExecutionBody(program.bodies().body(*test.value.body))
                )
                                  .run();
                if (!expect(context.reports.size() == 1uz)) {
                    return;
                }
                const auto& event = context.reports.front();
                expect(event.reason() == scenario.reason);
                expect(event.termination() == scenario.termination);
                expect(event.message() == scenario.message);
                expect(execution_message(event).contains("message: owned"));
                if (scenario.termination == ExecutionTermination::Continue) {
                    expect(result.has_value());
                    expect(context.output == "after\n");
                    return;
                }
                if (!expect(!result.has_value())) {
                    return;
                }
                const auto* halt = std::get_if<ExecutionHalt>(&result.error());
                if (!expect(halt != nullptr)) {
                    return;
                }
                expect(halt->event.origin == event.origin);
                expect(halt->event.reason() == event.reason());
                expect(halt->event.termination() == event.termination());
                expect(context.output.empty());
                context.reports.clear();
                expect(halt->event.termination() == scenario.termination);
                expect(halt->event.message() == scenario.message);
                expect(execution_message(halt->event).contains("message: owned"));
            });
        };

    "Semantic execution: report text budget covers empty and multiline fields exactly"_test =
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
            each(scenarios, &Scenario::name, [](const auto& scenario) static noexcept {
                const auto program = analyze_test_program(
                    std::format("test {{ check(false, \"{}\"); }}", scenario.message)
                );
                auto context = BodyExecutionContext(program);
                const auto values = PublishedConstantValues(program);
                const auto tests = program.tests().entries();
                if (!expect(!std::ranges::empty(tests))) {
                    return;
                }
                const auto test = *tests.begin();
                if (!expect(test.value.body.has_value())) {
                    return;
                }
                const auto body = ExecutionBody(program.bodies().body(*test.value.body));
                auto limits = static_execution_limits();
                limits.text_work = scenario.rendered.size();
                const auto completed = execute_body(values, context, body, limits).run();
                expect(completed.has_value());
                if (!expect(context.reports.size() == 1uz)) {
                    return;
                }
                expect(context.reports.front().termination() == ExecutionTermination::Continue);
                expect(execution_message(context.reports.front()) == scenario.rendered);
                context.reports.clear();
                --limits.text_work;
                auto limited = execute_body(values, context, body, limits).run();
                if (!expect(!limited.has_value())) {
                    return;
                }
                const auto* halt = std::get_if<ExecutionHalt>(&limited.error());
                if (!expect(halt != nullptr)) {
                    return;
                }
                expect(halt->event.reason() == ExecutionReason::Limit);
                expect(halt->event.termination() == ExecutionTermination::StopRoot);
                if (!expect(context.reports.size() == 1uz)) {
                    return;
                }
                expect(context.reports.front().reason() == halt->event.reason());
            });
        };

    "Static execution: logical operators preserve results and their calls"_test =
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
            each(scenarios, &Scenario::expression, [&](const auto& scenario) noexcept {
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
                        if (!(
                                expect(result.has_value()).note("scenario.right = ", scenario.right)
                            )) {
                            return;
                        }
                        const auto atom = execution_atom(draft, *result);
                        if (!(expect(atom.has_value()).note("scenario.right = ", scenario.right))) {
                            return;
                        }
                        expect(std::get<BooleanConstant>(atom->value).value == scenario.result)
                            .note("scenario.right = ", scenario.right);
                        expect(context.calls.size() == scenario.calls)
                            .note("scenario.right = ", scenario.right);
                        expect(context.reports.empty()).note("scenario.right = ", scenario.right);
                    }
                );
            });
        };

    "Static execution: aggregate work counts constructed and copied slots"_test =
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
            each(scenarios, &Scenario::source, [&](const auto& scenario) noexcept {
                with_execution(
                    std::string(scenario.source),
                    [&](ProgramDraft& draft,
                        ExecutionContext& context,
                        const auto& evaluate) noexcept {
                        if (scenario.work != 0uz) {
                            expect(!(evaluate(
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
                        if (!(expect(result.has_value()))) {
                            return;
                        }
                        check_integer(draft, *result, scenario.result);
                        expect(context.reports.empty());
                    }
                );
            });
        };

    "Static execution: calls share work within a root and new roots start independently"_test =
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
                        if (!expect(evaluate(
                                        "leaf",
                                        {.steps = maximum_constant_steps,
                                         .text_work = 8uz * maximum_constant_text_bytes,
                                         .aggregate_work = 2uz}
                            )
                                        .has_value())) {
                            return;
                        }
                        expect(context.reports.empty());
                    }
                    expect(!(evaluate(
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
                    if (!expect(result.has_value())) {
                        return;
                    }
                    check_integer(draft, *result, 2);
                }
            );
        };

    "Static execution: step limits include nested calls and recursive equality"_test =
        [] static noexcept {
            with_execution(
                R"(
        const data = [[1, 2], [3, 4]];
        const fn leaf() -> i32 => 1;
        const fn binding() -> i32 { let value = 1; return value; }
        const fn run() -> i32 => leaf() + leaf();
        const fn equal() -> bool => data == data;
    )",
                [](ProgramDraft& draft,
                   ExecutionContext& context,
                   const auto& evaluate) static noexcept {
                    expect(!(evaluate(
                                 "leaf",
                                 {.steps = 0uz,
                                  .text_work = 8uz * maximum_constant_text_bytes,
                                  .aggregate_work = maximum_constant_aggregate_work}
                    )
                                 .has_value()));
                    check_limit(context, "steps");
                    expect(!(evaluate(
                                 "leaf",
                                 {.steps = 5uz,
                                  .text_work = 8uz * maximum_constant_text_bytes,
                                  .aggregate_work = maximum_constant_aggregate_work}
                    )
                                 .has_value()));
                    check_limit(context, "steps");
                    // Call, callee value, invocation, body region, return, and literal each use one step.
                    for (auto root = 0uz; root < 2uz; ++root) {
                        expect(evaluate(
                                   "leaf",
                                   {.steps = 6uz,
                                    .text_work = 8uz * maximum_constant_text_bytes,
                                    .aggregate_work = maximum_constant_aggregate_work}
                        )
                                   .has_value());
                    }
                    expect(!(evaluate(
                                 "binding",
                                 {.steps = 7uz,
                                  .text_work = 8uz * maximum_constant_text_bytes,
                                  .aggregate_work = maximum_constant_aggregate_work}
                    )
                                 .has_value()));
                    check_limit(context, "steps");
                    const auto binding = evaluate(
                        "binding",
                        {.steps = 8uz,
                         .text_work = 8uz * maximum_constant_text_bytes,
                         .aggregate_work = maximum_constant_aggregate_work}
                    );
                    if (!expect(binding.has_value())) {
                        return;
                    }
                    check_integer(draft, *binding, 1);
                    expect(context.reports.empty());
                    expect(!(evaluate(
                                 "run",
                                 {.steps = 17uz,
                                  .text_work = 8uz * maximum_constant_text_bytes,
                                  .aggregate_work = maximum_constant_aggregate_work}
                    )
                                 .has_value()));
                    check_limit(context, "steps");
                    expect(evaluate(
                               "run",
                               {.steps = 18uz,
                                .text_work = 8uz * maximum_constant_text_bytes,
                                .aggregate_work = maximum_constant_aggregate_work}
                    )
                               .has_value());
                    expect(!(evaluate(
                                 "equal",
                                 {.steps = 14uz,
                                  .text_work = 8uz * maximum_constant_text_bytes,
                                  .aggregate_work = maximum_constant_aggregate_work}
                    )
                                 .has_value()));
                    check_limit(context, "comparison");
                    expect(evaluate(
                               "equal",
                               {.steps = 15uz,
                                .text_work = 8uz * maximum_constant_text_bytes,
                                .aggregate_work = maximum_constant_aggregate_work}
                    )
                               .has_value());
                }
            );
        };

    "Static execution: text work counts produced bytes across copies append and clear"_test =
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
            each(scenarios, &Scenario::body, [&](const auto& scenario) noexcept {
                with_execution(
                    std::format("const fn run() -> String {{ {} }}", scenario.body),
                    [&](ProgramDraft& draft,
                        ExecutionContext& context,
                        const auto& evaluate) noexcept {
                        expect(!(evaluate(
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
                        if (!(expect(result.has_value()))) {
                            return;
                        }
                        expect(execution_text(draft, *result) == scenario.result);
                        expect(context.reports.empty());
                    }
                );
            });
        };

    "Static execution: print output consumes the text-work budget"_test = [] static noexcept {
        with_execution(
            R"(const fn run() { println("ab", 3); })",
            [](ProgramDraft&, ExecutionContext& context, const auto& evaluate) static noexcept {
                expect(!(evaluate(
                             "run",
                             {.steps = maximum_constant_steps,
                              .text_work = 4uz,
                              .aggregate_work = maximum_constant_aggregate_work}
                )
                             .has_value()));
                check_limit(context, "text");
                expect(context.output == "ab 3");
                expect(evaluate(
                           "run",
                           {.steps = maximum_constant_steps,
                            .text_work = 5uz,
                            .aggregate_work = maximum_constant_aggregate_work}
                )
                           .has_value());
                expect(context.output == "ab 3\n");
            }
        );
    };

    "Static execution: slice elements retain their array addresses"_test = [] static noexcept {
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
                    if (!(expect(result.has_value()).note("name = ", name))) {
                        return;
                    }
                    const auto atom = execution_atom(draft, *result);
                    if (!(expect(atom.has_value()).note("name = ", name))) {
                        return;
                    }
                    expect(std::get<BooleanConstant>(atom->value).value).note("name = ", name);
                    expect(context.reports.empty()).note("name = ", name);
                }
            }
        );
    };

    "Static execution: slice display borrows its elements"_test = [] static noexcept {
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
                if (!expect(result.has_value())) {
                    return;
                }
                const auto atom = execution_atom(draft, *result);
                if (!expect(atom.has_value())) {
                    return;
                }
                expect(std::get<BooleanConstant>(atom->value).value);
                expect(context.output.contains('1'));
                expect(context.output.contains('2'));
                expect(context.reports.empty());
            }
        );
    };

    "Static execution: text views query and project without constructing their contents"_test =
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
                        if (!(expect(result.has_value()).note("name = ", name))) {
                            return;
                        }
                        const auto atom = execution_atom(draft, *result);
                        if (!(expect(atom.has_value()).note("name = ", name))) {
                            return;
                        }
                        expect(std::get<BooleanConstant>(atom->value).value).note("name = ", name);
                        expect(context.reports.empty()).note("name = ", name);
                    }
                    expect(!(evaluate(
                                 "owned",
                                 {.steps = maximum_constant_steps,
                                  .text_work = 2uz,
                                  .aggregate_work = 0uz}
                    )
                                 .has_value()));
                    check_limit(context, "text");
                    expect(!(evaluate(
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
                    if (!expect(frozen.has_value())) {
                        return;
                    }
                    const auto children = execution_compound_view(draft, *frozen);
                    if (!expect(children.has_value())) {
                        return;
                    }
                    expect(children->size() == 2uz);
                    expect(context.reports.empty());
                }
            );
        };

    "Static execution: mixed arithmetic trees preserve exact step limits at increasing depths"_test =
        [] static noexcept {
            with_execution(
                "const fn unused() -> i32 => 0;",
                [](ProgramDraft& draft, ExecutionContext& context, const auto&) static noexcept {
                    const auto module_id = draft.provenance_module_at(0uz);
                    const auto source = draft.module_source(module_id);
                    const auto origin = [&]() noexcept {
                        return draft.append_source_origin(source, Span::at(0u));
                    };
                    const auto integer = draft.builtin_type(BuiltinType::I32);
                    for (const auto nodes : {3uz, 65uz, 1025uz}) {
                        auto builder = BodyBuilder(draft.reserve_body(BodyKind::Test), draft);
                        const auto lifetime = builder.add_lifetime_region(
                            std::nullopt,
                            LifetimeRegionKind::Lexical,
                            origin()
                        );
                        const auto last_origin = origin();
                        auto root = builder.make_expression(
                            integer,
                            lifetime,
                            origin(),
                            SemBinary {
                                .left = OwnedSemanticExpression(builder.make_expression(
                                    integer,
                                    lifetime,
                                    origin(),
                                    SemConstant {draft.intern_constant(
                                        constant_test_integer_fact(integer, 1)
                                    )}
                                )),
                                .operation = BinaryOperator::Add,
                                .right = OwnedSemanticExpression(builder.make_expression(
                                    integer,
                                    lifetime,
                                    last_origin,
                                    SemConstant {draft.intern_constant(
                                        constant_test_integer_fact(integer, 2)
                                    )}
                                )),
                            }
                        );
                        auto expected = 3;
                        for (auto node = 3uz; node < nodes; ++node) {
                            if (node % 2uz == 0uz) {
                                root = builder.make_expression(
                                    integer,
                                    lifetime,
                                    origin(),
                                    SemUnary {
                                        .operation = UnaryOperator::Negate,
                                        .operand = OwnedSemanticExpression(std::move(root)),
                                    }
                                );
                                expected = -expected;
                            } else {
                                root = builder.make_expression(
                                    integer,
                                    lifetime,
                                    origin(),
                                    SemCast {
                                        .operand = OwnedSemanticExpression(std::move(root)),
                                        .kind = CastKind::Identity,
                                    }
                                );
                            }
                        }
                        // Direct construction retains executable operations rather than folding them.
                        expect(!root.constant.has_value());
                        auto limits = static_execution_limits();
                        limits.steps = nodes - 1uz;
                        context.reports.clear();
                        const auto limited =
                            execute_static_root(draft, context, root, limits).run();
                        expect(!limited.has_value()).note("nodes = ", nodes);
                        check_limit(context, "steps");
                        if (!expect_equal(context.reports.size(), 1uz)) {
                            return;
                        }
                        expect(context.reports.front().origin == last_origin);
                        context.reports.clear();
                        limits.steps = nodes;
                        const auto completed =
                            execute_static_root(draft, context, root, limits).run();
                        if (!expect(completed.has_value()).note("nodes = ", nodes)) {
                            return;
                        }
                        check_integer(draft, *completed, expected);
                        expect(context.reports.empty());
                    }
                }
            );
        };

    "Static execution: arithmetic operands preserve first failure and its origin"_test =
        [] static noexcept {
            with_execution(
                "const fn unused() -> i32 => 0;",
                [](ProgramDraft& draft, ExecutionContext& context, const auto&) static noexcept {
                    const auto module_id = draft.provenance_module_at(0uz);
                    const auto source = draft.module_source(module_id);
                    const auto origin = [&]() noexcept {
                        return draft.append_source_origin(source, Span::at(0u));
                    };
                    const auto integer = draft.builtin_type(BuiltinType::I32);
                    for (const auto left_fails : {true, false}) {
                        auto builder = BodyBuilder(draft.reserve_body(BodyKind::Test), draft);
                        const auto lifetime = builder.add_lifetime_region(
                            std::nullopt,
                            LifetimeRegionKind::Lexical,
                            origin()
                        );
                        const auto left_origin = origin();
                        const auto right_origin = origin();
                        const auto left_last = origin();
                        const auto right_last = origin();
                        const auto literal = [&](std::int64_t value, ProgramOriginID at) noexcept {
                            return builder.make_expression(
                                integer,
                                lifetime,
                                at,
                                SemConstant {draft.intern_constant(
                                    constant_test_integer_fact(integer, value)
                                )}
                            );
                        };
                        const auto root = builder.make_expression(
                            integer,
                            lifetime,
                            origin(),
                            SemBinary {
                                .left = OwnedSemanticExpression(builder.make_expression(
                                    integer,
                                    lifetime,
                                    left_origin,
                                    SemBinary {
                                        .left = OwnedSemanticExpression(literal(6, origin())),
                                        .operation = BinaryOperator::Divide,
                                        .right = OwnedSemanticExpression(
                                            literal(left_fails ? 0 : 2, left_last)
                                        ),
                                    }
                                )),
                                .operation = BinaryOperator::Add,
                                .right = OwnedSemanticExpression(builder.make_expression(
                                    integer,
                                    lifetime,
                                    right_origin,
                                    SemBinary {
                                        .left = OwnedSemanticExpression(literal(1, origin())),
                                        .operation = BinaryOperator::LeftShift,
                                        .right = OwnedSemanticExpression(literal(-1, right_last)),
                                    }
                                )),
                            }
                        );
                        auto limits = static_execution_limits();
                        limits.steps = left_fails ? 3uz : 6uz;
                        context.reports.clear();
                        const auto limited =
                            execute_static_root(draft, context, root, limits).run();
                        expect(!limited.has_value());
                        check_limit(context, "steps");
                        if (!expect_equal(context.reports.size(), 1uz)) {
                            return;
                        }
                        expect(
                            context.reports.front().origin == (left_fails ? left_last : right_last)
                        );
                        ++limits.steps;
                        context.reports.clear();
                        const auto failed = execute_static_root(draft, context, root, limits).run();
                        expect(!failed.has_value());
                        if (!expect_equal(context.reports.size(), 1uz)) {
                            return;
                        }
                        const auto& report = context.reports.front();
                        expect(
                            report.reason()
                            == (left_fails ? ExecutionReason::DivideByZero
                                           : ExecutionReason::ShiftOutOfRange)
                        );
                        expect(report.origin == (left_fails ? left_origin : right_origin));
                    }
                }
            );
        };

    "Static execution: comparisons preserve operand observations and binding copy budgets"_test =
        [] static noexcept {
            const auto program = analyze_test_program(R"(
                test {
                    var left = 1;
                    var right = 2;
                    check(left == right);
                    check(left > right);
                }
            )");
            auto context = BodyExecutionContext(program);
            const auto values = PublishedConstantValues(program);
            const auto tests = program.tests().entries();
            require(!std::ranges::empty(tests));
            const auto test = *tests.begin();
            require(test.value.body.has_value());
            const auto body = ExecutionBody(program.bodies().body(*test.value.body));
            auto comparisons = 0uz;
            for (const auto& statement : body.region().statements) {
                const auto* expression = std::get_if<SemExpressionStatement>(&statement.value);
                if (expression == nullptr) {
                    continue;
                }
                const auto* report = std::get_if<SemReport>(&expression->expression.value);
                if (report == nullptr || !report->condition) {
                    continue;
                }
                const auto* comparison = std::get_if<SemBinary>(&(**report->condition).value);
                if (!expect(comparison != nullptr)) {
                    return;
                }
                expect(std::holds_alternative<SemBinding>(comparison->left->value));
                expect(std::holds_alternative<SemBinding>(comparison->right->value));
                ++comparisons;
            }
            expect_equal(comparisons, 2uz);
            const auto completed = execute_body(values, context, body).run();
            expect(completed.has_value());
            if (!expect_equal(context.reports.size(), 2uz)) {
                return;
            }
            for (const auto& report : context.reports) {
                expect(report.reason() == ExecutionReason::Test);
                const auto operands = std::ranges::find(
                    report.fields,
                    std::string_view("operands:"),
                    &ExecutionReportField::label
                );
                if (!expect(operands != report.fields.end())) {
                    return;
                }
                expect_equal(operands->text, std::string("left: 1\nright: 2\n"));
            }
            with_execution(
                R"(const fn run() -> bool {
                    let text: String = "ab";
                    return text == text;
                })",
                [](ProgramDraft& draft,
                   ExecutionContext& context,
                   const auto& evaluate) static noexcept {
                    auto limits = static_execution_limits();
                    // Two produced bytes, then two independent binding reads of two bytes each.
                    limits.text_work = 5uz;
                    expect(!evaluate("run", limits).has_value());
                    check_limit(context, "text");
                    limits.text_work = 6uz;
                    const auto result = evaluate("run", limits);
                    if (!expect(result.has_value())) {
                        return;
                    }
                    const auto atom = execution_atom(draft, *result);
                    if (!expect(atom.has_value())) {
                        return;
                    }
                    expect(std::get<BooleanConstant>(atom->value).value);
                    expect(context.reports.empty());
                }
            );
        };

    "Static execution: root tasks defer source observation and execution until resumed"_test =
        [] static noexcept {
            with_execution(
                "const fn unused() -> i32 => 0;",
                [](ProgramDraft& draft, ExecutionContext& context, const auto&) static noexcept {
                    const auto module_id = draft.provenance_module_at(0uz);
                    const auto source = draft.module_source(module_id);
                    const auto origin = draft.append_source_origin(source, Span::at(0u));
                    const auto changed_origin = draft.append_source_origin(source, Span::at(0u));
                    const auto integer = draft.builtin_type(BuiltinType::I32);
                    auto builder = BodyBuilder(draft.reserve_body(BodyKind::Test), draft);
                    const auto lifetime = builder.add_lifetime_region(
                        std::nullopt,
                        LifetimeRegionKind::Lexical,
                        origin
                    );
                    auto root = builder.make_expression(
                        integer,
                        lifetime,
                        origin,
                        SemUnary {
                            .operation = UnaryOperator::Negate,
                            .operand = OwnedSemanticExpression(builder.make_expression(
                                integer,
                                lifetime,
                                origin,
                                SemConstant {
                                    draft.intern_constant(constant_test_integer_fact(integer, 1))
                                }
                            )),
                        }
                    );
                    auto limits = static_execution_limits();
                    limits.steps = 0uz;
                    {
                        [[maybe_unused]] const auto cancelled =
                            execute_static_root(draft, context, root, limits);
                        expect(context.reports.empty());
                        expect(context.output.empty());
                    }
                    expect(context.reports.empty());
                    limits.steps = 1uz;
                    auto task = execute_static_root(draft, context, root, limits);
                    expect(context.reports.empty());
                    root = builder.make_expression(
                        integer,
                        lifetime,
                        changed_origin,
                        SemConstant {draft.intern_constant(constant_test_integer_fact(integer, 42))}
                    );
                    const auto result = std::move(task).run();
                    if (!expect(result.has_value())) {
                        return;
                    }
                    check_integer(draft, *result, 42);
                    expect(context.reports.empty());
                    expect(context.output.empty());
                }
            );
        };
});

} // namespace
