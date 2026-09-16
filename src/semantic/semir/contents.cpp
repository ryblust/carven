module carven:semantic.semir.contents.impl;

import :semantic.semir.contents;
import :semantic.semir.decl;
import :semantic.semir.type;
import :support.invariant;
import :support.visit;
import std;

namespace {
// Contents describe resolved types and do not traverse pointer targets.
template<typename Types, typename Declarations>
class TypeContentsQuery final {
public:
    TypeContentsQuery(const Types& types, const Declarations& declarations) noexcept;
    auto contents(TypeID type) noexcept -> TypeContents;
    auto contents(ConstructionTypeRef type) noexcept -> TypeContents;

private:
    const Types& types;
    const Declarations& declarations;
    std::map<TypeID, TypeContents> contents_by_type;
};

template<typename Types, typename Declarations>
TypeContentsQuery<Types, Declarations>::TypeContentsQuery(
    const Types& types,
    const Declarations& declarations
) noexcept
    : types(types),
      declarations(declarations) {
    if (types.owner() != declarations.owner()) {
        invariant_violation("type contents inputs belong to different programs");
    }
}

template<typename Types, typename Declarations>
auto TypeContentsQuery<Types, Declarations>::contents(ConstructionTypeRef type) noexcept
    -> TypeContents {
    const auto* concrete = std::get_if<TypeID>(&type);
    if (concrete == nullptr) {
        invariant_violation("type contents query requires completed declaration fields");
    }
    return contents(*concrete);
}

template<typename Types, typename Declarations>
auto TypeContentsQuery<Types, Declarations>::contents(TypeID type) noexcept -> TypeContents {
    if (const auto found = contents_by_type.find(type); found != contents_by_type.end()) {
        return found->second;
    }
    const auto& canonical = [&]() noexcept -> decltype(auto) {
        if constexpr (std::same_as<Types, CanonicalTypeStore>) {
            return types.type(type);
        } else {
            return types.copy(type);
        }
    }();
    // Recursive containment contributes no additional contents on this path.
    auto& cached = contents_by_type[type];
    cached = TypeContents {.closure_owner = false, .callable_view = false, .storage_owner = false};
    const auto merge = [](TypeContents& destination, TypeContents source) static noexcept {
        destination.closure_owner |= source.closure_owner;
        destination.callable_view |= source.callable_view;
        destination.storage_owner |= source.storage_owner;
    };
    const auto result = std::visit(
        Overloaded {
            [](const ClosureTypeValue&) static noexcept -> TypeContents {
                return {.closure_owner = true, .callable_view = false, .storage_owner = false};
            },
            [](const CallableViewTypeValue&) static noexcept -> TypeContents {
                return {.closure_owner = false, .callable_view = true, .storage_owner = false};
            },
            [&](const ArrayTypeValue& value) noexcept {
                auto result = contents(value.element);
                result.storage_owner = true;
                return result;
            },
            [&](const StructTypeValue& value) noexcept {
                auto result = TypeContents {
                    .closure_owner = false,
                    .callable_view = false,
                    .storage_owner = false
                };
                const auto& declaration = declarations.structure(value.structure);
                for (const auto& field : declaration.fields) {
                    merge(result, contents(field.type));
                }
                return result;
            },
            [&](const EnumTypeValue& value) noexcept {
                auto result = TypeContents {
                    .closure_owner = false,
                    .callable_view = false,
                    .storage_owner = false
                };
                const auto& declaration = declarations.enumeration(value.enumeration);
                for (const auto case_id : declaration.cases) {
                    const auto& member = declarations.enum_case(case_id);
                    for (const auto element : member.payload_types) {
                        merge(result, contents(element));
                    }
                }
                return result;
            },
            [&](const SliceTypeValue& value) noexcept -> TypeContents {
                return {
                    .closure_owner = false,
                    .callable_view = contents(value.element).callable_view,
                    .storage_owner = false
                };
            },
            [](const RangeTypeValue&) static noexcept -> TypeContents {
                return {.closure_owner = false, .callable_view = false, .storage_owner = false};
            },
            [](const PointerTypeValue&) static noexcept -> TypeContents {
                return {.closure_owner = false, .callable_view = false, .storage_owner = false};
            },
            [](const BuiltinTypeValue& value) static noexcept -> TypeContents {
                return {
                    .closure_owner = false,
                    .callable_view = false,
                    .storage_owner = value.kind == BuiltinType::String
                };
            },
            [&](const CppTypeValue& value) noexcept -> TypeContents {
                auto result = TypeContents {
                    .closure_owner = false,
                    .callable_view = false,
                    .storage_owner = false
                };
                if (const auto* named = std::get_if<CppNamedType>(&value.form)) {
                    for (const auto argument : named->arguments) {
                        result.callable_view |= contents(argument).callable_view;
                    }
                }
                return result;
            },
            [](const FunctionTypeValue&) static noexcept -> TypeContents {
                return {.closure_owner = false, .callable_view = false, .storage_owner = false};
            },
        },
        canonical.value
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

auto TypeContents::read_borrows_storage() const noexcept -> bool {
    return storage_owner || closure_owner;
}

auto query_type_contents(
    const CanonicalTypeStoreBuilder& types,
    DeclarationConstructionView declarations,
    TypeID type
) noexcept -> TypeContents {
    return TypeContentsQuery(types, declarations).contents(type);
}
