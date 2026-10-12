module carven:semantic.semir.clone.impl;

import :semantic.semir.children;
import :semantic.semir.structured;
import std;

namespace {

// First construct each parent's child storage, then fill its children in place.
// Pending source/target pairs borrow stable owning storage without native recursion.
class SemanticClone final {
public:
    template<typename Value>
    auto tree(const Value& value) noexcept -> Value {
        auto result = operation(value);
        append_children(value, result);
        while (!pending.empty()) {
            const auto next = pending.back();
            pending.pop_back();
            next.visit([&](const auto& pair) noexcept {
                if constexpr (std::same_as<
                                  std::remove_cvref_t<decltype(*pair.source)>,
                                  SemanticRegion>) {
                    append_children(*pair.source, *pair.target);
                } else {
                    pair.target->value = operation(pair.source->value);
                    append_children(pair.source->value, pair.target->value);
                }
            });
        }
        return result;
    }

private:
    template<typename Node>
    struct NodePair final {
        const Node* source;
        Node* target;
    };

    template<typename Value>
    auto append_children(const Value& source, Value& target) noexcept -> void {
        auto index = pending.size();
        visit_semantic_children(source, [&](const auto& child) noexcept {
            using Node = std::remove_cvref_t<decltype(child)>;
            pending.push_back(NodePair<Node> {.source = &child, .target = nullptr});
        });
        visit_semantic_children(target, [&](auto& child) noexcept {
            using Node = std::remove_cvref_t<decltype(child)>;
            std::get<NodePair<Node>>(pending[index++]).target = &child;
        });
    }

    auto copy(const SemanticExpression& value) noexcept -> SemanticExpression;
    auto copy(const SemanticStatement& value) noexcept -> SemanticStatement;
    auto copy(const SemanticRegion& value) noexcept -> SemanticRegion;
    auto copy(const OwnedSemanticExpression& value) noexcept -> OwnedSemanticExpression;
    auto copy(const OwnedSemanticRegion& value) noexcept -> OwnedSemanticRegion;
    auto copy(const SemCallArgument& value) noexcept -> SemCallArgument;
    auto copy(const SemCapture& value) noexcept -> SemCapture;
    auto copy(const SemFieldInitializer& value) noexcept -> SemFieldInitializer;
    auto copy(const SemConditionalBranch& value) noexcept -> SemConditionalBranch;
    auto copy(const SemPatternBounds& value) noexcept -> SemPatternBounds;
    auto copy(const SemMatchArm& value) noexcept -> SemMatchArm;
    auto copy(const SemCatchArm& value) noexcept -> SemCatchArm;
    auto copy(const SemCppOperand& value) noexcept -> SemCppOperand;

    template<typename T>
    auto copy(const std::vector<T>& values) noexcept -> std::vector<T> {
        auto result = std::vector<T>();
        result.reserve(values.size());
        for (const auto& value : values) {
            result.push_back(copy(value));
        }
        return result;
    }

    template<typename T>
    auto copy(const std::optional<T>& value) noexcept -> std::optional<T> {
        return value ? std::optional(copy(*value)) : std::nullopt;
    }

