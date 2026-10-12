module carven:semantic.analysis.ownership.contracts.impl;

import :semantic.analysis.ownership.context;
import std;

auto check_ownership_contracts(
    const AnalysisModelView& model,
    const AnalysisBodyView& body
) noexcept -> std::optional<OwnershipRecordedError> {
    auto error = std::optional<OwnershipRecordedError>();
    const auto diagnose = [&](DiagnosticCode code,
                              std::string message,
                              ProgramOriginID origin,
                              std::optional<ProgramOriginID> related = std::nullopt,
                              std::string related_label = "related storage or access",
                              std::string help = {}) noexcept {
        if (!error) {
            error = OwnershipRecordedError {
                .code = code,
                .message = std::move(message),
                .origin = origin,
                .related = related,
                .related_label = std::move(related_label),
                .help = std::move(help)
            };
        }
    };
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
        if (const auto* tuple = std::get_if<SemTupleElement>(&source.value)) {
            return self(*tuple->source);
        }
        if (const auto* field = std::get_if<SemField>(&source.value)) {
            return self(*field->source);
        }
        if (const auto* index = std::get_if<SemIndex>(&source.value)) {
            return self(*index->source);
        }
        return std::nullopt;
    };
    const auto writable = [&](this const auto& self,
                              const SemanticExpression& source) noexcept -> bool {
        if (const auto* dereference = std::get_if<SemDereference>(&source.value)) {
            const auto* pointer = std::get_if<AnalysisPointerType>(
                &model.shape(dereference->source->type.construction())
            );
            return pointer != nullptr && pointer->access == PointerAccess::Write;
        }
        if (const auto* tuple = std::get_if<SemTupleElement>(&source.value)) {
            return self(*tuple->source);
        }
        if (const auto* field = std::get_if<SemField>(&source.value)) {
            return self(*field->source);
        }
        if (const auto* index = std::get_if<SemIndex>(&source.value)) {
            if (std::holds_alternative<ConstructionSliceTypeValue>(
                    model.shape(index->source->type.construction())
                )) {
                return false;
            }
            return self(*index->source);
        }
        if (const auto* cpp = std::get_if<SemCpp>(&source.value);
            cpp && source.category == SemanticValueCategory::Place) {
            return self(cpp->operands.front().expression);
        }
        const auto id = root(source);
        return id
            && body.binding(*id).storage.visit(
                Overloaded {
                    [](const OwnerBindingStorage& value) static noexcept { return value.writable; },
                    [](const ParameterBindingStorage& value) static noexcept {
                        return value.access == AccessMode::Write;
                    },
                    [](const AsyncChildBindingStorage&) static noexcept { return false; },
                    [](const CaptureBindingStorage& value) static noexcept {
                        return value.mode == CaptureMode::Write;
                    },
                    [](const AccessBindingStorage& value) static noexcept {
                        return value.access == AccessMode::Write;
                    },
                }
            );
    };
    const auto write = [&](const SemanticExpression& source) noexcept {
        if (!root(source) && model.contents(source.type.construction()).contains_callable_view) {
            diagnose(
                DiagnosticCode::TypeCallableViewEscape,
                "indirect storage cannot carry a tracked Carven callable borrow",
                source.origin
            );
        }
        if (!writable(source)) {
            const auto binding = root(source);
            if (!binding) {
                diagnose(
                    DiagnosticCode::AccessImmutable,
                    "Write requires writable storage",
                    source.origin
                );
                return;
            }
            const auto& declaration = body.binding(*binding);
            const auto name = model.program.provenance().spelling(declaration.name);
            const auto parameter =
                std::holds_alternative<ParameterBindingStorage>(declaration.storage);
            diagnose(
                DiagnosticCode::AccessImmutable,
                std::format("Write requires writable storage; '{}' is read-only", name),
                source.origin,
                declaration.origin,
                std::format("'{}' is declared read-only here", name),
                parameter ? "mark the parameter '&' to receive writable storage"
                    : std::holds_alternative<OwnerBindingStorage>(declaration.storage)
                    ? "declare the binding with 'var' to make it writable"
                    : ""
            );
        }
    };
    visit_semantic_nodes(
        body.region(),
        Overloaded {
            [&](const SemanticExpression& source) noexcept {
                source.value.visit(
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
                        [&](const SemFormat& value) noexcept {
                            if (value.receiver) {
                                write(**value.receiver);
                            }
                        },
                        [&](const SemIntrinsic& value) noexcept {
                            for (const auto& operand : value.operands) {
                                if (operand.access == AccessMode::Write) {
                                    write(operand.expression);
                                }
                            }
                        },
                        [&](const SemCall& value) noexcept {
                            for (const auto& argument : value.arguments) {
                                if (argument.access == AccessMode::Write) {
                                    write(argument.expression);
                                }
                            }
                        },
                        [&](const SemGenericCall& value) noexcept {
                            for (const auto& argument : value.arguments) {
                                if (argument.access == AccessMode::Write) {
                                    write(argument.expression);
                                }
                            }
                        },
                        [&](const SemColdCall& value) noexcept {
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
                    }
                );
            },
            [&](const SemanticStatement& source) noexcept {
                if (const auto* value = std::get_if<SemAssign>(&source.value)) {
                    write(value->target);
                    if (const auto* take = std::get_if<SemTake>(&value->value.value);
                        take != nullptr
                        && root(value->target).has_value()
                        && root(*take->place) == root(value->target)) {
                        diagnose(
                            DiagnosticCode::AccessOperationConflict,
                            "direct self-transfer assignment is invalid",
                            source.origin
                        );
                    }
                }
                if (const auto* value = std::get_if<SemReturn>(&source.value); value != nullptr
                    && value->value.has_value()
                    && model.contents(value->value->type.construction()).contains_callable_view) {
                    diagnose(
                        DiagnosticCode::TypeCallableViewEscape,
                        "callable view cannot be returned",
                        source.origin
                    );
                }
            },
        }
    );
    return error;
}
