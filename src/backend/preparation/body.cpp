module carven:backend.preparation.body.impl;

import :backend.preparation.body;
import :semantic.semir.children;
import :semantic.semir.decl;
import :semantic.semir.delegation;
import :semantic.semir.evaluation;
import :semantic.semir.ids;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

namespace {

// Only direct object projections identify a binding. Pointer and slice targets
// have separate storage; exposing their descriptor does not identify that storage.
auto storage_binding(const SemIRProgram& semantic, const SemanticExpression& source) noexcept
    -> std::optional<LocalBindingID> {
    auto* selected = std::addressof(source);
    while (true) {
        if (const auto* binding = std::get_if<SemBinding>(&selected->value)) {
            return binding->binding;
        }
        if (const auto* field = std::get_if<SemField>(&selected->value);
            field != nullptr && !field->consumes_source()) {
            selected = std::addressof(*field->source);
        } else if (const auto* index = std::get_if<SemIndex>(&selected->value);
                   index != nullptr
                   && std::holds_alternative<ArrayTypeValue>(
                       semantic.types().type(index->source->type.resolved()).value
                   )) {
            selected = std::addressof(*index->source);
        } else if (const auto* take = std::get_if<SemTake>(&selected->value)) {
            selected = std::addressof(*take->place);
        } else {
            return std::nullopt;
        }
    }
}

auto unstable_bindings(const SemIRProgram& semantic, const SemIRBody& body) noexcept
    -> std::flat_set<LocalBindingID> {
    auto result = std::vector<LocalBindingID>();
    const auto expose = [&](const SemanticExpression& source) noexcept {
        if (const auto binding = storage_binding(semantic, source)) {
            result.push_back(*binding);
        }
    };
    const auto access = [&](AccessMode mode, const SemanticExpression& source) noexcept {
        if (mode != AccessMode::Read) {
            expose(source);
        }
    };
    visit_semantic_nodes(
        body.region(),
        Overloaded {
            [&](const SemanticExpression& source) noexcept {
                if (const auto* take = std::get_if<SemTake>(&source.value)) {
                    expose(*take->place);
                } else if (const auto* address = std::get_if<SemAddressOf>(&source.value)) {
                    const auto* pointer = std::get_if<PointerTypeValue>(
                        &semantic.types().type(source.type.resolved()).value
                    );
                    if (pointer == nullptr) {
                        invariant_violation("address-of result must be a pointer");
                    }
                    if (pointer->access == PointerAccess::Write) {
                        expose(*address->source);
                    }
                } else if (const auto* closure = std::get_if<SemClosure>(&source.value)) {
                    for (const auto& capture : closure->captures) {
                        if (capture.mode == CaptureMode::Write) {
                            expose(capture.expression);
                        }
                    }
                } else if (const auto* call = std::get_if<SemCall>(&source.value)) {
                    for (const auto& argument : call->arguments) {
                        access(argument.access, argument.expression);
                    }
                } else if (const auto* cold = std::get_if<SemColdCall>(&source.value)) {
                    for (const auto& argument : cold->arguments) {
                        access(argument.access, argument.expression);
                    }
                } else if (const auto* intrinsic = std::get_if<SemIntrinsic>(&source.value)) {
                    for (const auto& operand : intrinsic->operands) {
                        access(operand.access, operand.expression);
                    }
                } else if (const auto* format = std::get_if<SemFormat>(&source.value)) {
                    if (format->receiver) {
                        expose(**format->receiver);
                    }
                } else if (const auto* cpp = std::get_if<SemCpp>(&source.value)) {
                    visit_cpp_operands(*cpp, access);
                } else if (const auto* cpp_call = std::get_if<SemCppCall>(&source.value)) {
                    visit_cpp_operands(*cpp_call, access);
                }
            },
            [&](const SemanticStatement& source) noexcept {
                if (const auto* assignment = std::get_if<SemAssign>(&source.value)) {
                    expose(assignment->target);
                } else if (const auto* loop = std::get_if<SemRangeLoop>(&source.value);
                           loop != nullptr && loop->access == AccessMode::Write) {
                    expose(loop->source);
                    if (loop->binding) {
                        // Write iteration bindings alias mutable element storage,
                        // including changes through a previously created pointer.
                        result.push_back(*loop->binding);
                    }
                }
            }
        }
    );
    return std::flat_set<LocalBindingID>(std::move(result));
}

// Snapshot owners remain constant unless their storage changes, is exposed,
// or aliases shared mutable storage. A writable address or capture accounts for
// later indirect calls without solving aliases. Native-containing values and
// captures retain their opaque observation policy.
auto stable_binding(
    const SemIRProgram& semantic,
    const LocalBinding& binding,
    bool unstable
) noexcept -> bool {
    if (!semantic.type_contents(binding.type).read_is_value_snapshot()) {
        return false;
    }
    return binding.storage.visit(
        Overloaded {
            [&](const OwnerBindingStorage&) noexcept { return !unstable; },
            [&](const ParameterBindingStorage& parameter) noexcept {
                return parameter.access == AccessMode::Read
                    || (parameter.access == AccessMode::Take && !unstable);
            },
            [](const CaptureBindingStorage&) static noexcept { return false; },
            [](const AsyncChildBindingStorage&) static noexcept { return false; }
        }
    );
}

template<typename Operation>
struct PreparationVisitor final {
    const Operation& operation;