    template<typename Value>
    auto operation(const Value& value) noexcept -> Value {
        return value.visit([&](const auto& node) noexcept -> Value {
            using T = std::remove_cvref_t<decltype(node)>;
            if constexpr (std::same_as<T, SemRange>) {
                return T {
                    .begin = copy(node.begin),
                    .end = copy(node.end),
                    .inclusive = node.inclusive
                };
            } else if constexpr (std::same_as<T, SemArray> || std::same_as<T, SemTuple>) {
                return T {.elements = copy(node.elements)};
            } else if constexpr (std::same_as<T, SemStruct>) {
                return T {.structure = node.structure, .fields = copy(node.fields)};
            } else if constexpr (std::same_as<T, SemEnumCase>) {
                return T {.enum_case = node.enum_case, .payload = copy(node.payload)};
            } else if constexpr (std::same_as<T, SemUnary>) {
                return T {.operation = node.operation, .operand = copy(node.operand)};
            } else if constexpr (std::same_as<T, SemBinary> || std::same_as<T, SemShortCircuit>) {
                return T {
                    .left = copy(node.left),
                    .operation = node.operation,
                    .right = copy(node.right)
                };
            } else if constexpr (std::same_as<T, SemCast>) {
                return T {.operand = copy(node.operand), .kind = node.kind};
            } else if constexpr (std::same_as<T, SemDereference>) {
                return T {.source = copy(node.source), .origin = node.origin};
            } else if constexpr (std::same_as<T, SemField>) {
                return T {.source = copy(node.source), .field = node.field};
            } else if constexpr (std::same_as<T, SemTupleElement>) {
                return T {.source = copy(node.source), .index = node.index};
            } else if constexpr (std::same_as<T, SemIndex>) {
                return T {
                    .source = copy(node.source),
                    .index = copy(node.index),
                    .bounds = node.bounds,
                    .source_owns_elements = node.source_owns_elements
                };
            } else if constexpr (std::same_as<T, SemArrayAdopt>
                                 || std::same_as<T, SemAddressOf>
                                 || std::same_as<T, SemBorrowCallable>) {
                return T {.source = copy(node.source)};
            } else if constexpr (std::same_as<T, SemTake>) {
                return T {.place = copy(node.place)};
            } else if constexpr (std::same_as<T, SemPropagate>) {
                return T {.operand = copy(node.operand)};
            } else if constexpr (std::same_as<T, SemReport>) {
                return T {
                    .kind = node.kind,
                    .condition = copy(node.condition),
                    .message = copy(node.message),
                    .condition_source = node.condition_source,
                    .operand_sources = node.operand_sources
                };
            } else if constexpr (std::same_as<T, SemPrint>) {
                return T {.kind = node.kind, .operands = copy(node.operands)};
            } else if constexpr (std::same_as<T, SemFormat>) {
                return T {
                    .specification = node.specification,
                    .operands = copy(node.operands),
                    .receiver = copy(node.receiver)
                };
            } else if constexpr (std::same_as<T, SemIntrinsic> || std::same_as<T, SemCpp>) {
                return T {.operation = node.operation, .operands = copy(node.operands)};
            } else if constexpr (std::same_as<T, SemCppCall>) {
                auto callee =
                    node.callee.visit([&](const auto& callee) noexcept -> CppCallee<SemCppOperand> {
                        using Callee = std::remove_cvref_t<decltype(callee)>;
                        if constexpr (std::same_as<Callee, CppNameReference>
                                      || std::same_as<
                                          Callee,
                                          CppStaticMemberCallee<ConstructionTypeRef>>) {
                            return callee;
                        } else if constexpr (std::same_as<Callee, SemCppOperand>) {
                            return copy(callee);
                        } else {
                            return Callee {
                                .receiver = copy(callee.receiver),
                                .member = callee.member
                            };
                        }
                    });
                return T {.callee = std::move(callee), .arguments = copy(node.arguments)};
            } else if constexpr (std::same_as<T, SemCall>) {
                return T {
                    .callee = copy(node.callee),
                    .target = node.target,
                    .arguments = copy(node.arguments),
                    .callee_failures = node.callee_failures
                };
            } else if constexpr (std::same_as<T, SemGenericCall>) {
                return T {
                    .definition = node.definition,
                    .environment = node.environment,
                    .arguments = copy(node.arguments),
                    .callee_failures = node.callee_failures
                };
            } else if constexpr (std::same_as<T, SemColdCall>) {
                return T {
                    .callee = copy(node.callee),
                    .target = node.target,
                    .arguments = copy(node.arguments)
                };
            } else if constexpr (std::same_as<T, SemColdCompose>) {
                return T {
                    .policy = node.policy,
                    .success = node.success,
                    .inputs = copy(node.inputs)
                };
            } else if constexpr (std::same_as<T, SemColdTimeout>) {
                return T {
                    .operation = copy(node.operation),
                    .duration_ticks = copy(node.duration_ticks),
                    .timeout_failure = node.timeout_failure,
                    .range_failure = node.range_failure,
                };
            } else if constexpr (std::same_as<T, SemAwait>) {
                return T {.operand = copy(node.operand), .operand_kind = node.operand_kind};
            } else if constexpr (std::same_as<T, SemClosure>) {
                return T {.callable = node.callable, .captures = copy(node.captures)};
            } else if constexpr (std::same_as<T, SemIf>) {
                return T {
                    .branches = copy(node.branches),
                    .otherwise = copy(node.otherwise),
                    .is_static = node.is_static
                };
            } else if constexpr (std::same_as<T, SemMatch>) {
                return T {
                    .subject = copy(node.subject),
                    .subject_is_place = node.subject_is_place,
                    .arms = copy(node.arms)
                };
            } else if constexpr (std::same_as<T, SemTry>) {
                return T {
                    .body = copy(node.body),
                    .protected_failures = node.protected_failures,
                    .residual_failures = node.residual_failures,
                    .arms = copy(node.arms)
                };
            } else if constexpr (std::same_as<T, SemReturn>) {
                return T {.value = copy(node.value)};
            } else if constexpr (std::same_as<T, SemThrow>) {
                return T {.value = copy(node.value), .failure_type = node.failure_type};
            } else if constexpr (std::same_as<T, SemExpressionStatement>) {
                return T {.expression = copy(node.expression)};
            } else if constexpr (std::same_as<T, SemInitialize>) {
                return T {.binding = node.binding, .initializer = copy(node.initializer)};
            } else if constexpr (std::same_as<T, SemDecompose>) {
                return T {
                    .owner = node.owner,
                    .initializer = copy(node.initializer),
                    .bindings = node.bindings
                };
            } else if constexpr (std::same_as<T, SemAsyncLet>) {
                return T {.child = node.child, .initializer = copy(node.initializer)};
            } else if constexpr (std::same_as<T, SemStaticBinding>) {
                return T {.binding = node.binding, .initializer = copy(node.initializer)};
            } else if constexpr (std::same_as<T, SemConstBlock>) {
                return T {.label = node.label, .source = node.source, .region = copy(node.region)};
            } else if constexpr (std::same_as<T, SemAssign>) {
                return T {
                    .target = copy(node.target),
                    .compound = node.compound,
                    .value = copy(node.value)
                };
            } else if constexpr (std::same_as<T, SemLoop>) {
                return T {
                    .initializer = copy(node.initializer),
                    .condition = copy(node.condition),
                    .body = copy(node.body),
                    .steps = copy(node.steps)
                };
            } else if constexpr (std::same_as<T, SemRangeLoop>) {
                return T {
                    .lifetime = node.lifetime,
                    .access = node.access,
                    .binding = node.binding,
                    .source = copy(node.source),
                    .body = copy(node.body),
                    .is_static = node.is_static
                };
            } else if constexpr (std::same_as<T, SemExpandedLoop>) {
                return T {.iterations = copy(node.iterations)};
            } else if constexpr (std::same_as<T, OwnedSemanticRegion>) {
                return copy(node);
            } else {
                static_assert(
                    std::same_as<T, SemDefault>
                    || std::same_as<T, SemConstant>
                    || std::same_as<T, SemUnreachable>
                    || std::same_as<T, SemAsyncIntrinsic>
                    || std::same_as<T, SemWaitReady>
                    || std::same_as<T, SemCloseChild>
                    || std::same_as<T, SemBinding>
                    || std::same_as<T, SemCallable>
                    || std::same_as<T, SemEnumConstructor>
                    || std::same_as<T, SemBreak>
                    || std::same_as<T, SemContinue>
                    || std::same_as<T, SemRethrow>
                );
                return node;
            }
        });
    }

