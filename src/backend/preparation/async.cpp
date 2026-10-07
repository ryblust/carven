module carven:backend.preparation.async.impl;

import :backend.preparation.async;
import :backend.preparation.body;
import :semantic.semir.body;
import :semantic.semir.decl;
import :semantic.semir.program;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :semantic.semir.type;
import std;

namespace {

auto scalar(const SemIRProgram& program, TypeID type, bool allow_void = false) noexcept -> bool {
    const auto* builtin = std::get_if<BuiltinTypeValue>(&program.types().type(type).value);
    return builtin != nullptr
        && (builtin_is_numeric(builtin->kind)
            || builtin->kind == BuiltinType::Bool
            || builtin->kind == BuiltinType::Char
            || (allow_void && builtin->kind == BuiltinType::Void));
}

// Residual static selection can retain unconditional region wrappers.
// Transparent factories add no execution beyond their returned cold call.
auto factory_return(const SemanticRegion& body) noexcept -> const SemanticExpression* {
    auto* region = std::addressof(body);
    auto returned = false;
    for (;;) {
        const auto* statement = static_cast<const SemanticStatement*>(nullptr);
        for (const auto& candidate : region->statements) {
            if (!candidate.reachable) {
                continue;
            }
            if (statement != nullptr) {
                return nullptr;
            }
            statement = std::addressof(candidate);
        }
        const auto result = region->result && region->result_reachable;
        if (statement != nullptr && result) {
            return nullptr;
        }
        const auto* expression = result ? std::addressof(*region->result) : nullptr;
        if (statement != nullptr) {
            if (const auto* nested = std::get_if<OwnedSemanticRegion>(&statement->value)) {
                returned = false;
                region = std::addressof(**nested);
                continue;
            }
            if (const auto* source_return = std::get_if<SemReturn>(&statement->value)) {
                expression = source_return->value ? std::addressof(*source_return->value) : nullptr;
                returned = true;
            } else if (const auto* evaluated =
                           std::get_if<SemExpressionStatement>(&statement->value)) {
                expression = std::addressof(evaluated->expression);
                returned = false;
            } else {
                return nullptr;
            }
        }
        if (expression == nullptr) {
            return nullptr;
        }
        const auto& operation = BodyPreparation::operation(*expression);
        if (std::holds_alternative<SemColdCall>(operation.value)) {
            return returned ? std::addressof(operation) : nullptr;
        }
        const auto* selected = std::get_if<SemIf>(&operation.value);
        if (selected == nullptr
            || selected->is_static
            || !selected->branches.empty()
            || !selected->otherwise) {
            return nullptr;
        }
        region = std::addressof(**selected->otherwise);
    }
}

auto producer(const SemIRProgram& program, const SemColdCall& call) noexcept
    -> std::optional<PreparedAwaitProducer> {
    const auto& declaration = program.declarations().callable(call.target);
    const auto body_id = callable_body_id(declaration);
    const auto& signature = program.callable_signatures().signature(declaration.signature);
    if (!body_id
        || signature.execution != CallableExecutionKind::Async
        || !scalar(program, signature.result, true)
        || !program.failure_sets().failure_set(signature.failures).members.empty()
        || program.may_complete_cancelled(call.target)
        || std::ranges::any_of(
            signature.parameters,
            [&](const CallableParameter& parameter) noexcept {
                return parameter.stage != ParameterStage::Runtime
                    || parameter.access != AccessMode::Read
                    || !scalar(program, parameter.type);
            }
        )) {
        return std::nullopt;
    }
    const auto& body = program.bodies().body(*body_id);
    if (!body.inputs().captures.empty()
        || body.inputs().parameters.size() != call.arguments.size()
        || std::ranges::any_of(body.bindings(), [](const auto& binding) static noexcept {
               return std::holds_alternative<AsyncChildBindingStorage>(binding.value.storage);
           })) {
        return std::nullopt;
    }
    auto nodes = 0uz;
    auto supported = true;
    visit_semantic_nodes(body.region(), [&](const auto& node) noexcept {
        ++nodes;
        using Node = std::remove_cvref_t<decltype(node)>;
        if constexpr (std::same_as<Node, SemanticExpression>) {
            supported &= !node.exits_test && !std::holds_alternative<SemReport>(node.value);
        } else if constexpr (std::same_as<Node, SemanticStatement>) {
            supported &= !std::holds_alternative<SemAsyncLet>(node.value);
        }
    });
    if (!supported) {
        return std::nullopt;
    }
    return PreparedAwaitProducer {
        .callable = call.target,
        .body = *body_id,
        .nodes = nodes,
        .factory = std::nullopt
    };
}

} // namespace

