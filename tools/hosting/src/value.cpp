module;

#include <carven/hosting/value.hpp>

module carven:hosting.value.impl;

import :hosting.value;
import :semantic.evaluation.value;
import :semantic.semir.constant;
import :semantic.semir.contract;
import :semantic.semir.decl;
import :semantic.semir.delegation;
import :semantic.semir.type;
import :semantic.semir.values;
import std;

extern "C++" {

HostValue::HostValue(HostObjectRef reference) noexcept
    : variant(std::in_place_type<HostObjectRef>, std::move(reference)) {}

HostValue::HostValue(const HostValue& other) noexcept
    : variant(static_cast<const variant&>(other)) {
    if (auto* native = std::get_if<HostObjectRef>(this)) {
        *native = carven::runtime::hosting::detail::ObjectAccess::copy_value(*native);
    }
}

auto HostValue::operator=(const HostValue& other) noexcept -> HostValue& {
    if (this != std::addressof(other)) {
        *this = HostValue(other);
    }
    return *this;
}

HostFailure::HostFailure(HostRecord payload) noexcept
    : value(std::move(payload)) {}

HostFailure::HostFailure(HostEnum payload) noexcept
    : value(std::move(payload)) {}

auto HostFailure::module_path() const noexcept -> std::string_view {
    return value.visit([](const auto& payload) static noexcept -> std::string_view {
        using Payload = std::remove_cvref_t<decltype(payload)>;
        if constexpr (std::same_as<Payload, HostRecord> || std::same_as<Payload, HostEnum>) {
            return payload.module_path;
        } else {
            std::unreachable();
        }
    });
}

auto HostFailure::name() const noexcept -> std::string_view {
    return value.visit([](const auto& payload) static noexcept -> std::string_view {
        using Payload = std::remove_cvref_t<decltype(payload)>;
        if constexpr (std::same_as<Payload, HostRecord> || std::same_as<Payload, HostEnum>) {
            return payload.name;
        } else {
            std::unreachable();
        }
    });
}

auto HostFailure::payload() const& noexcept -> const HostValue& {
    return value;
}

auto HostFailure::payload() && noexcept -> HostValue {
    return std::move(value);
}

auto HostFailure::same_type(const HostFailure& other) const noexcept -> bool {
    return !name().empty() && module_path() == other.module_path() && name() == other.name();
}

auto hosting_error(HostErrorKind kind, std::string message) noexcept -> HostError {
    return {.kind = kind, .message = std::move(message), .location = std::nullopt, .calls = {}};
}

} // extern "C++"

auto hosting_type_supported(
    const ExecutionValueAccess& values,
    TypeID type,
    bool allow_void
) noexcept -> bool {
    auto pending = std::vector<TypeID> {type};
    auto visited = std::set<TypeID>();
    while (!pending.empty()) {
        const auto current = pending.back();
        pending.pop_back();
        if (!visited.insert(current).second) {
            continue;
        }
        const auto& shape = values.type_copy(current).value;
        if (const auto* builtin = std::get_if<BuiltinTypeValue>(&shape)) {
            if (!builtin_is_integer(builtin->kind)
                && builtin->kind != BuiltinType::Bool
                && builtin->kind != BuiltinType::String
                && !(allow_void && current == type && builtin->kind == BuiltinType::Void)) {
                return false;
            }
        } else if (native_type_name(values, current)) {
            // Object identity and liveness are checked at value ingress.
        } else if (const auto* enumeration = std::get_if<EnumTypeValue>(&shape)) {
            if (values.generic_nominal_instance_copy(
                    NominalDeclarationRef(enumeration->enumeration)
                )) {
                return false;
            }
            for (const auto id : values.enum_declaration_copy(enumeration->enumeration).cases) {
                const auto entry = values.enum_case_declaration_copy(id);
                if (!entry) {
                    return false;
                }
                const auto& item = *entry;
                pending.insert(pending.end(), item.payload_types.begin(), item.payload_types.end());
            }
        } else if (const auto* sequence = std::get_if<VectorTypeValue>(&shape)) {
            pending.push_back(sequence->element);
        } else if (const auto* structure = std::get_if<StructTypeValue>(&shape)) {
            const auto record = values.struct_declaration_copy(structure->structure);
            if (!record) {
                return false;
            }
            const auto& declaration = *record;
            if (declaration.kind != RecordKind::Struct
                || values.generic_nominal_instance_copy(
                    NominalDeclarationRef(structure->structure)
                )) {
                return false;
            }
            for (const auto& field : declaration.fields) {
                pending.push_back(field.type);
            }
        } else {
            return false;
        }
    }
    return true;
}