    template<typename Node>
    auto leave(const Node& source) const noexcept -> void {
        operation(source);
    }
};

} // namespace

BodyPreparation::BodyPreparation(const SemIRProgram& semantic, BodyID body) noexcept
    : semantic(semantic),
      metadata(semantic.bodies().body(body)) {
    const auto unstable = unstable_bindings(semantic, metadata);
    const auto callable = semantic.declarations().callable_for_body(metadata.id());
    const auto async_body = callable
        && semantic.callable_signatures()
                .signature(semantic.declarations().callable(*callable).signature)
                .execution
            == CallableExecutionKind::Async;
    const auto index_children = [&](const auto& node) noexcept {
        using Node = std::remove_cvref_t<decltype(node)>;
        if constexpr (std::same_as<Node, SemanticStatement>) {
            if (node.reachable) {
                if (const auto* child = std::get_if<SemAsyncLet>(&node.value)) {
                    child_bindings[metadata.binding(child->child).lifetime].push_back(child->child);
                }
            }
        }
    };
    visit_semantic_nodes(metadata.region(), index_children);
    auto statement_contexts = std::unordered_map<const SemanticStatement*, bool>();
    const auto child_context = [&](const auto& node) noexcept -> bool {
        using Node = std::remove_cvref_t<decltype(node)>;
        if constexpr (std::same_as<Node, SemanticExpression>) {
            return summary(node).requires_coroutine_context;
        } else if constexpr (std::same_as<Node, SemanticRegion>) {
            return region_contexts.at(std::addressof(node));
        } else {
            return statement_contexts.at(std::addressof(node));
        }
    };
    const auto prepare = [&](const auto& source) noexcept {
        using Node = std::remove_cvref_t<decltype(source)>;
        if constexpr (std::same_as<Node, SemanticExpression>) {
            if (std::holds_alternative<SemPropagate>(source.value)) {
                return;
            }
            const auto rule = evaluation_rule(semantic, source);
            auto execution = rule.action == EvaluationAction::Required;
            auto coroutine_context = std::holds_alternative<SemAwait>(source.value)
                || (async_body
                    && !semantic.failure_sets()
                            .failure_set(source.failures.resolved())
                            .members.empty());
            visit_semantic_children(source.value, [&](const auto& child) noexcept {
                coroutine_context |= child_context(child);
            });
            const auto* binding = std::get_if<SemBinding>(&source.value);
            const auto* index = std::get_if<SemIndex>(&source.value);
            const auto reads_slice = index != nullptr
                && std::holds_alternative<SliceTypeValue>(
                                         semantic.types().type(index->source->type.resolved()).value
                );
            auto reads = execution
                || reads_slice
                || (binding != nullptr
                    && !stable_binding(
                        semantic,
                        metadata.binding(binding->binding),
                        unstable.contains(binding->binding)
                    ));
            for (const auto* input : rule.operands) {
                if (input != nullptr) {
                    const auto& child = summary(*input);
                    execution |= child.requires_execution;
                    reads |= child.reads_storage;
                }
            }
            summaries.emplace(
                std::addressof(source),
                ExpressionSummary {
                    .executes_operation = rule.action == EvaluationAction::Required,
                    .requires_execution = execution,
                    .reads_storage = reads,
                    .requires_coroutine_context = coroutine_context,
                }
            );
        } else {
            auto coroutine_context = false;
            if constexpr (std::same_as<Node, SemanticRegion>) {
                coroutine_context = false;
                visit_semantic_children(source, [&](const auto& child) noexcept {
                    coroutine_context |= child_context(child);
                });
                region_contexts.emplace(std::addressof(source), coroutine_context);
            } else {
                coroutine_context = async_body
                    && (std::holds_alternative<SemReturn>(source.value)
                        || std::holds_alternative<SemThrow>(source.value)
                        || std::holds_alternative<SemRethrow>(source.value)
                        || std::holds_alternative<SemAsyncLet>(source.value));
                visit_semantic_children(source.value, [&](const auto& child) noexcept {
                    coroutine_context |= child_context(child);
                });
                statement_contexts.emplace(std::addressof(source), coroutine_context);
            }
        }
    };
    const auto visitor = PreparationVisitor {.operation = prepare};
    visit_semantic_nodes(metadata.region(), visitor);
}

