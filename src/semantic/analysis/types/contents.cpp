module carven:semantic.analysis.types.contents.impl;

import :semantic.analysis.types.contents;
import :support.invariant;
import :support.visit;
import std;

TypeContentsQuery::TypeContentsQuery(ProgramDraft& program) noexcept
    : draft(program) {}

auto TypeContentsQuery::contents(TypeID type) noexcept -> TypeContents {
    if (type.owner() != draft.identity()) {
        invariant_violation("type contents query observed a foreign type");
    }
    if (const auto found = memo.find(type); found != memo.end()) {
        return found->second;
    }
    if (!visiting.insert(type).second) {
        return {.closure_owner = false, .callable_view = false};
    }
    const auto merge = [](TypeContents& destination, TypeContents source) static noexcept {
        destination.closure_owner |= source.closure_owner;
        destination.callable_view |= source.callable_view;
    };
    const auto result = std::visit(
        Overloaded {
            [](const ClosureTypeValue&) static noexcept -> TypeContents {
                return {.closure_owner = true, .callable_view = false};
            },
            [](const CallableViewTypeValue&) static noexcept -> TypeContents {
                return {.closure_owner = false, .callable_view = true};
            },
            [&](const ArrayTypeValue& value) noexcept { return contents(value.element); },
            [&](const StructTypeValue& value) noexcept {
                auto result = TypeContents {.closure_owner = false, .callable_view = false};
                const auto declaration =
                    draft.construction_struct_declaration_copy(value.structure);
                for (const auto& field : declaration.fields) {
                    merge(result, contents(draft.concrete_type(field.type)));
                }
                return result;
            },
            [&](const EnumTypeValue& value) noexcept {
                auto result = TypeContents {.closure_owner = false, .callable_view = false};
                const auto declaration =
                    draft.construction_enum_declaration_copy(value.enumeration);
                for (const auto case_id : declaration.cases) {
                    const auto member = draft.construction_enum_case_declaration_copy(case_id);
                    for (const auto element : member.payload_types) {
                        merge(result, contents(draft.concrete_type(element)));
                    }
                }
                return result;
            },
            [](const BuiltinTypeValue&) static noexcept -> TypeContents {
                return {.closure_owner = false, .callable_view = false};
            },
            [&](const CppTypeValue& value) noexcept -> TypeContents {
                auto result = TypeContents {.closure_owner = false, .callable_view = false};
                if (const auto* named = std::get_if<CppNamedType>(&value.form)) {
                    for (const auto argument : named->arguments) {
                        merge(result, contents(argument));
                    }
                }
                return result;
            },
            [](const FunctionTypeValue&) static noexcept -> TypeContents {
                return {.closure_owner = false, .callable_view = false};
            },
        },
        draft.canonical_type_copy(type).value
    );
    visiting.erase(type);
    memo.emplace(type, result);
    return result;
}

auto TypeContentsQuery::contains_owner(std::variant<TypeID, FailureSetID> type) noexcept -> bool {
    const auto* language = std::get_if<TypeID>(&type);
    return language != nullptr && contents(*language).closure_owner;
}

auto TypeContentsQuery::contains_view(std::variant<TypeID, FailureSetID> type) noexcept -> bool {
    const auto* language = std::get_if<TypeID>(&type);
    return language != nullptr && contains_view(*language);
}

auto TypeContentsQuery::contains_view(TypeID type) noexcept -> bool {
    return contents(type).callable_view;
}