auto hosting_signature_supported(
    const SemIRProgram& program,
    const CallableSignature& signature,
    bool allow_failures,
    bool allow_async
) noexcept -> bool {
    const auto values = ProgramValues(program);
    if ((!allow_async && signature.execution != CallableExecutionKind::Synchronous)
        || signature.has_static_parameters()
        || !hosting_type_supported(values, signature.result, true)) {
        return false;
    }
    for (const auto& parameter : signature.parameters) {
        if ((parameter.access != AccessMode::Read && parameter.access != AccessMode::Take)
            || !hosting_type_supported(values, parameter.type, false)) {
            return false;
        }
    }
    for (const auto failure : values.failure_set_copy(signature.failures).members) {
        if (!allow_failures
            || !hosting_failure_type(values, failure)
            || !hosting_type_supported(values, failure, false)) {
            return false;
        }
    }
    return true;
}

namespace {

template<typename Value>
auto import_host_value(
    const ExecutionValueAccess& values,
    Value&& value,
    TypeID type,
    HostLimits& budget,
    std::span<const HostNativeType> native_types
) noexcept -> std::expected<ExecutionValue, HostError> {
    auto limits = budget;
    const auto aggregate_limit =
        std::min(limits.aggregate_work, maximum_constant_aggregate_elements);
    limits.aggregate_work = aggregate_limit;
    const auto convert =
        [&](this auto&& self,
            auto&& source,
            TypeID target,
            std::size_t depth) noexcept -> std::expected<ExecutionValue, HostError> {
        const auto mismatch = []() static noexcept {
            return std::unexpected(hosting_error(
                HostErrorKind::Arguments,
                "value does not match its declared hosted type"
            ));
        };
        if (depth > maximum_constant_aggregate_depth) {
            return std::unexpected(
                hosting_error(HostErrorKind::Arguments, "hosted value exceeds its nesting limit")
            );
        }
        const auto& shape = values.type_copy(target).value;
        if (const auto* builtin = std::get_if<BuiltinTypeValue>(&shape)) {
            if (builtin_is_integer(builtin->kind)) {
                const auto read =
                    [&]<typename Carrier>() noexcept -> std::expected<ExecutionValue, HostError> {
                    if (const auto* value = std::get_if<Carrier>(&source)) {
                        const auto number = [&]() noexcept {
                            if constexpr (std::signed_integral<Carrier>) {
                                return IntegerConstant::from_signed(*value);
                            } else {
                                return IntegerConstant::from_parts(*value, false);
                            }
                        }();
                        if (integer_constant_fits(number, builtin->kind)) {
                            return ConstantAtom {.type = target, .value = number};
                        }
                    }
                    return mismatch();
                };
                if (builtin->kind == BuiltinType::I32) {
                    return read.template operator()<std::int32_t>();
                }
                if (builtin->kind == BuiltinType::U8) {
                    return read.template operator()<std::uint8_t>();
                }
                return builtin_is_signed_integer(builtin->kind)
                    ? read.template operator()<std::int64_t>()
                    : read.template operator()<std::uint64_t>();
            } else if (builtin->kind == BuiltinType::Bool) {
                if (const auto* boolean = std::get_if<bool>(&source)) {
                    return ConstantAtom {
                        .type = target,
                        .value = BooleanConstant {.value = *boolean}
                    };
                }
            } else if (builtin->kind == BuiltinType::Void
                       && std::holds_alternative<std::monostate>(source)) {
                return ExecutionVoid {};
            } else if (builtin->kind == BuiltinType::String || builtin->kind == BuiltinType::Str) {
                if (auto* text = std::get_if<std::string>(&source)) {
                    auto checked = host_native_text_check(*text, limits);
                    if (!checked) {
                        return std::unexpected(std::move(checked.error()));
                    }
                    if (builtin->kind == BuiltinType::Str) {
                        return ExecutionText(std::forward_like<decltype(source)>(*text));
                    }
                    return ExecutionOwnedText(std::forward_like<decltype(source)>(*text));
                }
            }
            return mismatch();
        }
        if (const auto name = native_type_name(values, target)) {
            const auto registered = std::ranges::find(native_types, *name, &HostNativeType::name);
            if (registered == native_types.end() || registered->decode == nullptr) {
                return std::unexpected(hosting_error(
                    HostErrorKind::Arguments,
                    "native argument requires its registered carrier codec: " + *name
                ));
            }
            auto native = registered->decode(source, limits, depth);
            if (!native) {
                return std::unexpected(std::move(native.error()));
            }
            return ExecutionHostObject(target, std::move(*native), registered->read_borrows);
        }
        const auto* vector = std::get_if<VectorTypeValue>(&shape);
        const auto* slice = std::get_if<SliceTypeValue>(&shape);
        if (vector || slice) {
            const auto element_type = vector ? vector->element : slice->element;
            if (element_type == values.builtin_type(BuiltinType::U8)) {
                auto* bytes = std::get_if<carven::runtime::Vector<std::uint8_t>>(&source);
                if (!bytes) {
                    return mismatch();
                }
                if (!bytes->empty() && depth == maximum_constant_aggregate_depth) {
                    return std::unexpected(hosting_error(
                        HostErrorKind::Arguments,
                        "hosted value exceeds its nesting limit"
                    ));
                }
                auto checked = host_native_bytes_check(bytes->size(), limits);
                if (!checked) {
                    return std::unexpected(std::move(checked.error()));
                }
                return ExecutionBytes {
                    .type = target,
                    .element = element_type,
                    .bytes = std::forward_like<decltype(source)>(*bytes)
                };
            }
            auto* array = std::get_if<std::vector<HostValue>>(&source);
            if (!array) {
                return mismatch();
            }
            auto checked = host_native_elements(array->size(), limits);
            if (!checked) {
                return std::unexpected(std::move(checked.error()));
            }
            auto children = std::vector<ExecutionValue>();
            for (auto& child : *array) {
                auto converted =
                    self(std::forward_like<decltype(source)>(child), element_type, depth + 1uz);
                if (!converted) {
                    return std::unexpected(std::move(converted.error()));
                }
                children.push_back(std::move(*converted));
            }
            return ExecutionAggregateValue {
                .type = target,
                .elements = std::move(children),
                .capacity = array->size()
            };
        }
        if (const auto* enumeration = std::get_if<EnumTypeValue>(&shape)) {
            auto* value = std::get_if<HostEnum>(&source);
            const auto& declaration = values.enum_declaration_copy(enumeration->enumeration);
            if (!value
                || values.generic_nominal_instance_copy(
                    NominalDeclarationRef(enumeration->enumeration)
                )
                || value->module_path != values.module_path(declaration.module_id)
                || value->name != values.spelling(declaration.name)) {
                return mismatch();
            }
            const auto selected =
                std::ranges::find_if(declaration.cases, [&](EnumCaseID id) noexcept {
                    const auto entry = values.enum_case_declaration_copy(id);
                    return entry && values.spelling(entry->name) == value->case_name;
                });
            if (selected == declaration.cases.end()) {
                return mismatch();
            }
            const auto entry = values.enum_case_declaration_copy(*selected);
            if (!entry) {
                return mismatch();
            }
            const auto& item = *entry;
            if (value->payload.size() != item.payload_types.size()) {
                return mismatch();
            }
            auto checked = host_native_elements(value->payload.size(), limits);
            if (!checked) {
                return std::unexpected(std::move(checked.error()));
            }
            if (std::holds_alternative<NumericEnumRepresentation>(declaration.representation)) {
                return ExecutionValue(*item.constant);
            }
            auto payload = std::vector<ExecutionValue>();
            for (auto index = 0uz; index < value->payload.size(); ++index) {
                auto converted = self(
                    std::forward_like<decltype(source)>(value->payload[index]),
                    item.payload_types[index],
                    depth + 1uz
                );
                if (!converted) {
                    return std::unexpected(std::move(converted.error()));
                }
                payload.push_back(std::move(*converted));
            }
            return ExecutionEnumValue {
                .type = target,
                .enum_case = *selected,
                .payload = std::move(payload)
            };
        }
        const auto* structure = std::get_if<StructTypeValue>(&shape);
        auto* record = std::get_if<HostRecord>(&source);
        if (structure == nullptr || record == nullptr) {
            return mismatch();
        }
        const auto definition = values.struct_declaration_copy(structure->structure);
        if (!definition) {
            return mismatch();
        }
        const auto& declaration = *definition;
        if (declaration.kind != RecordKind::Struct
            || values.generic_nominal_instance_copy(NominalDeclarationRef(structure->structure))
            || record->module_path != values.module_path(declaration.module_id)
            || record->name != values.spelling(declaration.name)
            || record->fields.size() != declaration.fields.size()) {
            return mismatch();
        }
        auto checked = host_native_elements(record->fields.size(), limits);
        if (!checked) {
            return std::unexpected(std::move(checked.error()));
        }
        auto fields = std::vector<ExecutionValue>();
        fields.reserve(record->fields.size());
        for (auto index = 0uz; index < record->fields.size(); ++index) {
            const auto& field = declaration.fields[index];
            if (record->fields[index].name != values.spelling(field.name)) {
                return mismatch();
            }
            auto converted = self(
                std::forward_like<decltype(source)>(record->fields[index].value),
                field.type,
                depth + 1uz
            );
            if (!converted) {
                return std::unexpected(std::move(converted.error()));
            }
            fields.push_back(std::move(*converted));
        }
        return ExecutionAggregateValue {
            .type = target,
            .elements = std::move(fields),
            .capacity = declaration.fields.size()
        };
    };
    auto result = convert(std::forward<Value>(value), type, 0uz);
    if (result) {
        budget.byte_work = limits.byte_work;
        budget.aggregate_work -= aggregate_limit - limits.aggregate_work;
    }
    return result;
}

} // namespace

