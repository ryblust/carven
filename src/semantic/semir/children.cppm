module carven:semantic.semir.children;

import :semantic.semir.structured;
import :support.visit;
import std;

template<typename Owner, typename Value>
using SemanticChildNode = std::conditional_t<std::is_const_v<Owner>, const Value, Value>;

// Visits direct stored expressions and regions in source order, including inactive
// branches. The caller owns recursion and execution selection. Borrows end when
// the containing operation is moved or its child storage is changed.
template<typename Operation, typename Visitor>
auto visit_semantic_edges(Operation& operation, Visitor visitor) noexcept -> void {
    if constexpr (std::same_as<std::remove_const_t<Operation>, SemanticExpressionValue>
                  || std::same_as<std::remove_const_t<Operation>, SemanticStatementValue>) {
        operation.visit([&](auto& node) noexcept { visit_semantic_edges(node, visitor); });
    } else {
        const auto child = [&](auto& value) noexcept {
            std::invoke(visitor, value);
        };
        if constexpr (std::same_as<std::remove_const_t<Operation>, SemDefault>
                      || std::same_as<std::remove_const_t<Operation>, SemConstant>) {
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemBinding>) {
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemCallable>) {
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemEnumConstructor>) {
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemCpp>) {
            auto& value = operation;
            for (auto& operand : value.operands) {
                child(operand.expression);
            }
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemCppCall>) {
            auto& value = operation;
            value.callee.visit(
                Overloaded {
                    [](SemanticChildNode<Operation, CppNameReference>&) static noexcept {},
                    [&](
                        SemanticChildNode<Operation, CppMemberCallee<SemCppOperand>>& callee
                    ) noexcept { child(callee.receiver.expression); },
                    [&](SemanticChildNode<Operation, SemCppOperand>& callee) noexcept {
                        child(callee.expression);
                    },
                }
            );
            for (auto& argument : value.arguments) {
                child(argument.expression);
            }
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemRange>) {
            child(operation.begin);
            child(operation.end);
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemArray>) {
            auto& value = operation;
            for (auto& element : value.elements) {
                child(element);
            }
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemArrayAdopt>) {
            auto& value = operation;
            child(value.source);
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemStruct>) {
            auto& value = operation;
            for (auto& field : value.fields) {
                child(field.value);
            }
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemEnumCase>) {
            auto& value = operation;
            for (auto& element : value.payload) {
                child(element);
            }
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemUnary>) {
            auto& value = operation;
            child(value.operand);
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemBinary>) {
            auto& value = operation;
            child(value.left);
            child(value.right);
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemShortCircuit>) {
            auto& value = operation;
            child(value.left);
            child(value.right);
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemCast>) {
            auto& value = operation;
            child(value.operand);
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemDereference>) {
            auto& value = operation;
            child(value.source);
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemField>) {
            auto& value = operation;
            child(value.source);
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemIndex>) {
            auto& value = operation;
            child(value.source);
            child(value.index);
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemTestReport>) {
            auto& value = operation;
            if (value.condition.has_value()) {
                child(*value.condition);
            }
            if (value.message.has_value()) {
                child(*value.message);
            }
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemPrint>) {
            auto& value = operation;
            for (auto& operand : value.operands) {
                child(operand.expression);
            }
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemFormat>) {
            auto& value = operation;
            if (value.receiver) {
                child(*value.receiver);
            }
            for (auto& operand : value.operands) {
                child(operand.expression);
            }
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemSliceIntrinsic>) {
            auto& value = operation;
            for (auto& operand : value.operands) {
                child(operand.expression);
            }
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemTextIntrinsic>) {
            auto& value = operation;
            for (auto& operand : value.operands) {
                child(operand.expression);
            }
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemCall>) {
            auto& value = operation;
            child(value.callee);
            for (auto& argument : value.arguments) {
                child(argument.expression);
            }
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemClosure>) {
            auto& value = operation;
            for (auto& capture : value.captures) {
                child(capture.expression);
            }
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemBorrowCallable>) {
            auto& value = operation;
            child(value.source);
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemTake>) {
            auto& value = operation;
            child(value.place);
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemPropagate>) {
            auto& value = operation;
            child(value.operand);
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemIf>) {
            auto& value = operation;
            for (auto& branch : value.branches) {
                child(branch.condition);
                child(branch.body);
            }
            if (value.otherwise.has_value()) {
                child(*value.otherwise);
            }
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemMatch>) {
            auto& value = operation;
            child(value.subject);
            for (auto& arm : value.arms) {
                for (auto& range : arm.pattern_bounds) {
                    if (range.begin) {
                        child(*range.begin);
                    }
                    if (range.end) {
                        child(*range.end);
                    }
                }
                if (arm.guard.has_value()) {
                    child(*arm.guard);
                }
                child(arm.body);
            }
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemTry>) {
            auto& value = operation;
            child(value.body);
            for (auto& arm : value.arms) {
                for (auto& range : arm.pattern_bounds) {
                    if (range.begin) {
                        child(*range.begin);
                    }
                    if (range.end) {
                        child(*range.end);
                    }
                }
                if (arm.guard.has_value()) {
                    child(*arm.guard);
                }
                child(arm.body);
            }
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemanticRegion>) {
            for (auto& statement : operation.statements) {
                child(statement);
            }
            if (operation.result) {
                child(*operation.result);
            }
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemReturn>) {
            if (operation.value) {
                child(*operation.value);
            }
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemBreak>
                             || std::same_as<std::remove_const_t<Operation>, SemContinue>
                             || std::same_as<std::remove_const_t<Operation>, SemRethrow>) {
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemThrow>) {
            child(operation.value);
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemExpressionStatement>) {
            child(operation.expression);
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemInitialize>) {
            child(operation.initializer);
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemAssign>) {
            child(operation.target);
            child(operation.value);
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemLoop>) {
            child(operation.initializer);
            if (operation.condition) {
                child(*operation.condition);
            }
            child(operation.body);
            child(operation.steps);
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, SemRangeLoop>) {
            child(operation.source);
            child(operation.body);
        } else if constexpr (std::same_as<std::remove_const_t<Operation>, OwnedSemanticRegion>) {
            child(operation);
        } else {
            static_assert(sizeof(Operation) == 0, "semantic child structure is undefined");
        }
    }
}

// Semantic consumers still require every mandatory owning edge to be live.
template<typename Operation, typename Visitor>
auto visit_semantic_children(Operation& operation, Visitor visitor) noexcept -> void {
    visit_semantic_edges(operation, [&](auto& edge) noexcept {
        using Edge = std::remove_cvref_t<decltype(edge)>;
        if constexpr (std::same_as<Edge, OwnedSemanticExpression>
                      || std::same_as<Edge, OwnedSemanticRegion>) {
            std::invoke(visitor, *edge);
        } else {
            std::invoke(visitor, edge);
        }
    });
}
