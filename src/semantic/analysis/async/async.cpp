module carven:semantic.analysis.async.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.async;
import :semantic.analysis.model;
import :semantic.analysis.types.standard;
import :semantic.semir.children;
import :semantic.semir.decl;
import :semantic.semir.traversal;
import :source.text;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto resolve_timeout_contract(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ConstructionRequests& requests,
    ProgramModuleID module,
    Span span
) noexcept -> AnalysisTask<TimeoutContract> {
    auto types = std::vector<TypeID>();
    for (const auto record :
         {StandardRecord::Duration, StandardRecord::Timeout, StandardRecord::TimeRangeError}) {
        auto type =
            co_await resolve_standard_record(draft, catalog, requests, module, span, record);
        if (!type) {
            co_return std::unexpected(type.error());
        }
        types.push_back(*type);
    }
    co_return TimeoutContract {types[0], types[1], types[2]};
}

} // namespace

auto find_async_builtin(std::string_view name) noexcept -> std::optional<AsyncBuiltin> {
    if (name == "cancellation_requested") {
        return AsyncIntrinsic::CancellationRequested;
    }
    if (name == "cancel_point") {
        return AsyncIntrinsic::CancelPoint;
    }
    if (name == "yield_once") {
        return AsyncIntrinsic::YieldOnce;
    }
    if (name == "wait_ready") {
        return AsyncIntrinsic::WaitReady;
    }
    if (name == "close") {
        return AsyncIntrinsic::CloseChild;
    }
    if (name == "when_all") {
        return AsyncCompositionPolicy::WhenAll;
    }
    if (name == "when_any") {
        return AsyncCompositionPolicy::WhenAny;
    }
    if (name == "first_successful") {
        return AsyncCompositionPolicy::FirstSuccessful;
    }
    if (name == "timeout") {
        return TimeoutBuiltin {};
    }
    return std::nullopt;
}

auto resolve_async_builtin(
    ProgramDraft& draft,
    AnalysisCatalogView catalog,
    ConstructionRequests& requests,
    ProgramModuleID module,
    AsyncBuiltin builtin,
    Span span
) noexcept -> AnalysisTask<ResolvedStaticCallable> {
    if (const auto* intrinsic = std::get_if<AsyncIntrinsic>(&builtin)) {
        co_return *intrinsic;
    }
    if (const auto* policy = std::get_if<AsyncCompositionPolicy>(&builtin)) {
        co_return *policy;
    }
    auto contract = co_await resolve_timeout_contract(draft, catalog, requests, module, span);
    if (!contract) {
        co_return std::unexpected(contract.error());
    }
    co_return *contract;
}