auto hosting_import(
    const ExecutionValueAccess& values,
    const HostValue& value,
    TypeID type,
    HostLimits& budget,
    std::span<const HostNativeType> native_types
) noexcept -> std::expected<ExecutionValue, HostError> {
    return import_host_value(values, value, type, budget, native_types);
}

auto hosting_import(
    const ExecutionValueAccess& values,
    HostValue&& value,
    TypeID type,
    HostLimits& budget,
    std::span<const HostNativeType> native_types
) noexcept -> std::expected<ExecutionValue, HostError> {
    return import_host_value(values, std::move(value), type, budget, native_types);
}

auto hosting_export(
    const ExecutionValueAccess& values,
    const ExecutionValue& value,
    TypeID type,
    HostLimits& budget,
    std::span<const HostNativeType> native_types
) noexcept -> std::expected<HostValue, HostError> {
    auto limits = budget;
    const auto aggregate_limit =
        std::min(limits.aggregate_work, maximum_constant_aggregate_elements);
    limits.aggregate_work = aggregate_limit;
    const auto convert = [&](this auto&& self,
                             const ExecutionValue& source,
                             TypeID target,
                             std::size_t depth) noexcept -> std::expected<HostValue, HostError> {
        if (depth > maximum_constant_aggregate_depth) {
            return std::unexpected(
                hosting_error(HostErrorKind::Execution, "hosted result exceeds its nesting limit")
            );
        }
        const auto& shape = values.type_copy(target).value;
        if (const auto* builtin = std::get_if<BuiltinTypeValue>(&shape)) {
            if (builtin->kind == BuiltinType::String || builtin->kind == BuiltinType::Str) {
                if (const auto text = execution_text(values, source)) {
                    auto checked = host_native_text_check(text->view(), limits);
                    if (!checked) {
                        return std::unexpected(std::move(checked.error()));
                    }
                    return HostValue(std::string(text->view()));
                }
            }
            if (const auto atom = execution_atom(values, source)) {
                if (builtin->kind == BuiltinType::Void
                    && std::holds_alternative<VoidConstant>(atom->value)) {
                    return HostValue(std::monostate {});
                }
                if (const auto* integer = std::get_if<IntegerConstant>(&atom->value);
                    builtin_is_integer(builtin->kind)
                    && integer
                    && integer_constant_fits(*integer, builtin->kind)) {
                    if (builtin_is_signed_integer(builtin->kind)) {
                        if (const auto number = integer->as_signed()) {
                            if (builtin->kind == BuiltinType::I32) {
                                return HostValue(static_cast<std::int32_t>(*number));
                            }
                            return HostValue(*number);
                        }
                    } else if (const auto number = integer->as_unsigned()) {
                        if (builtin->kind == BuiltinType::U8) {
                            return HostValue(static_cast<std::uint8_t>(*number));
                        }
                        return HostValue(*number);
                    }
                }
                if (const auto* boolean = std::get_if<BooleanConstant>(&atom->value);
                    builtin->kind == BuiltinType::Bool && boolean) {
                    return HostValue(boolean->value);
                }
            }
        }
        if (const auto name = native_type_name(values, target)) {
            const auto* object = std::get_if<ExecutionHostObject>(&source);
            const auto registered = std::ranges::find(native_types, *name, &HostNativeType::name);
            if (object
                && object->type == ConstructionTypeRef(target)
                && registered != native_types.end()
                && registered->encode != nullptr) {
                return registered->encode(object->reference, limits, depth);
            }
        }
        const auto* vector = std::get_if<VectorTypeValue>(&shape);
        const auto* slice = std::get_if<SliceTypeValue>(&shape);
        if (vector || slice) {
            const auto element_type = vector ? vector->element : slice->element;
            const auto compound = execution_compound_view(values, source);
            if (!compound) {
                return std::unexpected(
                    hosting_error(HostErrorKind::Execution, "hosted vector has no elements")
                );
            }
            if (element_type == values.builtin_type(BuiltinType::U8)) {
                if (compound->size() != 0uz && depth == maximum_constant_aggregate_depth) {
                    return std::unexpected(hosting_error(
                        HostErrorKind::Execution,
                        "hosted result exceeds its nesting limit"
                    ));
                }
                auto checked = host_native_bytes_check(compound->size(), limits);
                if (!checked) {
                    return std::unexpected(std::move(checked.error()));
                }
                if (const auto* bytes = std::get_if<ExecutionByteView>(&compound->elements)) {
                    return carven::runtime::Vector<std::uint8_t>::from_slice(
                        carven::runtime::Slice<std::uint8_t>(std::span(
                            reinterpret_cast<const std::uint8_t*>(bytes->bytes.data()),
                            bytes->bytes.size()
                        ))
                    );
                }
                auto bytes = carven::runtime::Vector<std::uint8_t>::with_capacity(compound->size());
                for (auto index = 0uz; index < compound->size(); ++index) {
                    const auto atom = compound->elements.visit([&](const auto items) noexcept {
                        if constexpr (std::same_as<
                                          std::remove_cvref_t<decltype(items[index])>,
                                          ConstantID>) {
                            return execution_atom(values, ExecutionValue(items[index]));
                        } else {
                            return execution_atom(values, items[index]);
                        }
                    });
                    const auto* integer =
                        atom ? std::get_if<IntegerConstant>(&atom->value) : nullptr;
                    if (!integer || integer->negative() || integer->magnitude() > 255u) {
                        return std::unexpected(hosting_error(
                            HostErrorKind::Execution,
                            "hosted byte vector contains a non-byte element"
                        ));
                    }
                    bytes.push(static_cast<std::uint8_t>(integer->magnitude()));
                }
                return HostValue(std::move(bytes));
            }
            auto checked = host_native_elements(compound->size(), limits);
            if (!checked) {
                return std::unexpected(std::move(checked.error()));
            }
            auto children = std::vector<HostValue>();
            for (auto index = 0uz; index < compound->size(); ++index) {
                auto child = compound->elements.visit(
                    [&](const auto items) noexcept -> std::expected<HostValue, HostError> {
                        if constexpr (std::same_as<
                                          std::remove_cvref_t<decltype(items[index])>,
                                          ConstantID>) {
                            return self(ExecutionValue(items[index]), element_type, depth + 1uz);
                        } else {
                            return self(items[index], element_type, depth + 1uz);
                        }
                    }
                );
                if (!child) {
                    return std::unexpected(std::move(child.error()));
                }
                children.push_back(std::move(*child));
            }
            return HostValue(std::move(children));
        }
        if (const auto* enumeration = std::get_if<EnumTypeValue>(&shape)) {
            const auto& declaration = values.enum_declaration_copy(enumeration->enumeration);
            auto selected = std::optional<EnumCaseID>();
            const auto atom = execution_atom(values, source);
            if (const auto* numeric =
                    atom ? std::get_if<NumericEnumConstant>(&atom->value) : nullptr) {
                selected = numeric->enum_case;
            }
            const auto compound = execution_compound_view(values, source);
            if (compound && compound->enum_case) {
                selected = compound->enum_case;
            }
            const auto entry =
                selected ? values.enum_case_declaration_copy(*selected) : std::nullopt;
            if (entry && entry->owner == enumeration->enumeration) {
                const auto& item = *entry;
                auto checked = host_native_elements(item.payload_types.size(), limits);
                if (!checked) {
                    return std::unexpected(std::move(checked.error()));
                }
                auto result = HostEnum {
                    .module_path = std::string(values.module_path(declaration.module_id)),
                    .name = std::string(values.spelling(declaration.name)),
                    .case_name = std::string(values.spelling(item.name)),
                    .payload = {}
                };
                for (auto index = 0uz; index < item.payload_types.size(); ++index) {
                    auto encoded = compound->elements.visit(
                        [&](const auto children) noexcept -> std::expected<HostValue, HostError> {
                            if constexpr (std::same_as<
                                              std::remove_cvref_t<decltype(children[index])>,
                                              ConstantID>) {
                                return self(
                                    ExecutionValue(children[index]),
                                    item.payload_types[index],
                                    depth + 1uz
                                );
                            } else {
                                return self(
                                    children[index],
                                    item.payload_types[index],
                                    depth + 1uz
                                );
                            }
                        }
                    );
                    if (!encoded) {
                        return std::unexpected(std::move(encoded.error()));
                    }
                    result.payload.push_back(std::move(*encoded));
                }
                return HostValue(std::move(result));
            }
        }
        if (const auto* structure = std::get_if<StructTypeValue>(&shape)) {
            const auto definition = values.struct_declaration_copy(structure->structure);
            const auto compound = execution_compound_view(values, source);
            if (definition && compound && compound->size() == definition->fields.size()) {
                const auto& declaration = *definition;
                auto checked = host_native_elements(compound->size(), limits);
                if (!checked) {
                    return std::unexpected(std::move(checked.error()));
                }
                auto record = HostRecord {
                    .module_path = std::string(values.module_path(declaration.module_id)),
                    .name = std::string(values.spelling(declaration.name)),
                    .fields = {}
                };
                for (auto index = 0uz; index < declaration.fields.size(); ++index) {
                    const auto& field = declaration.fields[index];
                    auto converted = compound->elements.visit(
                        [&](const auto children) noexcept -> std::expected<HostValue, HostError> {
                            if constexpr (std::same_as<
                                              std::remove_cvref_t<decltype(children[index])>,
                                              ConstantID>) {
                                return self(
                                    ExecutionValue(children[index]),
                                    field.type,
                                    depth + 1uz
                                );
                            } else {
                                return self(children[index], field.type, depth + 1uz);
                            }
                        }
                    );
                    if (!converted) {
                        return std::unexpected(std::move(converted.error()));
                    }
                    record.fields.push_back(
                        {.name = std::string(values.spelling(field.name)),
                         .value = std::move(*converted)}
                    );
                }
                return HostValue(std::move(record));
            }
        }
        return std::unexpected(hosting_error(
            HostErrorKind::Execution,
            "execution result could not be exported as an owning host value"
        ));
    };
    auto result = convert(value, type, 0uz);
    if (!result) {
        result.error().kind = HostErrorKind::Execution;
    } else {
        budget.byte_work = limits.byte_work;
        budget.aggregate_work -= aggregate_limit - limits.aggregate_work;
    }
    return result;
}

