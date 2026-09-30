module carven:semantic.semir.surface.impl;

import :semantic.semir.program;
import :semantic.semir.traversal;
import :support.invariant;
import std;

namespace {

auto collect_surface(const SemIRProgram& semantic, const SemIRBody& body) noexcept
    -> CallableSurface {
    auto types = std::set<TypeID>();
    auto callables = std::set<CallableID>();
    const auto collect_pattern = [&](PatternID root) noexcept {
        auto pending = std::vector<PatternID> {root};
        while (!pending.empty()) {
            const auto id = pending.back();
            pending.pop_back();
            const auto& pattern = body.pattern(id);
            types.insert(pattern.type);
            pattern.value.visit([&](const auto& value) noexcept {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<Value, TypeConstraintPattern>) {
                    types.insert(value.type);
                } else if constexpr (std::same_as<Value, OrPattern>) {
                    pending.append_range(value.alternatives);
                } else if constexpr (std::same_as<Value, EnumCasePattern>) {
                    types.insert(pattern.type);
                    pending.append_range(value.payload);
                }
            });
        }
    };
    using NodeRef =
        std::variant<const SemanticExpression*, const SemanticRegion*, const SemanticStatement*>;
    auto pending = std::vector<NodeRef> {&body.region()};
    const auto enqueue = [&](const auto& node) noexcept {
        pending.emplace_back(&node);
    };
    while (!pending.empty()) {
        const auto next = pending.back();
        pending.pop_back();
        next.visit([&](const auto* source) noexcept {
            const auto& node = *source;
            using Node = std::remove_cvref_t<decltype(node)>;
            if constexpr (std::same_as<Node, SemanticRegion>) {
                visit_semantic_children(node, [&](const auto& child) noexcept { enqueue(child); });
            } else if constexpr (std::same_as<Node, SemanticExpression>) {
                types.insert(node.type.resolved());
                if (const auto* call = std::get_if<SemCall>(&node.value)) {
                    if (call->target) {
                        callables.insert(*call->target);
                    }
                    enqueue(*call->callee);
                    if (call->target) {
                        const auto& signature = semantic.callable_signatures().signature(
                            semantic.declarations().callable(*call->target).signature
                        );
                        if (signature.parameters.size() != call->arguments.size()) {
                            invariant_violation("generic call arguments do not match signature");
                        }
                        for (auto index = 0uz; index < call->arguments.size(); ++index) {
                            if (signature.parameters[index].stage == ParameterStage::Runtime) {
                                enqueue(call->arguments[index].expression);
                            }
                        }
                        return;
                    }
                    for (const auto& argument : call->arguments) {
                        enqueue(argument.expression);
                    }
                    return;
                }
                if (const auto* match = std::get_if<SemMatch>(&node.value)) {
                    for (const auto& arm : match->arms) {
                        collect_pattern(arm.pattern);
                    }
                } else if (const auto* guarded = std::get_if<SemTry>(&node.value)) {
                    for (const auto& arm : guarded->arms) {
                        for (const auto& alternative : arm.alternatives) {
                            if (const auto* typed =
                                    std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                                types.insert(typed->type.resolved());
                                collect_pattern(typed->inner);
                            }
                        }
                    }
                }
                if (const auto* callable = std::get_if<SemCallable>(&node.value)) {
                    callables.insert(callable->callable);
                }
                if (const auto* conditional = std::get_if<SemIf>(&node.value);
                    conditional != nullptr && conditional->is_static) {
                    for (const auto& branch : conditional->branches) {
                        enqueue(branch.body);
                    }
                    if (conditional->otherwise) {
                        enqueue(**conditional->otherwise);
                    }
                    return;
                }
                visit_semantic_children(node.value, [&](const auto& child) noexcept {
                    enqueue(child);
                });
            } else {
                if (std::holds_alternative<SemStaticBinding>(node.value)
                    || std::holds_alternative<SemConstBlock>(node.value)) {
                    return;
                }
                if (const auto* loop = std::get_if<SemRangeLoop>(&node.value);
                    loop != nullptr && loop->is_static) {
                    enqueue(*loop->body);
                    return;
                }
                visit_semantic_children(node.value, [&](const auto& child) noexcept {
                    enqueue(child);
                });
            }
        });
    }
    return {
        .types = std::vector<TypeID>(types.begin(), types.end()),
        .callables = std::vector<CallableID>(callables.begin(), callables.end()),
        .closures = {},
    };
}

} // namespace

auto SemIRProgram::publish_surfaces() noexcept -> void {
    callable_surfaces.resize(declarations().callables().size());
    for (const auto entry : declarations().functions()) {
        const auto callable = entry.value.callable;
        if (definition_placement(callable) != DefinitionPlacement::None) {
            continue;
        }
        const auto body = declarations().body_for_callable(callable);
        if (body) {
            callable_surfaces[callable.index()] = collect_surface(*this, bodies().body(*body));
        }
    }
    for (const auto entry : declarations().callables()) {
        if (const auto body = callable_body_id(entry.value)) {
            auto& surface = callable_surfaces[entry.id.index()];
            visit_semantic_nodes(
                bodies().body(*body).region(),
                [&](const SemanticExpression& expression) noexcept {
                    if (const auto* closure = std::get_if<SemClosure>(&expression.value)) {
                        surface.closures.push_back(closure->callable);
                    }
                }
            );
        }
    }
}

auto SemIRProgram::callable_surface(CallableID callable) const noexcept -> const CallableSurface& {
    static_cast<void>(declarations().callable(callable));
    return callable_surfaces.at(callable.index());
}
