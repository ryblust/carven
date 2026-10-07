module carven:semantic.analysis.async.cancellation.impl;

import :semantic.analysis.async;
import :semantic.semir.async;
import :semantic.semir.children;
import :semantic.semir.decl;
import :semantic.semir.evaluation;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.type;
import std;

namespace {

// Both execution and cold-value provenance use the same monotone OR domain.
// Only the published completion observations survive this analysis.
struct CancellationCapability final {
    bool cancelled;
    std::vector<std::size_t> consumers;
};

struct BindingCapability final {
    std::size_t node;
    bool sourced;
};

using CancellationNode =
    std::variant<const SemanticExpression*, const SemanticRegion*, const SemanticStatement*>;

struct CancellationVisit final {
    CancellationNode node;
    bool reachable;
};

class CancellationAnalysis final {
public:
    explicit CancellationAnalysis(const SemIRProgram& program) noexcept;
    auto finish() noexcept -> AsyncCancellationFacts;

private:
    auto add_node(bool cancelled = false) noexcept -> std::size_t;
    auto connect(std::size_t source, std::size_t consumer) noexcept -> void;
    auto is_operation(TypeID type) const noexcept -> bool;
    auto expression_node(const SemanticExpression& expression) noexcept -> std::size_t;
    auto binding_node(const SemIRBody& body, LocalBindingID binding) noexcept -> BindingCapability&;
    auto bind(
        const SemIRBody& body,
        LocalBindingID binding,
        const SemanticExpression& source
    ) noexcept -> void;
    auto region_result(const SemanticRegion& region, std::size_t consumer) noexcept -> void;
    auto call_target(const SemCall& call) const noexcept -> std::optional<CallableID>;
    auto visit_body(const SemIRBody& body) noexcept -> void;
    auto visit_expression(
        const SemIRBody& body,
        const SemanticExpression& expression,
        bool reachable,
        std::optional<std::size_t> completion,
        std::vector<CancellationVisit>& pending
    ) noexcept -> void;