auto prepare_await_producer(const SemIRProgram& program, const SemanticExpression& source) noexcept
    -> std::optional<PreparedAwaitProducer> {
    const auto& operation = BodyPreparation::operation(source);
    if (const auto* call = std::get_if<SemColdCall>(&operation.value)) {
        return producer(program, *call);
    }
    const auto* call = std::get_if<SemCall>(&operation.value);
    if (call == nullptr || !call->target) {
        return std::nullopt;
    }
    const auto callable = *call->target;
    const auto& declaration = program.declarations().callable(callable);
    const auto body_id = callable_body_id(declaration);
    const auto& signature = program.callable_signatures().signature(declaration.signature);
    if (!body_id
        || signature.execution != CallableExecutionKind::Synchronous
        || !std::holds_alternative<OperationTypeValue>(program.types().type(signature.result).value)
        || !program.failure_sets().failure_set(signature.failures).members.empty()
        || program.may_stop_test(callable)
        || std::ranges::any_of(
            signature.parameters,
            [&](const CallableParameter& parameter) noexcept {
                return parameter.stage != ParameterStage::Runtime
                    || parameter.access != AccessMode::Read
                    || !scalar(program, parameter.type);
            }
        )) {
        return std::nullopt;
    }
    const auto& body = program.bodies().body(*body_id);
    if (!body.inputs().captures.empty()
        || body.inputs().parameters.size() != call->arguments.size()) {
        return std::nullopt;
    }
    const auto* returned = factory_return(body.region());
    if (returned == nullptr || returned->type.resolved() != signature.result) {
        return std::nullopt;
    }
    const auto& cold = std::get<SemColdCall>(returned->value);
    const auto* callee = std::get_if<SemCallable>(&BodyPreparation::operation(*cold.callee).value);
    if (callee == nullptr || callee->callable != cold.target) {
        return std::nullopt;
    }
    for (const auto& argument : cold.arguments) {
        const auto& expression = BodyPreparation::operation(argument.expression);
        if (const auto* binding = std::get_if<SemBinding>(&expression.value)) {
            if (std::ranges::find(body.inputs().parameters, binding->binding)
                == body.inputs().parameters.end()) {
                return std::nullopt;
            }
        } else if (!std::holds_alternative<SemConstant>(expression.value)) {
            return std::nullopt;
        }
    }
    auto prepared = producer(program, cold);
    if (!prepared) {
        return std::nullopt;
    }
    visit_semantic_nodes(body.region(), [&](const auto&) noexcept { ++prepared->nodes; });
    prepared->factory =
        PreparedAwaitFactory {.callable = callable, .body = *body_id, .returned = &cold};
    return prepared;
}