    std::vector<std::variant<
        NodePair<SemanticExpression>,
        NodePair<SemanticStatement>,
        NodePair<SemanticRegion>>>
        pending;
};

auto SemanticClone::copy(const SemanticExpression& value) noexcept -> SemanticExpression {
    return {
        .type = value.type,
        .lifetime = value.lifetime,
        .origin = value.origin,
        .constant = value.constant,
        .failures = value.failures,
        .exits_test = value.exits_test,
        .operation_reachable = value.operation_reachable,
        .category = value.category,
        .value = SemDefault {},
    };
}

auto SemanticClone::copy(const SemanticStatement& value) noexcept -> SemanticStatement {
    return {
        .origin = value.origin,
        .lifetime = value.lifetime,
        .reachable = value.reachable,
        .value = SemBreak {},
    };
}

auto SemanticClone::copy(const SemanticRegion& value) noexcept -> SemanticRegion {
    return {
        .lifetime = value.lifetime,
        .origin = value.origin,
        .statements = copy(value.statements),
        .result = copy(value.result),
        .result_reachable = value.result_reachable,
        .failures = value.failures,
        .exits_test = value.exits_test,
    };
}

auto SemanticClone::copy(const OwnedSemanticExpression& value) noexcept -> OwnedSemanticExpression {
    return OwnedSemanticExpression(copy(*value));
}

auto SemanticClone::copy(const OwnedSemanticRegion& value) noexcept -> OwnedSemanticRegion {
    return OwnedSemanticRegion(copy(*value));
}

auto SemanticClone::copy(const SemCallArgument& value) noexcept -> SemCallArgument {
    return {.access = value.access, .expression = copy(value.expression)};
}

auto SemanticClone::copy(const SemCapture& value) noexcept -> SemCapture {
    return {.mode = value.mode, .expression = copy(value.expression)};
}

auto SemanticClone::copy(const SemFieldInitializer& value) noexcept -> SemFieldInitializer {
    return {.declaration_index = value.declaration_index, .value = copy(value.value)};
}

auto SemanticClone::copy(const SemConditionalBranch& value) noexcept -> SemConditionalBranch {
    return {.condition = copy(value.condition), .body = copy(value.body)};
}

auto SemanticClone::copy(const SemPatternBounds& value) noexcept -> SemPatternBounds {
    return {.pattern = value.pattern, .begin = copy(value.begin), .end = copy(value.end)};
}

auto SemanticClone::copy(const SemMatchArm& value) noexcept -> SemMatchArm {
    return {
        .pattern = value.pattern,
        .bindings = value.bindings,
        .guard = copy(value.guard),
        .body = copy(value.body),
        .reachable = value.reachable,
        .pattern_may_reject = value.pattern_may_reject,
        .pattern_bounds = copy(value.pattern_bounds)
    };
}

auto SemanticClone::copy(const SemCatchArm& value) noexcept -> SemCatchArm {
    return {
        .origin = value.origin,
        .accepted_failures = value.accepted_failures,
        .alternatives = value.alternatives,
        .bindings = value.bindings,
        .guard = copy(value.guard),
        .body = copy(value.body),
        .pattern_bounds = copy(value.pattern_bounds)
    };
}

auto SemanticClone::copy(const SemCppOperand& value) noexcept -> SemCppOperand {
    return {.access = value.access, .expression = copy(value.expression)};
}

} // namespace

auto SemanticExpressionCleanup::copy(const SemanticExpressionValue& value) noexcept
    -> SemanticExpressionValue {
    return SemanticClone().tree(value);
}

auto SemanticStatementCleanup::copy(const SemanticStatementValue& value) noexcept
    -> SemanticStatementValue {
    return SemanticClone().tree(value);
}
