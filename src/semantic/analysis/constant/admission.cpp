module carven:semantic.analysis.constant.admission.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.constant.admission;
import :semantic.evaluation.shape;
import :semantic.semir.body;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.traversal;
import :semantic.semir.type;
import :support.visit;
import std;

namespace {

class ConstantBodyAdmission final {
public:
    ConstantBodyAdmission(
        ProgramDraft& draft,
        std::optional<FunctionID> function,
        const StructuredBodyDraft& body
    ) noexcept;
    auto run() noexcept -> AnalysisResult<void>;

private:
    auto reject(ProgramOriginID origin, std::string message) noexcept -> void;
    auto supported_type(ConstructionTypeRef type, bool allow_void = false) const noexcept -> bool;
    auto expression(const SemanticExpression& source) noexcept -> void;
    auto statement(const SemanticStatement& source) noexcept -> void;
    auto call(const SemCall& value, ProgramOriginID origin) noexcept -> void;

    ConstantTypeShapes shapes;
    ProgramDraft& draft;
    std::optional<FunctionID> function;
    const StructuredBodyDraft& body;
    std::optional<AnalysisFailure> failure;
    std::set<const SemanticExpression*> direct_callees;
};

ConstantBodyAdmission::ConstantBodyAdmission(
    ProgramDraft& draft,
    std::optional<FunctionID> function,
    const StructuredBodyDraft& body
) noexcept
    : shapes(draft),
      draft(draft),
      function(function),
      body(body) {}

auto ConstantBodyAdmission::reject(ProgramOriginID origin, std::string message) noexcept -> void {
    if (failure) {
        return;
    }
    auto diagnostic = DiagnosticBuilder(DiagnosticCode::ConstAdmission, std::move(message));
    diagnostic.primary(draft.source_span(origin));
    failure = draft.diagnostics().error(diagnostic.build());
}

auto ConstantBodyAdmission::supported_type(ConstructionTypeRef type, bool allow_void) const noexcept
    -> bool {
    const auto* id = std::get_if<TypeID>(&type);
    if (id == nullptr) {
        return false;
    }
    const auto canonical = draft.type_copy(*id);

    if (std::holds_alternative<ArrayTypeValue>(canonical.value)
        || std::holds_alternative<StructTypeValue>(canonical.value)) {
        const auto shape = shapes.get(*id);
        return shape && shape->supported;
    }
    const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value);
    return builtin != nullptr
        && (builtin_is_integer(builtin->kind)
            || builtin->kind == BuiltinType::Bool
            || builtin->kind == BuiltinType::Char
            || builtin->kind == BuiltinType::Str
            || builtin->kind == BuiltinType::String
            || (allow_void && builtin->kind == BuiltinType::Void));
}

auto ConstantBodyAdmission::run() noexcept -> AnalysisResult<void> {
    if (function) {
        const auto declaration = draft.function_declaration_copy(*function);
        if (!declaration.is_const) {
            return {};
        }
        if (draft.pending_function_contract_copy(declaration.callable)) {
            reject(declaration.origin, "const fn requires a completed concrete signature");
            return std::unexpected(*failure);
        }
        const auto contract = draft.construction_callable_contract_copy(declaration.callable);
        if (!supported_type(contract.result, true)) {
            reject(
                declaration.origin,
                "const fn result requires a supported scalar, text, aggregate, or void type"
            );
        }
        for (const auto& parameter : contract.parameters) {
            if (parameter.access == AccessMode::Write) {
                reject(declaration.origin, "const fn parameters cannot use Write access");
            } else if (!supported_type(parameter.type)) {
                reject(
                    declaration.origin,
                    "const fn parameters require supported scalar, text, or aggregate values"
                );
            }
        }
        if (!draft.construction_failure_term_copy(contract.failures).direct_members.empty()) {
            reject(declaration.origin, "const fn cannot declare typed failures");
        }
    }
    for (const auto entry : body.bindings.entries()) {
        if (!supported_type(entry.value.type)) {
            reject(
                entry.value.origin,
                "constant execution local values require supported scalar, text, or aggregate types"
            );
        }
    }
    for (const auto entry : body.patterns.entries()) {
        if (!supported_type(entry.value.type)
            || !(
                std::holds_alternative<WildcardPattern>(entry.value.value)
                || std::holds_alternative<LiteralPattern>(entry.value.value)
                || std::holds_alternative<BindingPattern>(entry.value.value)
                || std::holds_alternative<OrPattern>(entry.value.value)
            )) {
            reject(
                entry.value.origin,
                "constant execution match requires supported values and literal, binding, wildcard, or alternative patterns"
            );
        }
    }
    visit_semantic_nodes(
        body.region,
        Overloaded {
            [&](const SemanticExpression& source) noexcept { expression(source); },
            [&](const SemanticStatement& source) noexcept { statement(source); },
        }
    );
    return failure ? AnalysisResult<void>(std::unexpected(*failure)) : AnalysisResult<void>();
}