namespace {

auto scalar_parameters(const SemIRProgram& program, const CallableSignature& signature) noexcept
    -> bool {
    return std::ranges::all_of(signature.parameters, [&](const auto& parameter) noexcept {
        return parameter.stage == ParameterStage::Runtime
            && parameter.access == AccessMode::Read
            && scalar(program, parameter.type);
    });
}

auto scalar_call(
    const SemIRProgram& program,
    CallableID target,
    const SemanticExpression& callee,
    std::span<const SemCallArgument> arguments
) noexcept -> bool {
    const auto* named = std::get_if<SemCallable>(&BodyPreparation::operation(callee).value);
    const auto& declaration = program.declarations().callable(target);
    const auto& signature = program.callable_signatures().signature(declaration.signature);
    return named != nullptr
        && named->callable == target
        && scalar_parameters(program, signature)
        && std::ranges::all_of(arguments, [&](const auto& argument) noexcept {
               return argument.access == AccessMode::Read
                   && scalar(program, argument.expression.type.resolved());
           });
}

auto scalar_operation(const SemIRProgram& program, const SemanticExpression& expression) noexcept
    -> bool {
    if (scalar(program, expression.type.resolved(), true)
        || std::holds_alternative<SemCallable>(expression.value)) {
        return true;
    }
    const auto* operation =
        std::get_if<OperationTypeValue>(&program.types().type(expression.type.resolved()).value);
    return operation != nullptr
        && scalar(program, operation->success, true)
        && program.failure_sets().failure_set(operation->failures).members.empty()
        && (std::holds_alternative<SemColdCall>(expression.value)
            || std::holds_alternative<SemAsyncIntrinsic>(expression.value)
            || std::holds_alternative<SemCall>(expression.value));
}

auto closed_scalar_evaluation(
    const SemIRProgram& program,
    const SemanticExpression& expression
) noexcept -> bool {
    auto closed = true;
    visit_semantic_nodes(expression, [&](const auto& node) noexcept {
        using Node = std::remove_cvref_t<decltype(node)>;
        if constexpr (std::same_as<Node, SemanticExpression>) {
            // A completed native operation can still own observable frame
            // parameters until its enclosing full-expression ends.
            closed &= !std::holds_alternative<SemAwait>(node.value)
                && !std::holds_alternative<OperationTypeValue>(
                    program.types().type(node.type.resolved()).value
                );
        }
    });
    return closed;
}

auto transparent_tail_control(
    const SemIRProgram& program,
    const SemanticExpression& expression
) noexcept -> bool {
    if (const auto* branch = std::get_if<SemIf>(&expression.value)) {
        return std::ranges::all_of(branch->branches, [&](const auto& arm) noexcept {
            return closed_scalar_evaluation(program, arm.condition);
        });
    }
    if (const auto* match = std::get_if<SemMatch>(&expression.value)) {
        return closed_scalar_evaluation(program, *match->subject)
            && std::ranges::all_of(match->arms, [&](const auto& arm) noexcept {
                   return (!arm.guard || closed_scalar_evaluation(program, *arm.guard))
                       && std::ranges::all_of(arm.pattern_bounds, [&](const auto& bounds) noexcept {
                              return (!bounds.begin
                                      || closed_scalar_evaluation(program, *bounds.begin))
                                  && (!bounds.end
                                      || closed_scalar_evaluation(program, *bounds.end));
                          });
               });
    }
    return false;
}

auto select_tail_await(
    const SemIRProgram& program,
    const SemanticExpression& source,
    CallableID callable,
    PreparedTailAwaitLoop& selected
) noexcept -> void {
    auto pending = std::vector<const SemanticExpression*> {&source};
    while (!pending.empty()) {
        const auto* expression = pending.back();
        pending.pop_back();
        const auto& operation = BodyPreparation::operation(*expression);
        if (const auto* awaited = std::get_if<SemAwait>(&operation.value)) {
            const auto* call =
                std::get_if<SemColdCall>(&BodyPreparation::operation(*awaited->operand).value);
            if (awaited->operand_kind == AsyncAwaitOperandKind::ColdOperation
                && call != nullptr
                && call->target == callable
                && std::ranges::all_of(
                    call->arguments,
                    [&](const auto& argument) noexcept {
                        return closed_scalar_evaluation(program, argument.expression);
                    }
                )
                && std::ranges::find(selected.awaits, awaited) == selected.awaits.end()) {
                selected.awaits.push_back(awaited);
            }
        } else if (const auto* branch = std::get_if<SemIf>(&operation.value);
                   branch != nullptr && transparent_tail_control(program, operation)) {
            for (const auto& arm : branch->branches) {
                if (arm.body.result && arm.body.result_reachable) {
                    pending.push_back(&*arm.body.result);
                }
            }
            if (branch->otherwise
                && (*branch->otherwise)->result
                && (*branch->otherwise)->result_reachable) {
                pending.push_back(&*(*branch->otherwise)->result);
            }
        } else if (const auto* match = std::get_if<SemMatch>(&operation.value);
                   match != nullptr && transparent_tail_control(program, operation)) {
            for (const auto& arm : match->arms) {
                if (arm.body.result && arm.body.result_reachable) {
                    pending.push_back(&*arm.body.result);
                }
            }
        }
    }
}

struct TailReturnQuery final {
    const SemIRProgram& program;
    CallableID callable;
    PreparedTailAwaitLoop& selected;
    std::size_t enclosing_operands = 0;