extern "C++" {

auto HostRecord::field(std::string_view name) const noexcept -> const HostValue* {
    const auto found = std::ranges::find(fields, name, &HostField::name);
    return found == fields.end() ? nullptr : &found->value;
}

} // extern "C++"

auto hosting_failure_type(const ExecutionValueAccess& values, TypeID type) noexcept
    -> std::optional<HostFailureType> {
    const auto& shape = values.type_copy(type).value;
    auto result = std::optional<HostFailureType>();
    const auto describe = [&](const auto& declaration) noexcept {
        result = HostFailureType {
            .module_path = std::string(values.module_path(declaration.module_id)),
            .name = std::string(values.spelling(declaration.name)),
            .type = {.describe = nullptr, .allow_cpp = false}
        };
    };
    if (const auto* record = std::get_if<StructTypeValue>(&shape)) {
        const auto declaration = values.struct_declaration_copy(record->structure);
        if (declaration
            && declaration->kind == RecordKind::Struct
            && !values.generic_nominal_instance_copy(NominalDeclarationRef(record->structure))) {
            describe(*declaration);
        }
    } else if (const auto* enumeration = std::get_if<EnumTypeValue>(&shape)) {
        if (!values.generic_nominal_instance_copy(
                NominalDeclarationRef(enumeration->enumeration)
            )) {
            describe(values.enum_declaration_copy(enumeration->enumeration));
        }
    }
    return result;
}

