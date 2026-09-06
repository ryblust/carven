module carven:semantic.analysis.ownership.contracts.impl;

import :semantic.analysis.ownership.context;
import std;

namespace ownership {

auto BodyAnalyzer::check_contracts() noexcept -> void {
    const auto root = [&](this const auto& self,
                          const SemIRExpression& source) noexcept -> std::optional<LocalBindingID> {
        if (const auto* binding = std::get_if<SemBinding>(&source.value)) {
            return binding->binding;
        }
        if (const auto* foreign = std::get_if<SemCpp<TypeID, FailureSetID>>(&source.value);
            foreign != nullptr && source.category == SemanticValueCategory::Place) {
            return self(foreign->operands.front().expression);
        }
        if (const auto* field = std::get_if<SemField<TypeID, FailureSetID>>(&source.value)) {
            return self(*field->source);
        }
        if (const auto* index = std::get_if<SemIndex<TypeID, FailureSetID>>(&source.value)) {
            return self(*index->source);
        }
        return std::nullopt;
    };
    const auto write = [&](const SemIRExpression& source) noexcept {
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
            [&](const SemIRExpression& source) noexcept {
                std::visit(
                    Overloaded {
                        [&](const SemTake<TypeID, FailureSetID>& value) noexcept {
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
                        [&](const SemCpp<TypeID, FailureSetID>& value) noexcept {
                            for (const auto& operand : value.operands) {
                                if (operand.access == AccessMode::Write) {
                                    write(operand.expression);
                                }
                            }
                        },
                        [&](const SemCall<TypeID, FailureSetID>& value) noexcept {
                            for (const auto& argument : value.arguments) {
                                if (argument.access == AccessMode::Write) {
                                    write(argument.expression);
                                }
                            }
                        },
                        [&](const SemClosure<TypeID, FailureSetID>& value) noexcept {
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
            [&](const SemIRStatement& source) noexcept {
                if (const auto* value =
                        std::get_if<SemAssign<TypeID, FailureSetID>>(&source.value)) {
                    write(value->target);
                    if (const auto* take =
                            std::get_if<SemTake<TypeID, FailureSetID>>(&value->value.value);
                        take != nullptr && root(*take->place) == root(value->target)) {
                        diagnose(
                            DiagnosticCode::AccessOperationConflict,
                            "direct self-transfer assignment is invalid",
                            source.origin
                        );
                    }
                }
                if (const auto* value = std::get_if<SemReturn<TypeID, FailureSetID>>(&source.value);
                    value != nullptr
                    && value->value.has_value()
                    && type_contents.contains_view(value->value->type)) {
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
