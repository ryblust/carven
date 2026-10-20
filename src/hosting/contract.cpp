module;

#include <carven/hosting/value.hpp>

module carven:hosting.value.contract.impl;

import :hosting.value;
import :semantic.evaluation.limits;
import :semantic.semir.constant;
import :semantic.semir.program;
import :semantic.semir.type;
import :support.utf8;
import std;

namespace {

auto type_matches(
    const SemIRProgram& program,
    TypeID type,
    const HostType& expected,
    std::span<const HostNativeType> native_types,
    std::vector<std::pair<const HostType*, TypeID>>& visited
) noexcept -> bool {
    const auto identity = std::pair(std::addressof(expected), type);
    if (std::ranges::find(visited, identity) != visited.end()) {
        return true;
    }
    visited.push_back(identity);
    if (const auto name = hosting_native_type_name(program, type)) {
        return std::ranges::any_of(native_types, [&](const auto& entry) noexcept {
            return entry.name == *name && entry.type == expected.native_type;
        });
    }
    const auto& shape = program.types().type(type).value;
    if (const auto* builtin = std::get_if<BuiltinTypeValue>(&shape)) {
        switch (expected.kind) {
            case HostTypeKind::Void: return builtin->kind == BuiltinType::Void;
            case HostTypeKind::Integer:
                return builtin_is_integer(builtin->kind)
                    && builtin_is_signed_integer(builtin->kind) == expected.integer_signed
                    && builtin_integer_width(builtin->kind) == expected.integer_bits;
            case HostTypeKind::Boolean: return builtin->kind == BuiltinType::Bool;
            case HostTypeKind::String:  return builtin->kind == BuiltinType::String;
            case HostTypeKind::Str:     return builtin->kind == BuiltinType::Str;
            default:                    return false;
        }
    }
    if (expected.kind == HostTypeKind::Vector || expected.kind == HostTypeKind::Slice) {
        const auto* vector = std::get_if<VectorTypeValue>(&shape);
        const auto* slice = std::get_if<SliceTypeValue>(&shape);
        if (expected.kind == HostTypeKind::Vector ? vector == nullptr : slice == nullptr) {
            return false;
        }
        return type_matches(
            program,
            vector ? vector->element : slice->element,
            expected.element(),
            native_types,
            visited
        );
    }
    if (expected.kind == HostTypeKind::Enum) {
        const auto* enumeration = std::get_if<EnumTypeValue>(&shape);
        if (!enumeration
            || program.generic_nominal_instance(NominalDeclarationRef(enumeration->enumeration))) {
            return false;
        }
        const auto& declaration = program.declarations().enumeration(enumeration->enumeration);
        const auto& module_decl = program.declarations().module_decl(declaration.module_id);
        const auto name = std::format(
            "{}.{}",
            program.provenance().module_record(module_decl.provenance_module).path.value(),
            program.provenance().spelling(declaration.name)
        );
        if (expected.name != name || expected.cases.size() != declaration.cases.size()) {
            return false;
        }
        const auto* numeric = std::get_if<NumericEnumRepresentation>(&declaration.representation);
        if (numeric) {
            const auto underlying =
                std::get<BuiltinTypeValue>(program.types().type(numeric->underlying_type).value)
                    .kind;
            if (expected.integer_bits != builtin_integer_width(underlying)
                || expected.integer_signed != builtin_is_signed_integer(underlying)) {
                return false;
            }
        } else if (expected.integer_bits != 0) {
            return false;
        }
        for (const auto id : declaration.cases) {
            const auto& item = program.declarations().enum_case(id);
            const auto found = std::ranges::find(
                expected.cases,
                program.provenance().spelling(item.name),
                &HostTypeCase::name
            );
            if (found == expected.cases.end()
                || found->payload.size() != item.payload_types.size()) {
                return false;
            }
            if (numeric) {
                const auto number = std::get<NumericEnumConstant>(
                                        program.constants().constant(*item.constant).value
                )
                                        .value;
                const auto normalized =
                    number.negative() ? std::uint64_t(0) - number.magnitude() : number.magnitude();
                if (found->numeric_value != normalized) {
                    return false;
                }
            } else if (found->numeric_value) {
                return false;
            }
            for (auto index = 0uz; index < item.payload_types.size(); ++index) {
                if (!type_matches(
                        program,
                        item.payload_types[index],
                        found->payload[index].describe(),
                        native_types,
                        visited
                    )) {
                    return false;
                }
            }
        }
        return true;
    }
    const auto* record = std::get_if<StructTypeValue>(&shape);
    if (expected.kind != HostTypeKind::Record || record == nullptr) {
        return false;
    }
    const auto& declaration = program.declarations().structure(record->structure);
    const auto& module_decl = program.declarations().module_decl(declaration.module_id);
    const auto name = std::format(
        "{}.{}",
        program.provenance().module_record(module_decl.provenance_module).path.value(),
        program.provenance().spelling(declaration.name)
    );
    if (declaration.kind != RecordKind::Struct
        || program.generic_nominal_instance(NominalDeclarationRef(record->structure))
        || name != expected.name
        || declaration.fields.size() != expected.fields.size()) {
        return false;
    }
    for (auto index = 0uz; index < declaration.fields.size(); ++index) {
        const auto& field = declaration.fields[index];
        const auto& descriptor = expected.fields[index];
        if (descriptor.name != program.provenance().spelling(field.name)
            || !type_matches(program, field.type, descriptor.describe(), native_types, visited)) {
            return false;
        }
    }
    return true;
}

} // namespace

