module carven:semantic.evaluation.admission.impl;

import :semantic.evaluation.admission;
import :semantic.semir.simd;
import std;

auto supported_execution_type(
    const ExecutionValueAccess& values,
    ConstructionTypeRef type,
    bool allow_void
) noexcept -> bool {
    const auto* id = std::get_if<TypeID>(&type);
    if (!id) {
        return false;
    }
    auto pending = std::vector<TypeID> {*id};
    auto visited = std::set<TypeID>();
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (!visited.insert(current).second) {
            continue;
        }
        const auto canonical = values.type_copy(current);
        if (std::holds_alternative<OwnedSequenceTypeValue>(canonical.value)) {
            return false;
        } else if (const auto* array = std::get_if<ArrayTypeValue>(&canonical.value)) {
            pending.push_back(array->element);
        } else if (const auto* structure = std::get_if<StructTypeValue>(&canonical.value)) {
            const auto fields = values.struct_field_types(structure->structure);
            if (!fields) {
                return false;
            }
            pending.insert(pending.end(), fields->begin(), fields->end());
        } else if (const auto* enumeration = std::get_if<EnumTypeValue>(&canonical.value)) {
            const auto cases = values.enum_case_types(enumeration->enumeration);
            if (!cases) {
                return false;
            }
            for (const auto& item : *cases) {
                pending.insert(pending.end(), item.payload_types.begin(), item.payload_types.end());
            }
        } else if (std::holds_alternative<PointerTypeValue>(canonical.value)
                   || std::holds_alternative<SliceTypeValue>(canonical.value)
                   || std::holds_alternative<FunctionTypeValue>(canonical.value)
                   || std::holds_alternative<CallableViewTypeValue>(canonical.value)
                   || std::holds_alternative<RangeTypeValue>(canonical.value)) {
            // Views do not contain their targets' storage.
        } else if (const auto* cpp = std::get_if<CppTypeValue>(&canonical.value)) {
            if (!std::holds_alternative<CppConstCharPointerType>(cpp->form)) {
                return false;
            }
        } else if (const auto* builtin = std::get_if<BuiltinTypeValue>(&canonical.value)) {
            if (!builtin_is_numeric(builtin->kind)
                && !simd_layout(builtin->kind)
                && builtin->kind != BuiltinType::Bool
                && builtin->kind != BuiltinType::Char
                && builtin->kind != BuiltinType::Str
                && builtin->kind != BuiltinType::String
                && !(allow_void && current == *id && builtin->kind == BuiltinType::Void)) {
                return false;
            }
        } else {
            return false;
        }
    }
    return true;
}

auto unsupported_execution_expression(const SemanticExpression& source) noexcept
    -> std::optional<std::string_view> {
    return source.value.visit([](const auto& operation) static noexcept {
        return unsupported_execution_operation(operation);
    });
}

auto unsupported_execution_statement(const SemanticStatement& source) noexcept
    -> std::optional<std::string_view> {
    return source.value.visit(
        [](const auto& value) static noexcept -> std::optional<std::string_view> {
            using Operation = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Operation, SemReturn>
                          || std::same_as<Operation, SemThrow>
                          || std::same_as<Operation, SemRethrow>
                          || std::same_as<Operation, SemBreak>
                          || std::same_as<Operation, SemContinue>
                          || std::same_as<Operation, SemExpressionStatement>
                          || std::same_as<Operation, SemInitialize>
                          || std::same_as<Operation, SemStaticBinding>
                          || std::same_as<Operation, SemConstBlock>
                          || std::same_as<Operation, SemAssign>
                          || std::same_as<Operation, SemLoop>
                          || std::same_as<Operation, SemRangeLoop>
                          || std::same_as<Operation, SemExpandedLoop>
                          || std::same_as<Operation, OwnedSemanticRegion>) {
                return std::nullopt;
            } else {
                return "control operation is not supported in execution";
            }
        }
    );
}
