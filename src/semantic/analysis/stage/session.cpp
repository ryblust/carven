module carven:semantic.analysis.stage.session.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.constant.freeze;
import :semantic.analysis.stage.iteration;
import :semantic.analysis.stage.session;
import :semantic.analysis.stage.specialization;
import :semantic.evaluation.display;
import :semantic.evaluation.execution;
import :semantic.evaluation.value;
import :semantic.semir.decl;
import :semantic.semir.simd;
import :semantic.semir.stage;
import :semantic.semir.traversal;
import :support.invariant;
import std;

namespace {

constexpr auto maximum_nested_roots = 128uz;
constexpr auto maximum_nodes = 524'288uz;
constexpr auto maximum_iterations = 100'000uz;
constexpr auto maximum_instances = 4096uz;

// The block or test whose execution a diagnostic belongs to.
struct ExecutionRoot final {
    std::string_view kind;
    BlockSource source;
};

auto static_execution_code(ExecutionReason reason) noexcept -> DiagnosticCode {
    switch (reason) {
        case ExecutionReason::Admission:       return DiagnosticCode::ConstAdmission;
        case ExecutionReason::Evaluation:      return DiagnosticCode::ConstEvaluation;
        case ExecutionReason::Limit:           return DiagnosticCode::ConstLimit;
        case ExecutionReason::Cycle:           return DiagnosticCode::ConstCycle;
        case ExecutionReason::Unavailable:     return DiagnosticCode::AccessUnavailable;
        case ExecutionReason::Overflow:        return DiagnosticCode::ConstOverflow;
        case ExecutionReason::DivideByZero:    return DiagnosticCode::ConstDivideByZero;
        case ExecutionReason::ShiftOutOfRange: return DiagnosticCode::ConstShiftRange;
        case ExecutionReason::LiteralRange:    return DiagnosticCode::ConstLiteralRange;
        case ExecutionReason::IndexBounds:     return DiagnosticCode::ConstIndexBounds;
        case ExecutionReason::Assertion:       return DiagnosticCode::AssertionFailed;
        case ExecutionReason::Test:            return DiagnosticCode::ConstTest;
    }
    std::unreachable();
}

class StaticExecutionContext final : public SemanticExecutionContext {
public:
    StaticExecutionContext(
        StaticStage& stage,
        std::optional<ExecutionRoot> root = std::nullopt,
        ExecutionOutputMode output = ExecutionOutputMode::Write
    ) noexcept;
    auto function_for_callable(CallableID callable) const noexcept
        -> std::optional<FunctionID> override;
    auto bind_call(
        CallableID callable,
        std::vector<ExecutionOperand>& arguments,
        ExecutionArgumentAccess& access,
        ProgramOriginID origin
    ) noexcept -> ContinuationTask<std::expected<CallableID, ExecutionCallFailure>> override;
    auto freeze(ExecutionValue value) noexcept -> std::optional<ConstantID> override;
    auto prepare_call(CallableID callable, ProgramOriginID origin) noexcept
        -> ContinuationTask<std::expected<ExecutionBody, ExecutionCallFailure>> override;
    auto report(const ExecutionEvent& event) noexcept -> void override;
    auto failure() const noexcept -> std::optional<AnalysisFailure>;
    auto write(ExecutionOutputStream stream, std::string_view bytes) noexcept -> void override;

private:
    StaticStage& stage;
    ProgramDraft& draft;
    const std::optional<ExecutionRoot> root;
    ExecutionOutputMode output;
    std::optional<AnalysisFailure> reported_failure;
};

StaticExecutionContext::StaticExecutionContext(
    StaticStage& stage,
    std::optional<ExecutionRoot> root,
    ExecutionOutputMode output
) noexcept
    : stage(stage),
      draft(stage.draft()),
      root(root),
      output(output) {}

auto StaticExecutionContext::write(ExecutionOutputStream stream, std::string_view bytes) noexcept
    -> void {
    if (output == ExecutionOutputMode::Write) {
        draft.write_output(stream, bytes);
    }
}

auto StaticExecutionContext::function_for_callable(CallableID callable) const noexcept
    -> std::optional<FunctionID> {
    if (const auto instance = draft.static_instance(callable)) {
        return instance->function;
    }
    return draft.function_for_callable(callable);
}

auto StaticExecutionContext::freeze(ExecutionValue value) noexcept -> std::optional<ConstantID> {
    return freeze_constant_value(draft, std::move(value));
}

auto StaticExecutionContext::bind_call(
    CallableID callable,
    std::vector<ExecutionOperand>& arguments,
    ExecutionArgumentAccess& access,
    ProgramOriginID origin
) noexcept -> ContinuationTask<std::expected<CallableID, ExecutionCallFailure>> {
    const auto function = draft.function_for_callable(callable);
    if (!function) {
        co_return callable;
    }
    const auto contract = draft.construction_callable_contract_copy(callable);
    if (!std::ranges::any_of(contract.parameters, [](const auto& parameter) static noexcept {
            return parameter.stage == ParameterStage::Static;
        })) {
        co_return callable;
    }
    if (arguments.size() != contract.parameters.size()) {
        invariant_violation("a checked call has the wrong number of arguments");
    }
    auto statics = std::vector<ConstantID>();
    auto runtime = std::vector<ExecutionOperand>();
    for (auto index = 0uz; index < arguments.size(); ++index) {
        if (contract.parameters[index].stage == ParameterStage::Runtime) {
            runtime.push_back(std::move(arguments[index]));
            continue;
        }
        auto value = access.detach_argument(std::move(arguments[index]), origin);
        if (!value) {
            co_return std::unexpected(std::move(value.error()));
        }
        const auto constant = freeze(std::move(*value));
        if (!constant) {
            co_return std::unexpected(
                ExecutionEvent {
                    .origin = origin,
                    .cause =
                        ExecutionIssue {
                            .reason = ExecutionReason::Admission,
                            .message = "static argument has no frozen representation",
                            .termination = ExecutionTermination::StopRoot,
                        },
                    .fields = {},
                    .calls = {},
                }
            );
        }
        statics.push_back(*constant);
    }
    auto instance = co_await stage.instance(*function, std::move(statics), origin);
    if (!instance) {
        co_return std::unexpected(ExecutionFailure {ExecutionDependencyFailure {}});
    }
    arguments = std::move(runtime);
    co_return *instance;
}

auto StaticExecutionContext::prepare_call(CallableID callable, ProgramOriginID origin) noexcept
    -> ContinuationTask<std::expected<ExecutionBody, ExecutionCallFailure>> {
    const auto failure_event = [&](ExecutionReason reason, std::string message) noexcept {
        return ExecutionEvent {
            .origin = origin,
            .cause =
                ExecutionIssue {
                    .reason = reason,
                    .message = std::move(message),
                    .termination = ExecutionTermination::StopRoot,
                },
            .fields = {},
            .calls = {},
        };
    };
    const auto function = function_for_callable(callable);
    if (!function) {
        co_return std::unexpected(
            failure_event(ExecutionReason::Admission, "callable has no executable Carven body")
        );
    }
    if (!draft.function_declaration_copy(*function).is_const) {
        co_return std::unexpected(failure_event(
            ExecutionReason::Admission,
            "compile-time execution can only call an explicitly declared const fn"
        ));
    }
    if (draft.static_instance(callable)) {
        const auto body = draft.static_instance_body(callable);
        if (!body) {
            co_return std::unexpected(ExecutionFailure {ExecutionDependencyFailure {}});
        }
        if (!*body) {
            co_return std::unexpected(failure_event(
                ExecutionReason::Evaluation,
                "compile-time execution depends on an unfinished specialization"
            ));
        }
        const auto& instance = draft.body_draft(**body);
        co_return ExecutionBody(instance);
    }
    const auto location = draft.source_origin(origin);
    auto completed = co_await stage.requests().ensure_function_body(
        *function,
        draft.source_module(location.source_id),
        location.span
    );
    if (!completed) {
        co_return std::unexpected(ExecutionFailure {ExecutionDependencyFailure {}});
    }
    const auto realized = co_await stage.realize_body(*completed);
    if (!realized) {
        co_return std::unexpected(ExecutionFailure {ExecutionDependencyFailure {}});
    }
    if (stage.realizing(*completed)) {
        co_return std::unexpected(failure_event(
            ExecutionReason::Cycle,
            "call depends on a function whose compile-time execution is still in progress"
        ));
    }
    const auto& body = draft.body_draft(*completed);
    co_return ExecutionBody(body);
}

auto StaticExecutionContext::report(const ExecutionEvent& event) noexcept -> void {
    auto diagnostic =
        DiagnosticBuilder(static_execution_code(event.reason()), execution_message(event));
    diagnostic.primary(draft.source_span(event.origin));
    auto shown = 0uz;
    auto omitted = 0uz;
    for (const auto call : event.calls | std::views::reverse) {
        if (call == event.origin) {
            continue;
        }
        if (shown == 8uz) {
            ++omitted;
            continue;
        }
        diagnostic.related(draft.source_span(call), "while evaluating this const function call");
        ++shown;
    }
    for (const auto call : stage.path() | std::views::reverse) {
        if (call != event.origin && !std::ranges::contains(event.calls, call)) {
            if (shown == 8uz) {
                ++omitted;
                continue;
            }
            diagnostic.related(draft.source_span(call), "while specializing this call");
            ++shown;
        }
    }
    if (omitted > 0) {
        diagnostic.note(
            std::format("{} additional call site{} omitted", omitted, omitted == 1 ? "" : "s")
        );
    }
    const auto report_root = event.blocks.empty()
        ? root
        : std::optional(ExecutionRoot {.kind = "const block", .source = event.blocks.back()});
    if (report_root) {
        auto message = std::format("while evaluating this {}", report_root->kind);
        if (report_root->source.label) {
            message += std::format(" {:?}", draft.spelling(*report_root->source.label));
        }
        diagnostic.related(draft.source_span(report_root->source.origin), std::move(message));
    }
    reported_failure = draft.diagnostics().error(diagnostic.build());
}

auto StaticExecutionContext::failure() const noexcept -> std::optional<AnalysisFailure> {
    return reported_failure;
}

// Execution reports its own failures through the context.
template<typename Value>
auto delivered(
    ProgramDraft& draft,
    const StaticExecutionContext& context,
    ExecutionResult<Value> result
) noexcept -> AnalysisResult<Value> {
    if (const auto failure = context.failure()) {
        return std::unexpected(*failure);
    }
    if (result) {
        if constexpr (std::is_void_v<Value>) {
            return {};
        } else {
            return std::move(*result);
        }
    }
    if (const auto failure = draft.diagnostics().failure()) {
        return std::unexpected(*failure);
    }
    invariant_violation("static execution failed without a diagnostic");
}

} // namespace