extern "C++" {

auto hosting_valid_text(std::string_view text) noexcept -> bool {
    return UTF8Decoder::is_valid(text);
}

auto host_check_text(std::string_view text, HostLimits limits) noexcept
    -> std::expected<void, HostError> {
    const auto error = carven::runtime::hosting::detail::native_text_error(
        text,
        limits.byte_work,
        maximum_constant_bytes,
        hosting_valid_text
    );
    if (!error.empty()) {
        return std::unexpected(hosting_error(HostErrorKind::Arguments, std::string(error)));
    }
    return {};
}

auto host_maximum_bytes() noexcept -> std::size_t {
    return maximum_constant_bytes;
}

auto host_maximum_aggregate_depth() noexcept -> std::size_t {
    return maximum_constant_aggregate_depth;
}

auto host_maximum_aggregate_elements() noexcept -> std::size_t {
    return maximum_constant_aggregate_elements;
}

} // extern "C++"

auto hosting_call_type(
    const SemIRProgram& program,
    TypeID type,
    const HostCallType& entry,
    std::span<const HostNativeType> native_types
) noexcept -> bool {
    if (entry.allow_cpp && std::holds_alternative<CppTypeValue>(program.types().type(type).value)) {
        return true;
    }
    if (entry.describe == nullptr) {
        return false;
    }
    auto visited = std::vector<std::pair<const HostType*, TypeID>>();
    return type_matches(program, type, entry.describe(), native_types, visited);
}

auto hosting_failures_match(
    const SemIRProgram& program,
    FailureSetID failures,
    std::span<const HostFailureType> contract,
    std::span<const HostNativeType> native_types
) noexcept -> bool {
    const auto& members = program.failure_sets().failure_set(failures).members;
    if (members.size() != contract.size()) {
        return false;
    }
    return std::ranges::all_of(members, [&](const auto type) noexcept {
        const auto failure = hosting_failure_type(program, type);
        return failure && std::ranges::any_of(contract, [&](const auto& expected) noexcept {
                   return expected.module_path == failure->module_path
                       && expected.name == failure->name
                       && (expected.type.describe == nullptr
                           || hosting_call_type(program, type, expected.type, native_types));
               });
    });
}

auto hosting_contract_matches(
    const SemIRProgram& program,
    CallableSignatureID signature_id,
    const HostCallContract& contract
) noexcept -> bool {
    const auto& signature = program.callable_signatures().signature(signature_id);
    if (contract.source_signature) {
        const auto identity = program.callable_contract_identity(signature_id);
        if (!identity || *identity != *contract.source_signature) {
            return false;
        }
    }
    const auto execution = contract.execution == HostCallExecution::Async
        ? CallableExecutionKind::Async
        : CallableExecutionKind::Synchronous;
    if (signature.execution != execution
        || signature.parameters.size() != contract.parameters.size()
        || !hosting_call_type(program, signature.result, contract.result, contract.native_types)
        || !hosting_failures_match(
            program,
            signature.failures,
            contract.failures,
            contract.native_types
        )) {
        return false;
    }
    for (auto index = 0uz; index < signature.parameters.size(); ++index) {
        const auto& parameter = signature.parameters[index];
        const auto& expected = contract.parameters[index];
        const auto access = [&]() noexcept {
            switch (expected.access) {
                case HostCallAccess::Read:  return AccessMode::Read;
                case HostCallAccess::Write: return AccessMode::Write;
                case HostCallAccess::Take:  return AccessMode::Take;
            }
            std::unreachable();
        }();
        if (parameter.access != access
            || !hosting_call_type(program, parameter.type, expected.type, contract.native_types)) {
            return false;
        }
    }
    return true;
}
