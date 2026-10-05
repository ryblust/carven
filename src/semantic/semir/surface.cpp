module carven:semantic.semir.surface.impl;

import :semantic.semir.delegation;
import :semantic.semir.program;
import :semantic.semir.traversal;
import :support.invariant;
import :support.visit;
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

auto body_closure_references(const SemIRProgram& semantic, const SemIRBody& body) noexcept
    -> std::vector<CallableID> {
    auto references = std::vector<CallableID>();
    auto seen_closures = std::flat_set<CallableID>();
    auto seen_types = std::flat_set<TypeID>();
    const auto record = [&](CallableID callable) noexcept {
        if (seen_closures.insert(callable).second) {
            references.push_back(callable);
        }
    };
    const auto collect_type = [&](TypeID root) noexcept {
        auto pending = std::vector<TypeID> {root};
        const auto signature_types = [&](CallableSignatureID id) noexcept {
            const auto& signature = semantic.callable_signatures().signature(id);
            const auto& failures = semantic.failure_sets().failure_set(signature.failures);
            for (const auto failure : std::views::reverse(failures.members)) {
                pending.push_back(failure);
            }
            pending.push_back(signature.result);
            for (const auto& parameter : std::views::reverse(signature.parameters)) {
                pending.push_back(parameter.type);
            }
        };
        while (!pending.empty()) {
            const auto type = pending.back();
            pending.pop_back();
            if (!seen_types.insert(type).second) {
                continue;
            }
            semantic.types().type(type).value.visit(
                Overloaded {
                    [](const BuiltinTypeValue&) static noexcept {},
                    [&](const PointerTypeValue& value) noexcept {
                        pending.push_back(value.target);
                    },
                    [&](const StructTypeValue& value) noexcept {
                        for (const auto& field : std::views::reverse(
                                 semantic.declarations().structure(value.structure).fields
                             )) {
                            pending.push_back(field.type);
                        }
                    },
                    [&](const EnumTypeValue& value) noexcept {
                        for (const auto enum_case : std::views::reverse(
                                 semantic.declarations().enumeration(value.enumeration).cases
                             )) {
                            const auto& payload =
                                semantic.declarations().enum_case(enum_case).payload_types;
                            for (const auto type : std::views::reverse(payload)) {
                                pending.push_back(type);
                            }
                        }
                    },
                    [&](const ArrayTypeValue& value) noexcept { pending.push_back(value.element); },
                    [&](const SliceTypeValue& value) noexcept { pending.push_back(value.element); },
                    [&](const RangeTypeValue& value) noexcept { pending.push_back(value.element); },
                    [&](const FunctionTypeValue& value) noexcept {
                        signature_types(semantic.declarations().callable(value.callable).signature);
                    },
                    [&](const ClosureTypeValue& value) noexcept {
                        record(value.callable);
                        signature_types(semantic.declarations().callable(value.callable).signature);
                    },
                    [&](const CallableViewTypeValue& value) noexcept {
                        signature_types(value.signature);
                    },
                    [&](const CppTypeValue& value) noexcept {
                        const auto arguments = cpp_type_references(value);
                        for (const auto argument : std::views::reverse(arguments)) {
                            pending.push_back(argument);
                        }
                    },
                }
            );
        }
    };
    const auto collect_pattern = [&](PatternID root) noexcept {
        auto pending = std::vector<PatternID> {root};
        while (!pending.empty()) {
            const auto id = pending.back();
            pending.pop_back();
            const auto& pattern = body.pattern(id);
            collect_type(pattern.type);
            pattern.value.visit([&](const auto& value) noexcept {
                using Value = std::remove_cvref_t<decltype(value)>;
                if constexpr (std::same_as<Value, TypeConstraintPattern>) {
                    collect_type(value.type);
                } else if constexpr (std::same_as<Value, OrPattern>) {
                    pending.append_range(std::views::reverse(value.alternatives));
                } else if constexpr (std::same_as<Value, EnumCasePattern>) {
                    pending.append_range(std::views::reverse(value.payload));
                }
            });
        }
    };
    for (const auto parameter : body.inputs().parameters) {
        collect_type(body.binding(parameter).type);
    }
    for (const auto capture : body.inputs().captures) {
        collect_type(body.binding(capture).type);
    }
    visit_semantic_nodes(
        body.realized_region(),
        Overloaded {
            [&](const SemanticExpression& expression) noexcept {
                if (const auto* closure = std::get_if<SemClosure>(&expression.value)) {
                    record(closure->callable);
                }
                collect_type(expression.type.resolved());
                if (const auto* match = std::get_if<SemMatch>(&expression.value)) {
                    for (const auto& arm : match->arms) {
                        collect_pattern(arm.pattern);
                    }
                } else if (const auto* guarded = std::get_if<SemTry>(&expression.value)) {
                    for (const auto& arm : guarded->arms) {
                        for (const auto& alternative : arm.alternatives) {
                            if (const auto* typed =
                                    std::get_if<SemTypedCatchPattern>(&alternative.pattern)) {
                                collect_type(typed->type.resolved());
                                collect_pattern(typed->inner);
                            }
                        }
                    }
                }
            },
            [&](const SemanticStatement& statement) noexcept {
                if (const auto* initialize = std::get_if<SemInitialize>(&statement.value)) {
                    collect_type(body.binding(initialize->binding).type);
                } else if (const auto* binding = std::get_if<SemStaticBinding>(&statement.value)) {
                    collect_type(body.binding(binding->binding).type);
                } else if (const auto* loop = std::get_if<SemRangeLoop>(&statement.value);
                           loop && loop->binding) {
                    collect_type(body.binding(*loop->binding).type);
                } else if (const auto* failure = std::get_if<SemThrow>(&statement.value)) {
                    collect_type(failure->failure_type);
                }
            },
        }
    );
    return references;
}

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
            surface.closures = body_closure_references(*this, bodies().body(*body));
        }
    }
}

auto SemIRProgram::callable_surface(CallableID callable) const noexcept -> const CallableSurface& {
    static_cast<void>(declarations().callable(callable));
    return callable_surfaces.at(callable.index());
}
