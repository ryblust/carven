module carven:semantic.analysis.types.display.impl;

import :semantic.analysis.catalog;
import :semantic.analysis.program;
import :semantic.analysis.types.display;
import :semantic.semir.decl;
import :semantic.semir.delegation;
import :semantic.semir.generic;
import :semantic.semir.ids;
import :semantic.semir.operation;
import :semantic.semir.type;
import :source.provenance.ids;
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

        case BuiltinType::U8x16:  return "u8x16";
        case BuiltinType::Mask16: return "mask16";
        case BuiltinType::F32x4:  return "f32x4";
        case BuiltinType::Mask4:  return "mask4";
        case BuiltinType::U8x32:  return "u8x32";
        case BuiltinType::Mask32: return "mask32";
        case BuiltinType::F32x8:  return "f32x8";
        case BuiltinType::Mask8:  return "mask8";
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

template<typename Elements, typename Name>
auto tuple_name(const Elements& elements, const Name& name) noexcept -> std::string {
    auto text = std::string("(");
    for (auto index = 0uz; index < elements.size(); ++index) {
        if (index > 0uz) {
            text += ", ";
        }
        text += name(elements[index]);
    }
    if (elements.size() == 1uz) {
        text += ',';
    }
    text += ')';
    return text;
}

auto nominal_name(const ProgramDraft& draft, ModuleID module, ProgramSpellingID name) noexcept
    -> std::string {
    return std::format(
        "{}::{}",
        draft.module_path_copy(draft.module_declaration_copy(module).provenance_module).value(),
        draft.spelling_copy(name)
    );
}

auto nominal_name(const ProgramDraft& draft, const CatalogSymbol& symbol) noexcept -> std::string {
    return std::format("{}::{}", draft.module_path_copy(symbol.module_id).value(), symbol.name);
}