    auto operator()(const SemanticExpression& expression) noexcept -> void {
        enclosing_operands += !transparent_tail_control(program, expression);
    }

    auto leave(const SemanticExpression& expression) noexcept -> void {
        enclosing_operands -= !transparent_tail_control(program, expression);
    }

    auto operator()(const SemanticStatement& statement) noexcept -> void {
        if (statement.reachable && enclosing_operands == 0) {
            if (const auto* returned = std::get_if<SemReturn>(&statement.value);
                returned != nullptr && returned->value) {
                select_tail_await(program, *returned->value, callable, selected);
            }
        }
    }
};

} // namespace

auto prepare_tail_await_loop(const SemIRProgram& program, CallableID callable) noexcept
    -> std::optional<PreparedTailAwaitLoop> {
    const auto& declaration = program.declarations().callable(callable);
    const auto body_id = callable_body_id(declaration);
    const auto& signature = program.callable_signatures().signature(declaration.signature);
    const auto* result =
        std::get_if<BuiltinTypeValue>(&program.types().type(signature.result).value);
    if (!body_id
        || signature.execution != CallableExecutionKind::Async
        || !scalar(program, signature.result)
        // Each char await retains its own validation and diagnostic site.
        || (result != nullptr && result->kind == BuiltinType::Char)
        || !scalar_parameters(program, signature)
        || !program.failure_sets().failure_set(signature.failures).members.empty()
        || program.may_complete_cancelled(callable)
        || program.may_stop_test(callable)) {
        return std::nullopt;
    }
    const auto& body = program.bodies().body(*body_id);
    if (!body.inputs().captures.empty()
        || std::ranges::any_of(body.bindings(), [&](const auto& binding) noexcept {
               return !scalar(program, binding.value.type)
                   || std::holds_alternative<AsyncChildBindingStorage>(binding.value.storage);
           })) {
        return std::nullopt;
    }
    auto supported = true;
    auto selected = PreparedTailAwaitLoop {.awaits = {}};
    visit_semantic_nodes(body.region(), [&](const auto& node) noexcept {
        using Node = std::remove_cvref_t<decltype(node)>;
        if constexpr (std::same_as<Node, SemanticExpression>) {
            supported &= scalar_operation(program, node)
                && !node.exits_test
                && program.failure_sets().failure_set(node.failures.resolved()).members.empty()
                && !std::holds_alternative<SemAddressOf>(node.value)
                && !std::holds_alternative<SemCpp>(node.value)
                && !std::holds_alternative<SemCppCall>(node.value)
                && !std::holds_alternative<SemTry>(node.value)
                && !std::holds_alternative<SemReport>(node.value);
            if (const auto* call = std::get_if<SemCall>(&node.value)) {
                supported &= call->target
                    && scalar_call(program, *call->target, *call->callee, call->arguments);
            } else if (const auto* cold = std::get_if<SemColdCall>(&node.value)) {
                supported &= scalar_call(program, cold->target, *cold->callee, cold->arguments);
            }
        } else if constexpr (std::same_as<Node, SemanticStatement>) {
            supported &= !std::holds_alternative<SemAsyncLet>(node.value);
        }
    });
    visit_semantic_nodes(body.region(), TailReturnQuery {program, callable, selected});
    return supported && !selected.awaits.empty() ? std::optional(std::move(selected))
                                                 : std::nullopt;
}
