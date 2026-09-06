module carven:semantic.analysis.ownership.contracts.impl;

import :semantic.analysis.ownership.context;
import std;

namespace ownership {

auto BodyAnalyzer::check_contracts() noexcept -> void {
    const auto root =
        [&](this const auto& self,
            const SemanticExpression& source) noexcept -> std::optional<LocalBindingID> {
        if (const auto* binding = std::get_if<SemBinding>(&source.value)) {
            return binding->binding;
        }
        if (const auto* foreign = std::get_if<SemCpp>(&source.value);
            foreign != nullptr && source.category == SemanticValueCategory::Place) {
            return self(foreign->operands.front().expression);
        }
        if (const auto* field = std::get_if<SemField>(&source.value)) {
            return self(*field->source);
        }
        if (const auto* index = std::get_if<SemIndex>(&source.value)) {
            return self(*index->source);
        }
        return std::nullopt;
    };
    const auto write = [&](const SemanticExpression& source) noexcept {
        const auto id = root(source);
        if (!id.has_value() || !is_writable(*id)) {
            diagnose(
                DiagnosticCode::AccessImmutable,
                "Write requires writable storage",
                source.origin
            );
        }
    };
    visit_semantic_nodes(
        body.region(),
        Overloaded {
            [&](const SemanticExpression& source) noexcept {
                std::visit(
                    Overloaded {
                        [&](const SemTake& value) noexcept {
                            const auto* binding = std::get_if<SemBinding>(&value.place->value);
                            if (binding == nullptr) {
                                diagnose(
                                    DiagnosticCode::AccessTakeOperand,
                                    "Take requires a complete owner",
                                    source.origin
                                );
                                return;
                            }
                            const auto& storage = body.binding(binding->binding).storage;
                            const auto* parameter = std::get_if<ParameterBindingStorage>(&storage);
                            if (!std::holds_alternative<OwnerBindingStorage>(storage)
                                && (parameter == nullptr
                                    || parameter->access != AccessMode::Take)) {
                                diagnose(
                                    DiagnosticCode::AccessTakeOperand,
                                    "Take requires an owner",
                                    source.origin
                                );
                            }
                        },
                        [&](const SemCpp& value) noexcept {
                            visit_cpp_operands(
                                value,
                                [&](AccessMode access, const SemanticExpression& operand) noexcept {
                                    if (access == AccessMode::Write) {
                                        write(operand);
                                    }
                                }
                            );
                        },
                        [&](const SemCppCall& value) noexcept {
                            visit_cpp_operands(
                                value,
                                [&](AccessMode access, const SemanticExpression& operand) noexcept {
                                    if (access == AccessMode::Write) {
                                        write(operand);
                                    }
                                }
                            );
                        },
                        [&](const SemCall& value) noexcept {
                            for (const auto& argument : value.arguments) {
                                if (argument.access == AccessMode::Write) {
                                    write(argument.expression);
                                }
                            }
                        },
                        [&](const SemClosure& value) noexcept {
                            for (const auto& capture : value.captures) {
                                if (capture.mode == CaptureMode::Write) {
                                    write(capture.expression);
                                }
                            }
                        },
                        [](const auto&) static noexcept {},
                    },
                    source.value
                );
            },
            [&](const SemanticStatement& source) noexcept {
                if (const auto* value = std::get_if<SemAssign>(&source.value)) {
                    write(value->target);
                    if (const auto* take = std::get_if<SemTake>(&value->value.value);
                        take != nullptr && root(*take->place) == root(value->target)) {
                        diagnose(
                            DiagnosticCode::AccessOperationConflict,
                            "direct self-transfer assignment is invalid",
                            source.origin
                        );
                    }
                }
                if (const auto* value = std::get_if<SemReturn>(&source.value); value != nullptr
                    && value->value.has_value()
                    && analysis.contents(value->value->type.resolved()).callable_view) {
                    diagnose(
                        DiagnosticCode::TypeCallableViewEscape,
                        "callable view cannot be returned",
                        source.origin
                    );
                }
            },
        }
    );
}

} // namespace ownership