// One derivation of a body or an instance. The outermost root owns the budgets.
class StaticStage::Root final {
public:
    Root(StaticStage& stage, std::optional<ProgramOriginID> call) noexcept
        : stage(stage),
          call(call.has_value()) {
        if (stage.roots++ == 0uz) {
            stage.work = {};
        }
        if (call) {
            stage.calls.push_back(*call);
        }
    }

    ~Root() noexcept {
        if (call) {
            stage.calls.pop_back();
        }
        --stage.roots;
    }

    Root(const Root&) = delete;
    auto operator=(const Root&) -> Root& = delete;

    auto entered() const noexcept -> bool { return stage.roots <= maximum_nested_roots; }

private:
    StaticStage& stage;
    bool call;
};

StaticStage::StaticStage(ProgramDraft& draft, ConstructionRequests& requests) noexcept
    : program(draft),
      construction_requests(requests) {}

auto StaticStage::draft() noexcept -> ProgramDraft& {
    return program;
}

auto StaticStage::requests() noexcept -> ConstructionRequests& {
    return construction_requests;
}

auto StaticStage::path() const noexcept -> std::span<const ProgramOriginID> {
    return calls;
}

auto StaticStage::limit_failure(ProgramOriginID origin, std::string_view resource) noexcept
    -> AnalysisFailure {
    auto diagnostic = DiagnosticBuilder(
        DiagnosticCode::ConstLimit,
        std::format("static specialization exceeded its {} budget", resource)
    );
    diagnostic.primary(program.source_span(origin));
    auto shown = 0uz;
    for (const auto call : calls | std::views::reverse) {
        if (call != origin) {
            diagnostic.related(program.source_span(call), "while specializing this call");
            if (++shown == 8uz) {
                break;
            }
        }
    }
    return program.diagnostics().error(diagnostic.build());
}

