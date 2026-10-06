module carven:semantic.analysis.types.display.impl;

import :semantic.analysis.program;
import :semantic.analysis.types.display;
import :semantic.semir.decl;
import :semantic.semir.delegation;
import :semantic.semir.ids;
import :semantic.semir.operation;
import :semantic.semir.type;
import :support.visit;
import std;

namespace {

auto builtin_name(BuiltinType type) noexcept -> std::string_view {
    switch (type) {
        case BuiltinType::Bool:         return "bool";
        case BuiltinType::Char:         return "char";
        case BuiltinType::I8:           return "i8";
        case BuiltinType::I16:          return "i16";
        case BuiltinType::I32:          return "i32";
        case BuiltinType::I64:          return "i64";
        case BuiltinType::U8:           return "u8";
        case BuiltinType::U16:          return "u16";
        case BuiltinType::U32:          return "u32";
        case BuiltinType::U64:          return "u64";
        case BuiltinType::Isize:        return "isize";
        case BuiltinType::Usize:        return "usize";
        case BuiltinType::F32:          return "f32";
        case BuiltinType::F64:          return "f64";
        case BuiltinType::String:       return "String";
        case BuiltinType::Str:          return "str";
        case BuiltinType::StrCharsView: return "str chars view";
        case BuiltinType::Void:         return "void";
        case BuiltinType::EntryArgs:    return "entry arguments";
        case BuiltinType::U8x16:        return "u8x16";
        case BuiltinType::Mask16:       return "mask16";
        case BuiltinType::F32x4:        return "f32x4";
        case BuiltinType::Mask4:        return "mask4";
        case BuiltinType::U8x32:        return "u8x32";
        case BuiltinType::Mask32:       return "mask32";
        case BuiltinType::F32x8:        return "f32x8";
        case BuiltinType::Mask8:        return "mask8";
    }
    std::unreachable();
}

auto access_marker(AccessMode access) noexcept -> std::string_view {
    switch (access) {
        case AccessMode::Read:  return "";
        case AccessMode::Write: return "&";
        case AccessMode::Take:  return "&&";
    }
    std::unreachable();
}

template<typename Parameters, typename Name>
auto callable_name(const Parameters& parameters, std::string_view result, const Name& name) noexcept
    -> std::string {
    auto text = std::string("fn(");
    for (auto index = 0uz; index < parameters.size(); ++index) {
        if (index > 0) {
            text += ", ";
        }
        if (parameters[index].stage == ParameterStage::Static) {
            text += "const ";
        }
        text += access_marker(parameters[index].access);
        text += name(parameters[index].type);
    }
    text += ") -> ";
    text += result;
    return text;
}

auto canonical_name(const ProgramDraft& draft, TypeID type) noexcept -> std::string {
    const auto name = [&](TypeID nested) noexcept {
        return canonical_name(draft, nested);
    };
    const auto contract = [&](CallableID callable) noexcept {
        const auto value = draft.construction_callable_contract_copy(callable);
        return callable_name(
            value.parameters,
            type_display_name(draft, value.result),
            [&](ConstructionTypeRef nested) noexcept { return type_display_name(draft, nested); }
        );
    };
    return draft.type_copy(type).value.visit(
        Overloaded {
            [](const BuiltinTypeValue& value) static noexcept {
                return std::string(builtin_name(value.kind));
            },
            [&](const StructTypeValue& value) noexcept {
                return draft.spelling_copy(
                    draft.construction_struct_declaration_copy(value.structure).name
                );
            },
            [&](const EnumTypeValue& value) noexcept {
                return draft.spelling_copy(draft.enum_declaration_copy(value.enumeration).name);
            },
            [&](const ArrayTypeValue& value) noexcept {
                return std::format("[{}; {}]", name(value.element), value.extent);
            },
            [&](const SliceTypeValue& value) noexcept {
                return std::format("[{}]", name(value.element));
            },
            [&](const OwnedSequenceTypeValue& value) noexcept {
                return std::format("Sequence<{}>", name(value.element));
            },
            [&](const RangeTypeValue& value) noexcept {
                return std::format("range<{}>", name(value.element));
            },
            [&](const PointerTypeValue& value) noexcept {
                return std::format(
                    "ptr<{}{}>",
                    value.access == PointerAccess::Write ? "&" : "",
                    name(value.target)
                );
            },
            [&](const FunctionTypeValue& value) noexcept { return contract(value.callable); },
            [&](const ClosureTypeValue& value) noexcept { return contract(value.callable); },
            [&](const CallableViewTypeValue& value) noexcept {
                const auto signature = draft.callable_signature_copy(value.signature);
                return callable_name(signature.parameters, name(signature.result), name);
            },
            [](const CppTypeValue& value) static noexcept {
                const auto* reference = cpp_type_name(value);
                if (reference == nullptr || reference->components.empty()) {
                    return std::string("C++ type");
                }
                auto text = std::string();
                for (const auto& component : reference->components) {
                    if (!text.empty()) {
                        text += "::";
                    }
                    text += component;
                }
                return text;
            }
        }
    );
}

} // namespace

auto type_display_name(const ProgramDraft& draft, ConstructionTypeRef type) noexcept
    -> std::string {
    if (const auto* canonical = std::get_if<TypeID>(&type)) {
        return canonical_name(draft, *canonical);
    }
    const auto name = [&](ConstructionTypeRef nested) noexcept {
        return type_display_name(draft, nested);
    };
    return draft.construction_type_copy(std::get<TypeTermID>(type))
        .value.visit(
            Overloaded {
                [&](const ConstructionArrayTypeValue& value) noexcept {
                    return std::format("[{}; {}]", name(value.element), value.extent);
                },
                [&](const ConstructionSliceTypeValue& value) noexcept {
                    return std::format("[{}]", name(value.element));
                },
                [&](const ConstructionCallableViewTypeValue& value) noexcept {
                    return callable_name(value.parameters, name(value.result), name);
                }
            }
        );
}
