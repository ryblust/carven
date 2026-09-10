module carven:semantic.analysis.types.contents.impl;

import :semantic.analysis.types.contents;
import :support.invariant;
import :support.visit;
import std;

namespace {
// This query is scoped to analysis after construction types have been solved.
class TypeContentsQuery final {
public:
    TypeContentsQuery(
        const CanonicalTypeStore& types,
        const DeclarationStore& declarations
    ) noexcept;
    auto contents(TypeID type) noexcept -> TypeContents;

private:
    const CanonicalTypeStore& types;
    const DeclarationStore& declarations;
    std::vector<std::optional<TypeContents>> contents_by_type;
};

TypeContentsQuery::TypeContentsQuery(
    const CanonicalTypeStore& types,
    const DeclarationStore& declarations
) noexcept
    : types(types),
      declarations(declarations),
      contents_by_type(types.size()) {
    if (types.owner() != declarations.owner()) {
        invariant_violation("type contents inputs belong to different programs");
    }
}

auto TypeContentsQuery::contents(TypeID type) noexcept -> TypeContents {
    if (!types.contains(type)) {
        invariant_violation("type contents query observed a foreign or invalid type");
    }
    auto& cached = contents_by_type[type.index()];
    if (cached.has_value()) {
        return *cached;
    }
    // Recursive containment contributes no additional contents on this path.
    cached = TypeContents {.closure_owner = false, .callable_view = false};
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
                const auto& declaration = declarations.structure(value.structure);
                for (const auto& field : declaration.fields) {
                    merge(result, contents(field.type));
                }
                return result;
            },
            [&](const EnumTypeValue& value) noexcept {
                auto result = TypeContents {.closure_owner = false, .callable_view = false};
                const auto& declaration = declarations.enumeration(value.enumeration);
                for (const auto case_id : declaration.cases) {
                    const auto& member = declarations.enum_case(case_id);
                    for (const auto element : member.payload_types) {
                        merge(result, contents(element));
                    }
                }
                return result;
            },
            [](const PointerTypeValue&) static noexcept -> TypeContents {
                return {.closure_owner = false, .callable_view = false};
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
        types.type(type).value
    );
    cached = result;
    return result;
}

} // namespace

auto compute_type_contents(
    const CanonicalTypeStore& types,
    const DeclarationStore& declarations
) noexcept -> std::vector<TypeContents> {
    auto query = TypeContentsQuery(types, declarations);
    auto facts = std::vector<TypeContents>();
    facts.reserve(types.size());
    for (const auto [id, type] : types.entries()) {
        static_cast<void>(type);
        facts.push_back(query.contents(id));
    }
    return facts;
}