auto StaticStage::charge(StageResource resource, ProgramOriginID origin) noexcept
    -> AnalysisResult<void> {
    struct Budget final {
        std::size_t limit;
        std::string_view name;
    };

    static constexpr auto budgets = std::array {
        Budget {.limit = maximum_nodes, .name = "node"},
        Budget {.limit = maximum_iterations, .name = "iteration"},
        Budget {.limit = maximum_instances, .name = "instance"},
    };
    const auto& budget = budgets[std::to_underlying(resource)];
    if (++work[std::to_underlying(resource)] > budget.limit) {
        return std::unexpected(limit_failure(origin, budget.name));
    }
    return {};
}

auto StaticStage::realizing(BodyID body) const noexcept -> bool {
    return active_bodies.contains(body);
}

auto StaticStage::realize_body(BodyID body) noexcept -> AnalysisTask<void> {
    if (const auto found = realized_bodies.find(body); found != realized_bodies.end()) {
        if (found->second) {
            co_return std::unexpected(*found->second);
        }
        co_return {};
    }
    if (active_bodies.contains(body)) {
        co_return {};
    }
    auto needs_stage = false;
    visit_semantic_nodes(program.body_draft(body).region, [&](const auto& node) noexcept {
        using Node = std::remove_cvref_t<decltype(node)>;
        if constexpr (std::same_as<Node, SemanticStatement>) {
            needs_stage |= !node.reachable
                || std::holds_alternative<SemStaticBinding>(node.value)
                || std::holds_alternative<SemConstBlock>(node.value);
            if (const auto* loop = std::get_if<SemRangeLoop>(&node.value)) {
                needs_stage |= loop->is_static;
            }
        } else if constexpr (std::same_as<Node, SemanticRegion>) {
            needs_stage |= node.result && !node.result_reachable;
        } else {
            if (const auto* branch = std::get_if<SemIf>(&node.value)) {
                needs_stage |= branch->is_static;
            }
            if (const auto* call = std::get_if<SemCall>(&node.value); call && call->target) {
                if (const auto function = program.function_for_callable(*call->target)) {
                    needs_stage |= program.staged_function(*function);
                }
            }
            if (const auto* call = std::get_if<SemColdCall>(&node.value)) {
                if (const auto function = program.function_for_callable(call->target)) {
                    needs_stage |= program.staged_function(*function);
                }
            }
            if (const auto* intrinsic = std::get_if<SemIntrinsic>(&node.value)) {
                if (const auto* simd = std::get_if<SIMDIntrinsic>(&intrinsic->operation)) {
                    needs_stage |= simd_static_input(*simd).has_value();
                }
            }
        }
    });
    if (!needs_stage) {
        realized_bodies.emplace(body, std::nullopt);
        co_return {};
    }
    auto region = program.body_draft(body).region;
    const auto root = Root(*this, std::nullopt);
    if (!root.entered()) {
        co_return std::unexpected(limit_failure(region.origin, "nesting"));
    }
    active_bodies.insert(body);
    auto changed = co_await specialize_region(*this, body, {}, region);
    active_bodies.erase(body);
    if (!changed) {
        realized_bodies.emplace(body, changed.error());
        co_return std::unexpected(changed.error());
    }
    if (*changed) {
        const auto& source = program.body_draft(body);
        program.set_residual(body, bind_expanded_iterations(source, std::move(region)));
    }
    realized_bodies.emplace(body, std::nullopt);
    co_return {};
}

