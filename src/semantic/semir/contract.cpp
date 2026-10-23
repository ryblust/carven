module carven:semantic.semir.contract.impl;

import :semantic.semir.constant;
import :semantic.semir.contract;
import :semantic.semir.decl;
import :semantic.semir.delegation;
import :semantic.semir.generic;
import :semantic.semir.type;
import :semantic.semir.values;
import std;

namespace {

auto source_type_identity(
    const ExecutionValueAccess& values,
    TypeID type,
    bool fields,
    std::set<NominalDeclarationRef>& seen
) noexcept -> std::optional<std::string>;

auto source_module_name(const ExecutionValueAccess& values, ModuleID id) noexcept -> std::string {
    return values.module_path(id);
}

auto source_failure_identity(
    const ExecutionValueAccess& values,
    FailureSetID failures,
    bool fields,
    std::set<NominalDeclarationRef>& seen
) noexcept -> std::optional<std::string> {
    auto ordered = std::vector<std::pair<std::string, TypeID>>();
    for (const auto member : values.failure_set_copy(failures).members) {
        auto plain_seen = std::set<NominalDeclarationRef>();
        auto name = source_type_identity(values, member, false, plain_seen);
        if (!name) {
            return std::nullopt;
        }
        ordered.emplace_back(std::move(*name), member);
    }
    std::ranges::sort(ordered, {}, &std::pair<std::string, TypeID>::first);
    auto result = std::string("throw(");
    for (const auto [index, entry] : std::views::enumerate(ordered)) {
        if (index != 0) {
            result += ',';
        }
        auto name = source_type_identity(values, entry.second, fields, seen);
        if (!name) {
            return std::nullopt;
        }
        result += *name;
    }
    return result + ')';
}

auto source_callable_identity(
    const ExecutionValueAccess& values,
    const CallableSignature& signature,
    bool fields,
    std::set<NominalDeclarationRef>& seen
) noexcept -> std::optional<std::string> {
    auto result =
        std::string(signature.execution == CallableExecutionKind::Async ? "async fn(" : "fn(");
    for (const auto [index, parameter] : std::views::enumerate(signature.parameters)) {
        if (index != 0) {
            result += ',';
        }
        if (parameter.stage == ParameterStage::Static) {
            result += "const ";
        }
        if (parameter.access == AccessMode::Write) {
            result += '&';
        }
        if (parameter.access == AccessMode::Take) {
            result += "&&";
        }
        auto name = source_type_identity(values, parameter.type, fields, seen);
        if (!name) {
            return std::nullopt;
        }
        result += *name;
    }
    auto value = source_type_identity(values, signature.result, fields, seen);
    auto failures = source_failure_identity(values, signature.failures, fields, seen);
    if (!value || !failures) {
        return std::nullopt;
    }
    return result + ")->" + *value + ";" + *failures;
}

auto source_native_identity(
    const ExecutionValueAccess& values,
    const CppNamedType& named,
    bool fields,
    std::set<NominalDeclarationRef>& seen
) noexcept -> std::optional<std::string> {
    auto result = named.name.lookup == CppNameLookup::ModuleScope
        ? "[" + std::string(source_module_name(values, named.name.context_module)) + "]::"
        : std::string();
    for (const auto& component : named.name.components) {
        if (!result.empty() && !result.ends_with("::")) {
            result += "::";
        }
        result += component;
    }
    if (!named.arguments.empty()) {
        result += '<';
        for (const auto [index, argument] : std::views::enumerate(named.arguments)) {
            if (index != 0) {
                result += ',';
            }
            auto name = source_type_identity(values, argument, fields, seen);
            if (!name) {
                return std::nullopt;
            }
            result += *name;
        }
        result += '>';
    }
    return result;
}

auto source_nominal_identity(
    const ExecutionValueAccess& values,
    NominalDeclarationRef nominal,
    bool fields,
    std::set<NominalDeclarationRef>& seen
) noexcept -> std::optional<std::string> {
    auto identity = nominal.visit([&](auto id) noexcept -> std::optional<std::string> {
        if constexpr (std::same_as<decltype(id), StructID>) {
            const auto declaration = values.struct_declaration_copy(id);
            if (!declaration) {
                return std::nullopt;
            }
            return values.module_path(declaration->module_id) + "."
                + std::string(values.spelling(declaration->name));
        } else {
            const auto declaration = values.enum_declaration_copy(id);
            return values.module_path(declaration.module_id) + "."
                + std::string(values.spelling(declaration.name));
        }
    });
    if (!identity) {
        return std::nullopt;
    }
    auto name = std::move(*identity);
    if (const auto instance = values.generic_nominal_instance_copy(nominal)) {
        const auto& declaration = values.generic_declaration_contract_copy(instance->definition);
        name = std::string(source_module_name(values, declaration.module_id)) + "."
            + std::string(values.spelling(*declaration.name)) + '<';
        auto plain_seen = std::set<NominalDeclarationRef>();
        for (const auto [index, argument] : std::views::enumerate(instance->arguments)) {
            if (index != 0) {
                name += ',';
            }
            auto nested = argument.visit([&](const auto id) noexcept {
                if constexpr (std::same_as<std::remove_cvref_t<decltype(id)>, TypeID>) {
                    return source_type_identity(values, id, false, plain_seen);
                } else {
                    return source_failure_identity(values, id, false, plain_seen);
                }
            });
            if (!nested) {
                return std::nullopt;
            }
            name += *nested;
        }
        name += '>';
    }
    if (!fields || !seen.insert(nominal).second) {
        return name;
    }
    return nominal.visit([&](auto id) noexcept -> std::optional<std::string> {
        if constexpr (std::same_as<decltype(id), StructID>) {
            const auto record = values.struct_declaration_copy(id);
            if (!record) {
                return std::nullopt;
            }
            const auto& declaration = *record;
            name += declaration.kind == RecordKind::Struct ? "{struct:" : "{class:";
            for (const auto& field : declaration.fields) {
                auto child = source_type_identity(values, field.type, true, seen);
                if (!child) {
                    return std::nullopt;
                }
                name += std::string(values.spelling(field.name)) + ":" + *child + ";";
            }
        } else {
            const auto& declaration = values.enum_declaration_copy(id);
            name += "{enum:";
            if (const auto* numeric =
                    std::get_if<NumericEnumRepresentation>(&declaration.representation)) {
                auto underlying =
                    source_type_identity(values, numeric->underlying_type, true, seen);
                if (!underlying) {
                    return std::nullopt;
                }
                name += *underlying;
            }
            name += declaration.supports_equality ? ";equal;" : ";opaque;";
            for (const auto case_id : declaration.cases) {
                const auto entry = values.enum_case_declaration_copy(case_id);
                if (!entry) {
                    return std::nullopt;
                }
                const auto& member = *entry;
                name += std::string(values.spelling(member.name)) + '(';
                for (const auto payload : member.payload_types) {
                    auto child = source_type_identity(values, payload, true, seen);
                    if (!child) {
                        return std::nullopt;
                    }
                    name += *child + ";";
                }
                name += ')';
                if (member.constant
                    && std::holds_alternative<NumericEnumRepresentation>(
                        declaration.representation
                    )) {
                    const auto* value =
                        std::get_if<NumericEnumConstant>(&values.constant(*member.constant).value);
                    if (value == nullptr) {
                        return std::nullopt;
                    }
                    name += '=';
                    if (value->value.negative()) {
                        name += '-';
                    }
                    name += std::to_string(value->value.magnitude());
                }
                name += ';';
            }
        }
        return name + '}';
    });
}

auto source_type_identity(
    const ExecutionValueAccess& values,
    TypeID type,
    bool fields,
    std::set<NominalDeclarationRef>& seen
) noexcept -> std::optional<std::string> {
    const auto unary = [&](std::string_view prefix,
                           TypeID element,
                           std::string_view suffix) noexcept -> std::optional<std::string> {
        auto name = source_type_identity(values, element, fields, seen);
        if (!name) {
            return std::nullopt;
        }
        name->insert(0uz, prefix);
        name->append(suffix);
        return name;
    };
    return values.type_copy(type).value.visit(
        [&](const auto& shape) noexcept -> std::optional<std::string> {
            using Shape = std::remove_cvref_t<decltype(shape)>;
            if constexpr (std::same_as<Shape, BuiltinTypeValue>) {
                return std::string(builtin_name(shape.kind));
            } else if constexpr (std::same_as<Shape, StructTypeValue>) {
                return source_nominal_identity(
                    values,
                    NominalDeclarationRef(shape.structure),
                    fields,
                    seen
                );
            } else if constexpr (std::same_as<Shape, EnumTypeValue>) {
                return source_nominal_identity(
                    values,
                    NominalDeclarationRef(shape.enumeration),
                    fields,
                    seen
                );
            } else if constexpr (std::same_as<Shape, CppTypeValue>) {
                if (const auto* named = std::get_if<CppNamedType>(&shape.form)) {
                    auto name = source_native_identity(values, *named, fields, seen);
                    if (!name) {
                        return std::nullopt;
                    }
                    return "::" + *name;
                }
                if (std::holds_alternative<CppConstCharPointerType>(shape.form)) {
                    return "const char*";
                }
                return std::nullopt;
            } else if constexpr (std::same_as<Shape, VectorTypeValue>) {
                return unary("Vector<", shape.element, ">");
            } else if constexpr (std::same_as<Shape, RangeTypeValue>) {
                return unary("Range<", shape.element, ">");
            } else if constexpr (std::same_as<Shape, SliceTypeValue>) {
                return unary("[", shape.element, "]");
            } else if constexpr (std::same_as<Shape, ArrayTypeValue>) {
                return unary("[", shape.element, ";" + std::to_string(shape.extent) + "]");
            } else if constexpr (std::same_as<Shape, PointerTypeValue>) {
                return unary(
                    shape.access == PointerAccess::Write ? "ptr<&" : "ptr<",
                    shape.target,
                    ">"
                );
            } else if constexpr (std::same_as<Shape, TupleTypeValue>) {
                auto name = std::string("(");
                for (const auto [index, element] : std::views::enumerate(shape.elements)) {
                    if (index != 0) {
                        name += ',';
                    }
                    auto nested = source_type_identity(values, element, fields, seen);
                    if (!nested) {
                        return std::nullopt;
                    }
                    name += *nested;
                }
                if (shape.elements.size() == 1uz) {
                    name += ',';
                }
                return name + ')';
            } else if constexpr (std::same_as<Shape, OperationTypeValue>) {
                auto success = source_type_identity(values, shape.success, fields, seen);
                auto failures = source_failure_identity(values, shape.failures, fields, seen);
                if (!success || !failures) {
                    return std::nullopt;
                }
                return "Operation<" + *success + ";" + *failures + ">";
            } else if constexpr (std::same_as<Shape, CallableViewTypeValue>) {
                return source_callable_identity(
                    values,
                    values.callable_signature_copy(shape.signature),
                    fields,
                    seen
                );
            } else {
                static_assert(
                    std::same_as<Shape, FunctionTypeValue> || std::same_as<Shape, ClosureTypeValue>
                );
                return std::nullopt;
            }
        }
    );
}

} // namespace

auto native_type_name(const ExecutionValueAccess& values, TypeID type) noexcept
    -> std::optional<std::string> {
    const auto canonical = values.type_copy(type);
    const auto* native = std::get_if<CppTypeValue>(&canonical.value);
    const auto* named = native ? std::get_if<CppNamedType>(&native->form) : nullptr;
    if (named == nullptr) {
        return std::nullopt;
    }
    auto seen = std::set<NominalDeclarationRef>();
    return source_native_identity(values, *named, false, seen);
}

auto callable_contract_identity(
    const ExecutionValueAccess& values,
    CallableSignatureID signature
) noexcept -> std::optional<std::string> {
    auto seen = std::set<NominalDeclarationRef>();
    return source_callable_identity(values, values.callable_signature_copy(signature), true, seen);
}

auto callable_contract_identity(
    const ExecutionValueAccess& values,
    const CallableSignature& signature
) noexcept -> std::optional<std::string> {
    auto seen = std::set<NominalDeclarationRef>();
    return source_callable_identity(values, signature, true, seen);
}