auto ConstantBodyAdmission::call(const SemCall& value, ProgramOriginID origin) noexcept -> void {
    const auto* selected = std::get_if<SemCallable>(&value.callee->value);
    const auto callee =
        selected == nullptr ? std::nullopt : draft.function_for_callable(selected->callable);
    if (!callee || !draft.function_declaration_copy(*callee).is_const) {
        reject(origin, "constant execution calls must directly select a const fn");
        return;
    }
    direct_callees.insert(std::addressof(*value.callee));
    for (const auto& argument : value.arguments) {
        if (argument.access == AccessMode::Write) {
            reject(
                argument.expression.origin,
                "constant execution calls cannot pass Write arguments"
            );
        }
    }
}

auto ConstantBodyAdmission::expression(const SemanticExpression& source) noexcept -> void {
    if (failure) {
        return;
    }
    if (std::holds_alternative<SemCallable>(source.value)) {
        if (!direct_callees.contains(std::addressof(source))) {
            reject(source.origin, "constant execution cannot form callable values");
        }
        return;
    }
    if (!supported_type(source.type.construction(), true)) {
        reject(source.origin, "expression type is not supported in constant execution");
        return;
    }
    std::visit(
        [&](const auto& value) noexcept {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, SemConstant>
                          || std::same_as<Value, SemBinding>
                          || std::same_as<Value, SemStruct>
                          || std::same_as<Value, SemField>
                          || std::same_as<Value, SemArray>
                          || std::same_as<Value, SemIndex>
                          || std::same_as<Value, SemUnary>
                          || std::same_as<Value, SemBinary>
                          || std::same_as<Value, SemShortCircuit>
                          || std::same_as<Value, SemFormat>
                          || std::same_as<Value, SemPrint>
                          || std::same_as<Value, SemTestReport>
                          || std::same_as<Value, SemTake>
                          || std::same_as<Value, SemIf>
                          || std::same_as<Value, SemMatch>) {
                return;
            } else if constexpr (std::same_as<Value, SemCast>) {
                if (value.kind != CastKind::Identity
                    && value.kind != CastKind::IntegerToInteger
                    && value.kind != CastKind::IntegerToBool
                    && value.kind != CastKind::BoolToInteger
                    && value.kind != CastKind::CharToU32) {
                    reject(source.origin, "cast is not supported in constant execution");
                }
            } else if constexpr (std::same_as<Value, SemCall>) {
                call(value, source.origin);
            } else if constexpr (std::same_as<Value, SemTextIntrinsic>) {
                switch (value.intrinsic) {
                    case TextIntrinsic::New:
                    case TextIntrinsic::FromStr:
                    case TextIntrinsic::AsStr:
                    case TextIntrinsic::Len:
                    case TextIntrinsic::IsEmpty:
                    case TextIntrinsic::Append:
                    case TextIntrinsic::Push:
                    case TextIntrinsic::Clear:             return;
                    case TextIntrinsic::Bytes:
                    case TextIntrinsic::Chars:
                    case TextIntrinsic::FromUTF8Unchecked:
                    case TextIntrinsic::FromU32Unchecked:
                        reject(
                            source.origin,
                            "text operation is not supported in constant execution"
                        );
                        return;
                }
            } else {
                reject(source.origin, "operation is not supported in constant execution");
            }
        },
        source.value
    );
}

auto ConstantBodyAdmission::statement(const SemanticStatement& source) noexcept -> void {
    if (failure) {
        return;
    }
    std::visit(
        [&](const auto& value) noexcept {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, SemReturn>
                          || std::same_as<Value, SemBreak>
                          || std::same_as<Value, SemContinue>
                          || std::same_as<Value, SemExpressionStatement>
                          || std::same_as<Value, SemInitialize>
                          || std::same_as<Value, SemLoop>
                          || std::same_as<Value, SemRangeLoop>
                          || std::same_as<Value, OwnedSemanticRegion>) {
                return;
            } else if constexpr (std::same_as<Value, SemAssign>) {
                auto* target = &value.target;
                while (true) {
                    if (const auto* index = std::get_if<SemIndex>(&target->value)) {
                        target = &*index->source;
                    } else if (const auto* field = std::get_if<SemField>(&target->value)) {
                        target = &*field->source;
                    } else {
                        break;
                    }
                }
                if (!std::holds_alternative<SemBinding>(target->value)) {
                    reject(source.origin, "constant execution assignment requires local storage");
                }
            } else {
                reject(source.origin, "control operation is not supported in constant execution");
            }
        },
        source.value
    );
}

} // namespace

auto validate_constant_function(
    ProgramDraft& draft,
    FunctionID function,
    const StructuredBodyDraft& body
) noexcept -> AnalysisResult<void> {
    return ConstantBodyAdmission(draft, function, body).run();
}

auto validate_constant_test(ProgramDraft& draft, const StructuredBodyDraft& body) noexcept
    -> AnalysisResult<void> {
    return ConstantBodyAdmission(draft, std::nullopt, body).run();
}
