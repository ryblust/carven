module carven:semantic.analysis.async.impl;

import :diagnostics.builder;
import :diagnostics.code;
import :semantic.analysis.async;
import :semantic.semir.children;
import :semantic.semir.decl;
import :semantic.semir.traversal;
import :semantic.visibility;
import :support.invariant;
import :support.visit;
import std;

namespace {

auto scalar(const SemIRProgram& program, TypeID type) noexcept -> bool {
    const auto* builtin = std::get_if<BuiltinTypeValue>(&program.types().type(type).value);
    return builtin
        && (builtin_is_numeric(builtin->kind)
            || builtin->kind == BuiltinType::Bool
            || builtin->kind == BuiltinType::Char);
}

// Read snapshots and owning values have different source storage contracts.
// Native payloads use the provider's ordinary interop contract.
auto admitted_value(
    const SemIRProgram& program,
    TypeID type,
    bool owning,
    bool native_payload = false
) noexcept -> bool {
    auto pending = std::vector<TypeID> {type};
    auto seen = std::set<TypeID>();
    while (!pending.empty()) {
        const auto next = pending.back();
        pending.pop_back();
        if (!seen.insert(next).second || scalar(program, next)) {
            continue;
        }
        const auto& value = program.types().type(next).value;
        if (const auto* builtin = std::get_if<BuiltinTypeValue>(&value)) {
            if (builtin->kind == BuiltinType::Void
                || (owning && builtin->kind == BuiltinType::String)) {
                continue;
            }
            return false;
        }
        if (const auto* structure = std::get_if<StructTypeValue>(&value)) {
            for (const auto& field :
                 program.declarations().structure(structure->structure).fields) {
                pending.push_back(field.type);
            }
            continue;
        }
        if (const auto* enumeration = std::get_if<EnumTypeValue>(&value)) {
            const auto& declaration = program.declarations().enumeration(enumeration->enumeration);
            if (std::holds_alternative<NumericEnumRepresentation>(declaration.representation)) {
                continue;
            }
            for (const auto id : declaration.cases) {
                for (const auto element : program.declarations().enum_case(id).payload_types) {
                    pending.push_back(element);
                }
            }
            continue;
        }
        if (owning) {
            if (const auto* array = std::get_if<ArrayTypeValue>(&value)) {
                pending.push_back(array->element);
                continue;
            }
        }
        if (native_payload && std::holds_alternative<CppTypeValue>(value)) {
            continue;
        }
        return false;
    }
    return true;
}

auto operation(const SemIRProgram& program, TypeID type) noexcept -> bool {
    return std::holds_alternative<OperationTypeValue>(program.types().type(type).value);
}

} // namespace

auto analyze_async_contracts(const SemIRProgram& program, AnalysisDiagnostics diagnostics) noexcept
    -> AnalysisResult<void> {
    const auto diagnose =
        [&](ProgramOriginID origin, DiagnosticCode code, std::string message) noexcept {
            diagnostics.error(DiagnosticBuilder(code, std::move(message))
                                  .primary(program.provenance().source_span(origin))
                                  .build());
        };
    for (const auto entry : program.declarations().callables()) {
        const auto& signature = program.callable_signatures().signature(entry.value.signature);
        const auto body_id = program.declarations().body_for_callable(entry.id);
        if (signature.execution == CallableExecutionKind::Synchronous
            && operation(program, signature.result)) {
            if (const auto function = program.declarations().function_for_callable(entry.id)) {
                const auto& declaration = program.declarations().function(*function);
                if (declaration.cpp_export_origin
                    || declaration.visibility == DeclarationVisibility::Compilation) {
                    diagnose(
                        declaration.origin,
                        DiagnosticCode::AsyncAdmission,
                        "inferred operation return requires a private source function"
                    );
                }
            }
        }
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
        for (const auto& parameter : signature.parameters) {
            if (parameter.stage == ParameterStage::Static) {
                continue;
            }
            if (parameter.access == AccessMode::Write ? !scalar(program, parameter.type)
                                                      : !admitted_value(
                                                            program,
                                                            parameter.type,
                                                            parameter.access == AccessMode::Take,
                                                            parameter.access == AccessMode::Read
                                                        )) {
                diagnose(
                    origin,
                    DiagnosticCode::AsyncOwnership,
                    "async inputs require value or native borrowed Read, scalar Write, or referent-free owning Take"
                );
            }
        }
        if (!admitted_value(program, signature.result, true, true)) {
            diagnose(
                origin,
                DiagnosticCode::AsyncOwnership,
                "async success must be independent of source backing"
            );
        }
        for (const auto type : program.failure_sets().failure_set(signature.failures).members) {
            if (!admitted_value(program, type, true, true)) {
                diagnose(
                    origin,
                    DiagnosticCode::AsyncOwnership,
                    "async failure must be independent of source backing"
                );
            }
        }
    }
    for (const auto entry : program.bodies().entries()) {
        visit_semantic_nodes(
            entry.value.region(),
            Overloaded {
                [&](const SemanticExpression& source) noexcept {
                    if (const auto* array = std::get_if<ArrayTypeValue>(
                            &program.types().type(source.type.resolved()).value
                        );
                        array && program.type_contents(array->element).contains_operation_owner) {
                        diagnose(
                            source.origin,
                            DiagnosticCode::AsyncAdmission,
                            "operations cannot be stored in arrays"
                        );
                    }
                    if (std::holds_alternative<SemCpp>(source.value)
                        || std::holds_alternative<SemCppCall>(source.value)
                        || std::holds_alternative<SemClosure>(source.value)
                        || std::holds_alternative<SemEnumCase>(source.value)
                        || std::holds_alternative<SemStruct>(source.value)) {
                        visit_semantic_children(source.value, [&](const auto& operand) noexcept {
                            if constexpr (std::same_as<
                                              std::remove_cvref_t<decltype(operand)>,
                                              SemanticExpression>) {
                                if (program.type_contents(operand.type.resolved())
                                        .contains_operation_owner) {
                                    diagnose(
                                        operand.origin,
                                        DiagnosticCode::AsyncAdmission,
                                        "operation cannot cross a native, capture, or aggregate boundary"
                                    );
                                }
                            }
                        });
                    }
                },
                [&](const SemanticStatement& source) noexcept {
                    if (const auto* assignment = std::get_if<SemAssign>(&source.value); assignment
                        && (operation(program, assignment->target.type.resolved())
                            || operation(program, assignment->value.type.resolved()))) {
                        diagnose(
                            source.origin,
                            DiagnosticCode::AsyncAdmission,
                            "operation assignment is not admitted"
                        );
                    }
                    if (const auto* discarded = std::get_if<SemExpressionStatement>(&source.value);
                        discarded && operation(program, discarded->expression.type.resolved())) {
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