auto StaticStage::instance(
    FunctionID function,
    std::vector<ConstantID> arguments,
    ProgramOriginID origin
) noexcept -> AnalysisTask<CallableID> {
    if (const auto found = program.find_static_instance(function, arguments)) {
        if (auto body = program.static_instance_body(*found); !body) {
            co_return std::unexpected(body.error());
        }
        co_return *found;
    }
    const auto root = Root(*this, origin);
    if (!root.entered()) {
        co_return std::unexpected(limit_failure(origin, "nesting"));
    }
    if (auto charged = charge(StageResource::Instances, origin); !charged) {
        co_return std::unexpected(charged.error());
    }
    const auto declaration = program.function_declaration_copy(function);
    const auto location = program.source_origin(origin);
    auto completed = co_await construction_requests.ensure_function_body(
        function,
        program.source_module(location.source_id),
        location.span
    );
    if (!completed) {
        co_return std::unexpected(completed.error());
    }
    const auto contract = program.construction_callable_contract_copy(declaration.callable);
    auto environment = StaticEnvironment();
    auto region = [&]() noexcept {
        const auto& source = program.body_draft(*completed);
        auto bound = 0uz;
        for (auto index = 0uz; index < contract.parameters.size(); ++index) {
            if (contract.parameters[index].stage == ParameterStage::Static) {
                environment.emplace(source.inputs.parameters[index], arguments.at(bound++));
            }
        }
        if (bound != arguments.size()) {
            invariant_violation("static instance input arity mismatch");
        }
        return source.region;
    }();
    const auto [callable, body] = program.reserve_static_instance(function, std::move(arguments));
    auto changed = co_await specialize_region(*this, *completed, std::move(environment), region);
    if (!changed) {
        program.fail_static_instance(callable, changed.error());
        co_return std::unexpected(changed.error());
    }
    // Specialization may complete other bodies; borrow the source only now.
    const auto& source = program.body_draft(*completed);
    auto inputs = BodyInputs {.parameters = {}, .captures = source.inputs.captures};
    for (auto index = 0uz; index < contract.parameters.size(); ++index) {
        if (contract.parameters[index].stage == ParameterStage::Runtime) {
            inputs.parameters.push_back(source.inputs.parameters[index]);
        }
    }
    auto expanded = bind_expanded_iterations(source, std::move(region));
    program.complete_static_instance(
        callable,
        StructuredBodyDraft {
            .id = body,
            .kind = source.kind,
            .provenance_identity = source.provenance_identity,
            .inputs = std::move(inputs),
            .lifetime_regions = std::move(expanded.lifetime_regions),
            .bindings = std::move(expanded.bindings),
            .patterns = std::move(expanded.patterns),
            .region = std::move(expanded.region),
            .residual = std::nullopt,
            .specialized = *completed,
        }
    );
    co_return callable;
}

auto StaticStage::evaluate(
    const SemanticExpression& expression,
    ExecutionOutputMode output
) noexcept -> AnalysisTask<ExecutionValue> {
    auto context = StaticExecutionContext(*this, std::nullopt, output);
    co_return delivered(
        program,
        context,
        co_await execute_static_root(program, context, expression)
    );
}

auto StaticStage::run_body(BodyID body, BlockSource source) noexcept -> AnalysisTask<void> {
    const auto& definition = program.body_draft(body);
    auto context = StaticExecutionContext(
        *this,
        ExecutionRoot {
            .kind = definition.kind == BodyKind::Test ? "const test" : "const block",
            .source = source,
        }
    );
    co_return delivered(
        program,
        context,
        co_await execute_body(program, context, ExecutionBody(definition, definition.region))
    );
}

auto StaticStage::run_block(BodyID body, const SemanticRegion& region, BlockSource source) noexcept
    -> AnalysisTask<void> {
    auto context =
        StaticExecutionContext(*this, ExecutionRoot {.kind = "const block", .source = source});
    co_return delivered(
        program,
        context,
        co_await execute_body(program, context, ExecutionBody(program.body_draft(body), region))
    );
}