auto canonical_name(
    const ProgramDraft& draft,
    TypeID type,
    std::optional<AnalysisCatalogView> catalog
) noexcept -> std::string {
    const auto name = [&](TypeID nested) noexcept {
        return canonical_name(draft, nested, catalog);
    };
    const auto contract = [&](CallableID callable) noexcept {
        const auto value = draft.construction_callable_contract_copy(callable);
        return callable_name(
            value.parameters,
            type_display_name(draft, value.result, catalog),
            [&](ConstructionTypeRef nested) noexcept {
                return type_display_name(draft, nested, catalog);
            }
        );
    };
    return draft.type_copy(type).value.visit(
        Overloaded {
            [](const BuiltinTypeValue& value) static noexcept {
                return std::string(builtin_name(value.kind));
            },
            [&](const StructTypeValue& value) noexcept {
                if (catalog && value.structure.index() < catalog->struct_count()) {
                    return nominal_name(
                        draft,
                        *catalog->symbol(catalog->struct_symbol(value.structure))
                    );
                }
                const auto declaration =
                    draft.construction_struct_declaration_copy(value.structure);
                return nominal_name(draft, declaration.module_id, declaration.name);
            },
            [&](const EnumTypeValue& value) noexcept {
                if (catalog && value.enumeration.index() < catalog->enum_count()) {
                    return nominal_name(
                        draft,
                        *catalog->symbol(catalog->enum_symbol(value.enumeration))
                    );
                }
                const auto declaration = draft.enum_declaration_copy(value.enumeration);

                return nominal_name(draft, declaration.module_id, declaration.name);
            },
            [&](const ArrayTypeValue& value) noexcept {
                return std::format("[{}; {}]", name(value.element), value.extent);
            },
            [&](const TupleTypeValue& value) noexcept { return tuple_name(value.elements, name); },
            [&](const SliceTypeValue& value) noexcept {
                return std::format("[{}]", name(value.element));
            },
            [&](const VectorTypeValue& value) noexcept {
                return std::format("Vector<{}>", name(value.element));
            },
            [&](const RangeTypeValue& value) noexcept {
                return std::format("Range<{}>", name(value.element));
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
            [&](const OperationTypeValue& value) noexcept {
                return std::format("Operation<{}>", name(value.success));
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

auto type_display_name(
    const ProgramDraft& draft,
    ConstructionTypeRef type,
    std::optional<AnalysisCatalogView> catalog
) noexcept -> std::string {
    if (const auto* canonical = std::get_if<TypeID>(&type)) {
        return canonical_name(draft, *canonical, catalog);
    }
    const auto name = [&](ConstructionTypeRef nested) noexcept {
        return type_display_name(draft, nested, catalog);
    };
    if (const auto* generic = std::get_if<GenericTypeID>(&type)) {
        return draft.generic_type_copy(*generic).visit(
            Overloaded {
                [&](TypeID value) noexcept { return name(value); },
                [&](const GenericTypeParameter& value) noexcept {
                    const auto contract = draft.generic_declaration_contract_copy(value.definition);
                    return draft.spelling_copy(contract.parameters[value.index]);
                },

                [&](const GenericArrayType& value) noexcept {
                    return std::format("[{}; {}]", name(value.element), value.extent);
                },
                [&](const GenericTupleType& value) noexcept {
                    return tuple_name(value.elements, name);
                },
                [&](const GenericSliceType& value) noexcept {
                    return std::format("[{}]", name(value.element));
                },
                [&](const GenericVectorType& value) noexcept {
                    return std::format("Vector<{}>", name(value.element));
                },
                [&](const GenericPointerType& value) noexcept {
                    return std::format(
                        "ptr<{}{}>",
                        value.access == PointerAccess::Write ? "&" : "",
                        name(value.target)
                    );
                },
                [&](const GenericOperationType& value) noexcept {
                    return std::format("Operation<{}>", name(value.success));
                },
                [&](const GenericCppNamedType& value) noexcept {
                    auto text = std::string("::");
                    for (const auto [index, component] :
                         std::views::enumerate(value.name.components)) {
                        if (index != 0) {
                            text += "::";
                        }
                        text += component;
                    }
                    text += "<";
                    for (const auto [index, argument] : std::views::enumerate(value.arguments)) {
                        if (index != 0) {
                            text += ", ";
                        }
                        text += name(argument);
                    }
                    return text + ">";
                },
                [](const GenericCppQueryType&) static noexcept {
                    return std::string("C++ expression");
                },
                [&](const GenericNominalApplication& value) noexcept {
                    const auto contract = draft.generic_declaration_contract_copy(value.definition);
                    auto text = nominal_name(draft, contract.module_id, contract.name) + "<";
                    for (const auto [index, argument] : std::views::enumerate(value.arguments)) {
                        if (index != 0) {
                            text += ", ";
                        }
                        if (const auto* type = std::get_if<ConstructionTypeRef>(&argument)) {
                            text += name(*type);
                        } else if (const auto* source_effect =
                                       std::get_if<ConstructionFailureRef>(&argument)) {
                            text += "throw ";
                            const auto& effect = *source_effect;
                            if (const auto* known = std::get_if<FailureSetID>(&effect)) {
                                auto first = true;
                                for (const auto member : draft.failure_set_copy(*known).members) {
                                    if (!first) {
                                        text += "+";
                                    }
                                    first = false;
                                    text += name(member);
                                }
                                if (first) {
                                    text += "()";
                                }
                            } else {
                                text += "<dependent>";
                            }
                        }
                    }
                    return text + ">";
                },
            }
        );
    }
    return draft.construction_type_copy(std::get<TypeTermID>(type))
        .value.visit(
            Overloaded {
                [&](const ConstructionArrayTypeValue& value) noexcept {
                    return std::format("[{}; {}]", name(value.element), value.extent);
                },
                [&](const ConstructionTupleTypeValue& value) noexcept {
                    return value.elements.empty() ? std::string("void")
                                                  : tuple_name(value.elements, name);
                },
                [&](const ConstructionSliceTypeValue& value) noexcept {
                    return std::format("[{}]", name(value.element));
                },
                [&](const ConstructionOperationTypeValue& value) noexcept {
                    return std::format("Operation<{}>", name(value.success));
                },
                [&](const ConstructionCallableViewTypeValue& value) noexcept {
                    return callable_name(value.parameters, name(value.result), name);
                }
            }
        );
}