namespace {

enum class ValueAdmission { Satisfied, Rejected, Dependent };

auto scalar(const AnalysisModelView& model, ConstructionTypeRef type) noexcept -> ValueAdmission {
    const auto& shape = model.shape(type);
    if ((std::holds_alternative<GenericTypeParameter>(shape))) {
        return ValueAdmission::Dependent;
    }
    const auto* builtin = std::get_if<BuiltinTypeValue>(&shape);
    return builtin
            && (builtin_is_numeric(builtin->kind)
                || builtin->kind == BuiltinType::Bool
                || builtin->kind == BuiltinType::Char)
        ? ValueAdmission::Satisfied
        : ValueAdmission::Rejected;
}

// Read snapshots and owning values have different source storage contracts.
// Native payloads use the provider's ordinary interop contract.
auto admitted_value(
    const AnalysisModelView& model,
    ConstructionTypeRef type,
    bool owning,
    bool native_payload = false,
    bool operation_input = false
) noexcept -> ValueAdmission {
    auto pending = std::vector<ConstructionTypeRef> {type};
    auto seen = std::set<ConstructionTypeRef>();
    auto result = ValueAdmission::Satisfied;
    while (!pending.empty()) {
        const auto next = pending.back();
        pending.pop_back();
        if (!seen.insert(next).second || scalar(model, next) == ValueAdmission::Satisfied) {
            continue;
        }
        const auto& value = model.shape(next);
        if ((std::holds_alternative<GenericTypeParameter>(value))) {
            result = ValueAdmission::Dependent;
            continue;
        }
        if (const auto* builtin = std::get_if<BuiltinTypeValue>(&value)) {
            if (builtin->kind == BuiltinType::Void
                || (owning && builtin->kind == BuiltinType::String)) {
                continue;
            }
            return ValueAdmission::Rejected;
        }
        if (const auto* structure = std::get_if<StructTypeValue>(&value)) {
            for (const auto& field :
                 model.program.declarations().structure(structure->structure).fields) {
                pending.push_back(field.type);
            }
            continue;
        }
        if (std::holds_alternative<GenericNominalApplication>(value)) {
            pending.append_range(model.fields(std::get<GenericTypeID>(next)));
            continue;
        }
        if (const auto* tuple = std::get_if<ConstructionTupleTypeValue>(&value)) {
            pending.append_range(tuple->elements);
            continue;
        }
        if (const auto* enumeration = std::get_if<EnumTypeValue>(&value)) {
            const auto& declaration =
                model.program.declarations().enumeration(enumeration->enumeration);
            if (std::holds_alternative<NumericEnumRepresentation>(declaration.representation)) {
                continue;
            }
            for (const auto id : declaration.cases) {
                for (const auto element :
                     model.program.declarations().enum_case(id).payload_types) {
                    pending.push_back(element);
                }
            }
            continue;
        }
        if (owning) {
            if (std::holds_alternative<ConstructionOperationTypeValue>(value)) {
                if (operation_input) {
                    continue;
                }
                return ValueAdmission::Rejected;
            }
            if (const auto* array = std::get_if<ConstructionArrayTypeValue>(&value)) {
                pending.push_back(array->element);
                continue;
            }
            if (const auto* sequence = std::get_if<AnalysisVectorType>(&value)) {
                pending.push_back(sequence->element);
                continue;
            }
        }
        if (native_payload && std::holds_alternative<CppTypeValue>(value)) {
            continue;
        }
        return ValueAdmission::Rejected;
    }
    return result;
}

auto operation(const AnalysisModelView& model, ConstructionTypeRef type) noexcept -> bool {
    return std::holds_alternative<ConstructionOperationTypeValue>(model.shape(type));
}

auto native_call(const AnalysisModelView& model, const SemCall& call) noexcept -> bool {
    auto target = call.target;
    if (!target) {
        if (const auto* function =
                std::get_if<FunctionTypeValue>(&model.shape(call.callee->type.construction()))) {
            target = function->callable;
        }
    }
    return target
        && cpp_import_form_origin(model.program.declarations().callable(*target)).has_value();
}

} // namespace