    const SemIRProgram& program;
    std::vector<CancellationCapability> nodes;
    std::size_t unknown;
    std::vector<std::size_t> callable_completion;
    std::vector<std::size_t> returned_operation;
    std::map<const SemanticExpression*, std::size_t> expressions;
    std::map<std::pair<BodyID, LocalBindingID>, BindingCapability> bindings;
    std::map<BodyID, std::map<const SemAwait*, std::size_t>> awaits;
};

CancellationAnalysis::CancellationAnalysis(const SemIRProgram& semantic) noexcept
    : program(semantic),
      unknown(add_node(true)) {
    const auto no_cancel = add_node();
    for (const auto entry : program.declarations().callables()) {
        const auto& signature = program.callable_signatures().signature(entry.value.signature);
        const auto body = callable_body_id(entry.value);
        const auto unavailable = !body || !program.bodies().contains(*body);
        callable_completion.push_back(
            signature.execution == CallableExecutionKind::Async ? add_node(unavailable) : no_cancel
        );
        returned_operation.push_back(
            is_operation(signature.result) ? add_node(unavailable) : unknown
        );
    }
}

auto CancellationAnalysis::add_node(bool cancelled) noexcept -> std::size_t {
    const auto result = nodes.size();
    nodes.push_back({.cancelled = cancelled, .consumers = {}});
    return result;
}

auto CancellationAnalysis::connect(std::size_t source, std::size_t consumer) noexcept -> void {
    nodes[source].consumers.push_back(consumer);
}

auto CancellationAnalysis::is_operation(TypeID type) const noexcept -> bool {
    return std::holds_alternative<OperationTypeValue>(program.types().type(type).value);
}

auto CancellationAnalysis::expression_node(const SemanticExpression& expression) noexcept
    -> std::size_t {
    if (!is_operation(expression.type.resolved())) {
        return unknown;
    }
    if (const auto found = expressions.find(&expression); found != expressions.end()) {
        return found->second;
    }
    const auto node = add_node();
    expressions.emplace(&expression, node);
    return node;
}

auto CancellationAnalysis::binding_node(const SemIRBody& body, LocalBindingID binding) noexcept
    -> BindingCapability& {
    static_cast<void>(body.binding(binding));
    const auto key = std::pair(body.id(), binding);
    if (const auto found = bindings.find(key); found != bindings.end()) {
        return found->second;
    }
    const auto node = add_node();
    return bindings.emplace(key, BindingCapability {.node = node, .sourced = false}).first->second;
}

auto CancellationAnalysis::bind(
    const SemIRBody& body,
    LocalBindingID binding,
    const SemanticExpression& source
) noexcept -> void {
    if (!is_operation(body.binding(binding).type)) {
        return;
    }
    auto& destination = binding_node(body, binding);
    destination.sourced = true;
    connect(expression_node(source), destination.node);
}

auto CancellationAnalysis::region_result(
    const SemanticRegion& region,
    std::size_t consumer
) noexcept -> void {
    if (region.result && region.result_reachable) {
        connect(expression_node(*region.result), consumer);
    }
}

auto CancellationAnalysis::call_target(const SemCall& call) const noexcept
    -> std::optional<CallableID> {
    if (call.target) {
        return call.target;
    }
    const auto& type = program.types().type(call.callee->type.resolved()).value;
    if (const auto* function = std::get_if<FunctionTypeValue>(&type)) {
        return function->callable;
    }
    if (const auto* closure = std::get_if<ClosureTypeValue>(&type)) {
        return closure->callable;
    }
    return std::nullopt;
}

auto CancellationAnalysis::visit_expression(
    const SemIRBody& body,
    const SemanticExpression& expression,
    bool reachable,
    std::optional<std::size_t> completion,
    std::vector<CancellationVisit>& pending
) noexcept -> void {
    const auto value = is_operation(expression.type.resolved()) && expression.operation_reachable
        ? std::optional(expression_node(expression))
        : std::nullopt;
    const auto add = [&](const auto& child) noexcept {
        pending.push_back({.node = &child, .reachable = reachable});
    };
    const auto select = [&](const SemanticRegion& region, bool possible) noexcept {
        if (value && possible) {
            region_result(region, *value);
        }
        pending.push_back({.node = &region, .reachable = reachable && possible});
    };
    expression.value.visit([&](const auto& operation) noexcept {
        using Operation = std::remove_cvref_t<decltype(operation)>;
        if constexpr (std::same_as<Operation, SemBinding>) {
            if (value) {
                connect(binding_node(body, operation.binding).node, *value);
            }
        } else if constexpr (std::same_as<Operation, SemTake>) {
            if (value) {
                connect(expression_node(*operation.place), *value);
            }
        } else if constexpr (std::same_as<Operation, SemPropagate>
                             || std::same_as<Operation, SemCast>) {
            if (value) {
                connect(expression_node(*operation.operand), *value);
            }
        } else if constexpr (std::same_as<Operation, SemColdCall>) {
            if (value) {
                connect(callable_completion.at(operation.target.index()), *value);
            }
        } else if constexpr (std::same_as<Operation, SemCall>) {
            if (value) {
                const auto target = call_target(operation);
                if (target) {
                    const auto& signature = program.callable_signatures().signature(
                        program.declarations().callable(*target).signature
                    );
                    connect(
                        signature.execution == CallableExecutionKind::Async
                            ? callable_completion.at(target->index())
                            : returned_operation.at(target->index()),
                        *value
                    );
                } else {
                    connect(unknown, *value);
                }
            }
        } else if constexpr (std::same_as<Operation, SemAsyncIntrinsic>) {
            if (value && operation.kind != AsyncIntrinsic::YieldOnce) {
                connect(unknown, *value);
            }
        } else if constexpr (std::same_as<Operation, SemAwait>) {
            const auto producer = expression_node(*operation.operand);
            awaits[body.id()].emplace(&operation, producer);
            if (completion && reachable && expression.operation_reachable) {
                connect(producer, *completion);
            }
            if (value) {
                connect(unknown, *value);
            }
        } else if constexpr (std::same_as<Operation, SemIf>) {
            auto remaining = true;
            for (const auto& branch : operation.branches) {
                pending.push_back({.node = &branch.condition, .reachable = reachable && remaining});
                const auto truth = known_boolean(program, branch.condition);
                const auto evaluated = remaining && branch.condition.operation_reachable;
                select(branch.body, evaluated && truth != false);
                remaining = evaluated && truth != true;
            }
            if (operation.otherwise) {
                select(**operation.otherwise, remaining);
            }
            return;
        } else if constexpr (std::same_as<Operation, SemMatch>) {
            add(*operation.subject);
            for (const auto& arm : operation.arms) {
                const auto attempted = operation.subject->operation_reachable && arm.reachable;
                for (const auto binding : arm.bindings) {
                    bind(body, binding, *operation.subject);
                }
                for (const auto& bound : arm.pattern_bounds) {
                    if (bound.begin) {
                        pending.push_back(
                            {.node = &*bound.begin, .reachable = reachable && attempted}
                        );
                    }
                    if (bound.end) {
                        pending.push_back(
                            {.node = &*bound.end, .reachable = reachable && attempted}
                        );
                    }
                }
                if (arm.guard) {
                    pending.push_back({.node = &*arm.guard, .reachable = reachable && attempted});
                }
                select(
                    arm.body,
                    attempted
                        && (!arm.guard
                            || (arm.guard->operation_reachable
                                && known_boolean(program, *arm.guard) != false))
                );
            }
            return;
        } else if constexpr (std::same_as<Operation, SemTry>) {
            select(*operation.body, true);
            for (const auto& arm : operation.arms) {
                const auto attempted = !program.failure_sets()
                                            .failure_set(arm.accepted_failures.resolved())
                                            .members.empty()
                    && std::ranges::any_of(
                        arm.alternatives,
                        [](const auto& alternative) static noexcept {
                            return alternative.reachable;
                        }
                    );
                for (const auto& bound : arm.pattern_bounds) {
                    if (bound.begin) {
                        pending.push_back(
                            {.node = &*bound.begin, .reachable = reachable && attempted}
                        );
                    }
                    if (bound.end) {
                        pending.push_back(
                            {.node = &*bound.end, .reachable = reachable && attempted}
                        );
                    }
                }
                if (arm.guard) {
                    pending.push_back({.node = &*arm.guard, .reachable = reachable && attempted});
                }
                select(
                    arm.body,
                    attempted
                        && (!arm.guard
                            || (arm.guard->operation_reachable
                                && known_boolean(program, *arm.guard) != false))
                );
            }
            return;
        } else if constexpr (std::same_as<Operation, SemShortCircuit>) {
            add(*operation.left);
            const auto truth = known_boolean(program, *operation.left);
            const auto evaluated = operation.left->operation_reachable
                && (!truth || *truth == (operation.operation == ShortCircuitOperator::And));
            pending.push_back({.node = &*operation.right, .reachable = reachable && evaluated});
            return;
        } else if (value) {
            connect(unknown, *value);
        }
        // Required operands can execute even when the operation itself cannot.
        visit_evaluation_children(operation, add);
    });
}

auto CancellationAnalysis::visit_body(const SemIRBody& body) noexcept -> void {
    auto completion = std::optional<std::size_t>();
    auto returned = std::optional<std::size_t>();
    if (const auto callable = program.declarations().callable_for_body(body.id())) {
        const auto& signature = program.callable_signatures().signature(
            program.declarations().callable(*callable).signature
        );
        if (signature.execution == CallableExecutionKind::Async) {
            completion = callable_completion.at(callable->index());
        }
        if (is_operation(signature.result)) {
            returned = returned_operation.at(callable->index());
            region_result(body.region(), *returned);
        }
    }
    if (!completion && !returned) {
        return;
    }
    auto pending = std::vector<CancellationVisit> {{.node = &body.region(), .reachable = true}};
    while (!pending.empty()) {
        const auto next = pending.back();
        pending.pop_back();
        next.node.visit([&](const auto* node) noexcept {
            using Value = std::remove_cvref_t<decltype(*node)>;
            const auto add = [&](const auto& child) noexcept {
                pending.push_back({.node = &child, .reachable = next.reachable});
            };
            if constexpr (std::same_as<Value, SemanticRegion>) {
                for (const auto& statement : node->statements) {
                    pending.push_back(
                        {.node = &statement, .reachable = next.reachable && statement.reachable}
                    );
                }
                if (node->result) {
                    pending.push_back(
                        {.node = &*node->result,
                         .reachable = next.reachable && node->result_reachable}
                    );
                }
            } else if constexpr (std::same_as<Value, SemanticExpression>) {
                visit_expression(body, *node, next.reachable, completion, pending);
            } else {
                node->value.visit([&](const auto& statement) noexcept {
                    using Statement = std::remove_cvref_t<decltype(statement)>;
                    if constexpr (std::same_as<Statement, SemInitialize>) {
                        bind(body, statement.binding, statement.initializer);
                    } else if constexpr (std::same_as<Statement, SemAsyncLet>) {
                        bind(body, statement.child, statement.initializer);
                    } else if constexpr (std::same_as<Statement, SemReturn>) {
                        if (returned && next.reachable && statement.value) {
                            connect(expression_node(*statement.value), *returned);
                        }
                    } else if constexpr (std::same_as<Statement, SemLoop>) {
                        add(*statement.initializer);
                        if (statement.condition) {
                            add(*statement.condition);
                        }
                        const auto entered = !statement.condition
                            || (statement.condition->operation_reachable
                                && known_boolean(program, *statement.condition) != false);
                        pending.push_back(
                            {.node = &*statement.body, .reachable = next.reachable && entered}
                        );
                        pending.push_back(
                            {.node = &*statement.steps, .reachable = next.reachable && entered}
                        );
                        return;
                    }
                    visit_evaluation_children(statement, add);
                });
            }
        });
    }
}

auto CancellationAnalysis::finish() noexcept -> AsyncCancellationFacts {
    for (const auto entry : program.bodies().entries()) {
        visit_body(entry.value);
    }
    for (const auto& [binding, capability] : bindings) {
        if (!capability.sourced) {
            connect(unknown, capability.node);
        }
    }
    auto pending = std::vector<std::size_t>();
    for (auto index = 0uz; index < nodes.size(); ++index) {
        if (nodes[index].cancelled) {
            pending.push_back(index);
        }
    }
    for (auto next = 0uz; next < pending.size(); ++next) {
        for (const auto consumer : nodes[pending[next]].consumers) {
            if (!nodes[consumer].cancelled) {
                nodes[consumer].cancelled = true;
                pending.push_back(consumer);
            }
        }
    }
    auto result = AsyncCancellationFacts {.callable_completion = {}, .await_completion = {}};
    for (const auto node : callable_completion) {
        result.callable_completion.push_back(nodes[node].cancelled);
    }
    for (const auto& [body, observations] : awaits) {
        auto& destination = result.await_completion[body];
        for (const auto& [occurrence, node] : observations) {
            destination.emplace(occurrence, nodes[node].cancelled);
        }
    }
    return result;
}

} // namespace

auto analyze_async_cancellation(const SemIRProgram& program) noexcept -> AsyncCancellationFacts {
    return CancellationAnalysis(program).finish();
}