auto BodyPreparation::body() const noexcept -> const SemIRBody& {
    return metadata;
}

auto BodyPreparation::operation(const SemanticExpression& source) noexcept
    -> const SemanticExpression& {
    auto* selected = std::addressof(source);
    while (const auto* propagation = std::get_if<SemPropagate>(&selected->value)) {
        selected = std::addressof(*propagation->operand);
    }
    return *selected;
}

auto BodyPreparation::summary(const SemanticExpression& source) const noexcept
    -> const ExpressionSummary& {
    return summaries.at(std::addressof(operation(source)));
}

auto BodyPreparation::prepare(const SemanticExpression& input) const noexcept -> PreparedOperation {
    const auto& source = operation(input);
    auto preparation = prepare_operation(semantic, source);
    auto inputs = operands(source, preparation.get());
    if (const auto* prepared = std::get_if<PreparedFormat>(preparation.get())) {
        const auto& format = std::get<SemFormat>(source.value);
        const auto offset = format.receiver ? 1uz : 0uz;
        for (auto index = offset; index < inputs.size(); ++index) {
            inputs[index].demand = PreparedDemand::Effects;
        }
        for (const auto index : prepared_format_operands(*prepared)) {
            inputs.at(index + offset).demand = PreparedDemand::Value;
        }
    } else if (const auto* prepared = std::get_if<PreparedPrint>(preparation.get())) {
        for (auto index = 0uz; index < inputs.size(); ++index) {
            if (prepared->operand_text[index]) {
                inputs[index].demand = PreparedDemand::Effects;
            }
        }
    }
    if (const auto* construction = std::get_if<PreparedNativeConstruction>(preparation.get())) {
        for (const auto [index, argument] : std::views::enumerate(construction->arguments)) {
            if (std::holds_alternative<const CppConstructArgument*>(argument)) {
                inputs[index].demand = PreparedDemand::Effects;
            }
        }
    }
    const auto& effect = summary(source);
    return PreparedOperation {
        .operation = source,
        .executes_operation = effect.executes_operation,
        .requires_execution = effect.requires_execution,
        .reads_storage = effect.reads_storage,
        .requires_coroutine_context = effect.requires_coroutine_context,
        .operands = std::move(inputs),
        .preparation = std::move(preparation)
    };
}

auto BodyPreparation::operand(const SemanticExpression& source, PreparedUse use) const noexcept
    -> PreparedOperand {
    return {.expression = std::addressof(source), .use = use, .demand = PreparedDemand::Value};
}

auto BodyPreparation::argument(const SemCallArgument& source) const noexcept -> PreparedOperand {
    auto use = PreparedUse::ReadBorrow;
    if (source.access == AccessMode::Write) {
        use = PreparedUse::WritePlace;
    } else if (source.access == AccessMode::Take) {
        use = PreparedUse::Consume;
    } else if (std::holds_alternative<PointerTypeValue>(
                   semantic.types().type(source.expression.type.resolved()).value
               )) {
        use = PreparedUse::AddressValue;
    }
    return operand(source.expression, use);
}

auto BodyPreparation::requires_coroutine_context(const SemanticRegion& source) const noexcept
    -> bool {
    return region_contexts.at(std::addressof(source));
}

auto BodyPreparation::children(LifetimeRegionID lifetime) const noexcept
    -> std::span<const LocalBindingID> {
    const auto found = child_bindings.find(lifetime);
    return found == child_bindings.end() ? std::span<const LocalBindingID>()
                                         : std::span<const LocalBindingID>(found->second);
}