auto hosting_service_error(ProgramOriginID origin, std::string message) noexcept
    -> std::unexpected<ExecutionCallFailure> {
    return std::unexpected<ExecutionCallFailure>(ExecutionEvent {
        .origin = origin,
        .cause =
            ExecutionIssue {
                .reason = ExecutionReason::Admission,
                .message = std::move(message),
                .termination = ExecutionTermination::StopRoot
            },
        .fields = {},
        .calls = {},
        .blocks = {}
    });
}

extern "C++" {

auto host_native_elements(std::size_t count, HostLimits& budget) noexcept
    -> std::expected<void, HostError> {
    const auto error =
        carven::runtime::hosting::detail::native_elements_error(count, budget.aggregate_work);
    if (!error.empty()) {
        return std::unexpected(hosting_error(HostErrorKind::Arguments, std::string(error)));
    }
    return {};
}

auto host_native_text_check(std::string_view text, HostLimits& budget) noexcept
    -> std::expected<void, HostError> {
    auto checked = host_check_text(text, budget);
    if (checked) {
        budget.byte_work -= text.size();
    }
    return checked;
}

auto host_native_bytes_check(std::size_t size, HostLimits& budget) noexcept
    -> std::expected<void, HostError> {
    const auto error = carven::runtime::hosting::detail::native_bytes_error(
        size,
        budget.byte_work,
        maximum_constant_bytes
    );
    if (!error.empty()) {
        return std::unexpected(hosting_error(HostErrorKind::Arguments, std::string(error)));
    }
    return {};
}

} // extern "C++"
