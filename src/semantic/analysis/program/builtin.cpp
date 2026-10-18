module carven:semantic.analysis.program.builtin.impl;

import :semantic.analysis.program;
import :semantic.semir.decl;
import :source.standard;
import std;

auto ProgramDraft::provided_function(ProvidedFunction function) noexcept -> CallableID {
    if (const auto found = storage.provided_functions.find(function);
        found != storage.provided_functions.end()) {
        return found->second;
    }
    auto parameters = std::vector<ConstructionCallableParameter>();
    const auto parameter = [&](BuiltinType type) noexcept {
        parameters.push_back(
            {.stage = ParameterStage::Runtime,
             .access = AccessMode::Read,
             .type = builtin_type(type)}
        );
    };
    const auto operation = [&](TypeID result) noexcept {
        return intern_type(
            {.value = OperationTypeValue {.success = result, .failures = empty_failure_set()}}
        );
    };
    auto result = builtin_type(BuiltinType::Void);
    switch (function) {
        case ProvidedFunction::ReadInput: {
            parameter(BuiltinType::U8);
            parameter(BuiltinType::Usize);
            const auto bytes =
                intern_type({.value = VectorTypeValue {.element = builtin_type(BuiltinType::U8)}});
            result = operation(intern_type(
                {.value = TupleTypeValue {
                     .elements = {
                         bytes,
                         builtin_type(BuiltinType::Bool),
                         builtin_type(BuiltinType::I32),
                         builtin_type(BuiltinType::Bool)
                     }
                 }}
            ));
            break;
        }
        case ProvidedFunction::MonotonicNow: result = builtin_type(BuiltinType::I64); break;
        case ProvidedFunction::WaitUntil:
            parameter(BuiltinType::I64);
            result = operation(builtin_type(BuiltinType::Void));
            break;
    }
    const auto callable = append_body_callable(
        {.execution = CallableExecutionKind::Synchronous,
         .parameters = std::move(parameters),
         .result = result,
         .failures = empty_failure_set(),
         .policy = FailureContractPolicy::Declared}
    );
    complete_callable(callable, ProvidedFunctionImplementation {.function = function});
    storage.provided_functions.emplace(function, callable);
    return callable;
}

auto ProgramDraft::provided_callable(ProvidedExport identity) noexcept
    -> std::optional<CallableID> {
    switch (identity) {
        case ProvidedExport::ReadInput:    return provided_function(ProvidedFunction::ReadInput);
        case ProvidedExport::MonotonicNow: return provided_function(ProvidedFunction::MonotonicNow);
        case ProvidedExport::WaitUntil:    return provided_function(ProvidedFunction::WaitUntil);
        default:                           return std::nullopt;
    }
}

auto ProgramDraft::number_parser(BuiltinType number, ConstantID invalid) noexcept -> CallableID {
    if (const auto found = storage.number_parsers.find(number);
        found != storage.number_parsers.end()) {
        return found->second;
    }
    const auto callable = append_body_callable(
        ConstructionCallableContract {
            .execution = CallableExecutionKind::Synchronous,
            .parameters =
                {{.stage = ParameterStage::Runtime,
                  .access = AccessMode::Read,
                  .type = builtin_type(BuiltinType::Str)}},
            .result = builtin_type(number),
            .failures = intern_failure_set({constant(invalid).type}),
            .policy = FailureContractPolicy::Declared,
        }
    );
    complete_callable(callable, NumberParseImplementation {.invalid = invalid});
    storage.number_parsers.emplace(number, callable);
    return callable;
}