auto analyze_async_contracts(
    const AnalysisModelView& model,
    AnalysisDiagnostics diagnostics
) noexcept -> AnalysisResult<void> {
    const auto& program = model.program;
    const auto& generics = model.generics;
    const auto diagnose =
        [&](ProgramOriginID origin, DiagnosticCode code, std::string message) noexcept {
            diagnostics.error(DiagnosticBuilder(code, std::move(message))
                                  .primary(program.provenance().source_span(origin))
                                  .build());
        };
    const auto check_signature = [&](const auto& signature,
                                     ProgramOriginID origin,
                                     bool allow_dependent) noexcept {
        const auto rejected = [&](ValueAdmission admission) noexcept {
            if (admission == ValueAdmission::Dependent && !allow_dependent) {
                invariant_violation(
                    "concrete async contract retains a dependent admission requirement"
                );
            }
            return admission == ValueAdmission::Rejected;
        };
        for (const auto& parameter : signature.parameters) {
            if (parameter.stage == ParameterStage::Static) {
                continue;
            }
            if (rejected(
                    parameter.access == AccessMode::Write
                        ? scalar(model, parameter.type)
                        : admitted_value(
                              model,
                              parameter.type,
                              parameter.access == AccessMode::Take,
                              parameter.access == AccessMode::Read,
                              parameter.access == AccessMode::Take
                          )
                )) {
                diagnose(
                    origin,
                    DiagnosticCode::AsyncOwnership,
                    "async inputs require value or native borrowed Read, scalar Write, or referent-free owning Take"
                );
            }
        }
        if (rejected(admitted_value(model, signature.result, true, true))) {
            diagnose(
                origin,
                DiagnosticCode::AsyncOwnership,
                "async success must be independent of source backing"
            );
        }
        const auto failures = ConstructionFailureRef(signature.failures);
        for (const auto type : model.failure_known_members(failures)) {
            if (rejected(admitted_value(model, type, true, true))) {
                diagnose(
                    origin,
                    DiagnosticCode::AsyncOwnership,
                    "async failure must be independent of source backing"
                );
            }
        }
    };
    for (const auto entry : program.declarations().callables()) {
        const auto& signature = program.callable_signatures().signature(entry.value.signature);
        const auto body_id = program.declarations().body_for_callable(entry.id);
        if (signature.execution != CallableExecutionKind::Async) {
            continue;
        }
        const auto native_origin = cpp_import_form_origin(entry.value);
        if (!body_id && !native_origin) {
            invariant_violation("async callable has neither a body nor a native import");
        }
        const auto origin =
            body_id ? program.bodies().body(*body_id).region().origin : *native_origin;
        if (program.may_stop_test(entry.id)) {
            diagnose(
                origin,
                DiagnosticCode::AsyncAdmission,
                "async fn cannot execute a transitive test-stop effect"
            );
        }
        check_signature(signature, origin, false);
    }
    for (const auto& definition : generics.definitions) {
        if (const auto* function = std::get_if<GenericFunctionDefinition>(&definition);
            function && function->signature.execution == CallableExecutionKind::Async) {
            check_signature(function->signature, function->contract.origin, true);
        }
    }
    for (const auto& [definition, stops] : generics.test_stops) {
        const auto& function = std::get<GenericFunctionDefinition>(generics.definition(definition));
        if (stops && function.signature.execution == CallableExecutionKind::Async) {
            diagnose(
                function.contract.origin,
                DiagnosticCode::AsyncAdmission,
                "async fn cannot execute a transitive test-stop effect"
            );
        }
    }

    for (const auto& [id, body] : model.bodies()) {
        static_cast<void>(id);
        visit_semantic_nodes(
            body.region(),
            Overloaded {
                [&](const SemanticExpression& source) noexcept {
                    if (const auto* call = std::get_if<SemGenericCall>(&source.value)) {
                        const auto& function = std::get<GenericFunctionDefinition>(
                            generics.definition(call->definition)
                        );
                        if (function.signature.execution == CallableExecutionKind::Async) {
                            auto signature = function.signature;
                            if (signature.parameters.size() != call->arguments.size()) {
                                invariant_violation(
                                    "checked generic call has mismatched parameter arity"
                                );
                            }
                            for (auto index = 0uz; index < call->arguments.size(); ++index) {
                                signature.parameters[index].type =
                                    call->arguments[index].expression.type.construction();
                            }
                            const auto* completion = std::get_if<ConstructionOperationTypeValue>(
                                &model.shape(source.type.construction())
                            );
                            if (!completion) {
                                invariant_violation(
                                    "checked generic async call lacks an operation result"
                                );
                            }
                            signature.result = completion->success;
                            signature.failures = completion->failures;
                            check_signature(signature, source.origin, true);
                        }
                    }
                    if (source.category == SemanticValueCategory::Value
                        && source.selects_storage()
                        && model.contents(source.type.construction()).contains_operation_owner) {
                        diagnose(
                            source.origin,
                            DiagnosticCode::AsyncOwnership,
                            "operation-containing storage requires complete Take rather than copying"
                        );
                    }
                    const auto* call = std::get_if<SemCall>(&source.value);
                    if ((call && native_call(model, *call))
                        || std::holds_alternative<SemCpp>(source.value)
                        || std::holds_alternative<SemCppCall>(source.value)
                        || std::holds_alternative<SemClosure>(source.value)) {
                        visit_semantic_children(source.value, [&](const auto& operand) noexcept {
                            if constexpr (std::same_as<
                                              std::remove_cvref_t<decltype(operand)>,
                                              SemanticExpression>) {
                                if (model.contents(operand.type.construction())
                                        .contains_operation_owner) {
                                    diagnose(
                                        operand.origin,
                                        DiagnosticCode::AsyncAdmission,
                                        "operation cannot cross a native or capture boundary"
                                    );
                                }
                            }
                        });
                    }
                },
                [&](const SemanticStatement& source) noexcept {
                    if (const auto* discarded = std::get_if<SemExpressionStatement>(&source.value);
                        discarded && operation(model, discarded->expression.type.construction())) {
                        diagnose(
                            source.origin,
                            DiagnosticCode::AsyncOwnership,
                            "discarded cold operation never executes"
                        );
                    }
                },
                [](const auto&) static noexcept {},
            }
        );
    }
    if (const auto failure = diagnostics.failure()) {
        return std::unexpected(*failure);
    }
    return {};
}
