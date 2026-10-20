module carven:semantic.semir.contents;

import :semantic.semir.decl;
import :semantic.semir.generic;
import :semantic.semir.type;
import std;

inline constexpr auto closure_owner_content = 1u;
inline constexpr auto callable_view_content = 2u;
inline constexpr auto storage_owner_content = 4u;
inline constexpr auto native_value_content = 8u;
inline constexpr auto string_storage_content = 16u;
inline constexpr auto operation_owner_content = 32u;
inline constexpr auto storage_view_content = 64u;
inline constexpr auto parametric_value_content = 128u;
inline constexpr auto value_storage_constraint = 256u;
inline constexpr auto all_type_contents = 511u;

struct TypeContentDependent final {
    std::size_t index;
    unsigned mask;
};

// Containment facts include the type itself and propagate through
// struct fields, tuple and array elements, and enum payloads; pointers propagate none.
struct TypeContents final {
    bool contains_operation_owner;
    bool contains_closure_owner;
    // Also propagates through slice element types and native C++ template arguments.
    bool contains_callable_view;
    // Owned Array, Vector or String storage.
    bool contains_storage_owner;
    // Borrowed Str, StrCharsView, or Slice held by value; empty arrays contain none.
    bool contains_storage_view;
    // Opaque native C++ values held by value, including inside Carven aggregates.
    // Compiler-known C string pointers propagate no contents.
    bool contains_native_value;
    // Possible String storage held by value; zero-length arrays stop propagation.
    // Implies contains_storage_owner.
    bool contains_string_storage;

    // Rigid definition value with relationships described by its input role.
    bool contains_parametric_value;

    // Internal value transport is proven only for plain data. This conservative
    // constraint also propagates through zero-length arrays; it does not deny
    // source transfer or require a particular storage layout for other values.
    bool has_value_storage_constraint;

    auto mask() const noexcept -> unsigned;

    auto contains_carven_owner() const noexcept -> bool;

    // Read may borrow owned storage, closures, and opaque native values.
    auto read_borrows_storage() const noexcept -> bool;
    // Read is a value snapshot with no owned Carven storage or opaque native value.
    auto read_is_value_snapshot() const noexcept -> bool;
    auto can_store_inline() const noexcept -> bool;
};

auto compute_type_contents(
    const CanonicalTypeStore& types,
    const DeclarationStore& declarations
) noexcept -> std::vector<TypeContents>;

// Referenced declaration fields must have completed concrete types.
auto query_type_contents(
    const CanonicalTypeStoreBuilder& types,
    DeclarationConstructionView declarations,
    TypeID type
) noexcept -> TypeContents;

class ExecutionValueAccess;

auto query_type_contents(const ExecutionValueAccess& values, ConstructionTypeRef type) noexcept
    -> TypeContents;

// Dependencies carry only the contents relevant to their enclosing shape.
// The same finite worklist handles concrete and rigid recursive value shapes.
auto solve_type_content_graph(
    std::vector<unsigned> contents,
    std::span<const std::vector<TypeContentDependent>> dependents
) noexcept -> std::vector<TypeContents>;

// Shape readers supply nominal members; this rule owns direct contents and
// the masks carried by structural children at every type-completion stage.
template<typename Shape, typename Members, typename Depend>
auto type_content_mask(const Shape& shape, Members members, Depend depend) noexcept -> unsigned {
    if constexpr (std::same_as<Shape, OperationTypeValue>
                  || std::same_as<Shape, ConstructionOperationTypeValue>
                  || std::same_as<Shape, GenericOperationType>) {
        return operation_owner_content | value_storage_constraint;
    } else if constexpr (std::same_as<Shape, ClosureTypeValue>) {
        return closure_owner_content | value_storage_constraint;
    } else if constexpr (std::same_as<Shape, FunctionTypeValue>) {
        return value_storage_constraint;
    } else if constexpr (std::same_as<Shape, CallableViewTypeValue>
                         || std::same_as<Shape, ConstructionCallableViewTypeValue>
                         || std::same_as<Shape, GenericCallableViewType>) {
        return callable_view_content | value_storage_constraint;
    } else if constexpr (std::same_as<Shape, VectorTypeValue>
                         || std::same_as<Shape, GenericVectorType>) {
        depend(shape.element, all_type_contents);
        return storage_owner_content | value_storage_constraint;
    } else if constexpr (std::same_as<Shape, ArrayTypeValue>
                         || std::same_as<Shape, ConstructionArrayTypeValue>
                         || std::same_as<Shape, GenericArrayType>) {
        depend(
            shape.element,
            shape.extent == 0u ? all_type_contents
                    & ~(string_storage_content | storage_view_content | parametric_value_content)
                               : all_type_contents
        );
        return storage_owner_content;
    } else if constexpr (std::same_as<Shape, TupleTypeValue>
                         || std::same_as<Shape, ConstructionTupleTypeValue>
                         || std::same_as<Shape, GenericTupleType>) {
        for (const auto element : shape.elements) {
            depend(element, all_type_contents);
        }
    } else if constexpr (std::same_as<Shape, StructTypeValue>
                         || std::same_as<Shape, EnumTypeValue>
                         || std::same_as<Shape, GenericNominalApplication>) {
        for (const auto member : members(shape)) {
            depend(member, all_type_contents);
        }
    } else if constexpr (std::same_as<Shape, SliceTypeValue>
                         || std::same_as<Shape, ConstructionSliceTypeValue>
                         || std::same_as<Shape, GenericSliceType>) {
        depend(shape.element, callable_view_content);
        return storage_view_content | value_storage_constraint;
    } else if constexpr (std::same_as<Shape, BuiltinTypeValue>) {
        if (shape.kind == BuiltinType::String) {
            return storage_owner_content | string_storage_content | value_storage_constraint;
        }
        if (shape.kind == BuiltinType::Str || shape.kind == BuiltinType::StrCharsView) {
            return storage_view_content | value_storage_constraint;
        }
    } else if constexpr (std::same_as<Shape, CppTypeValue>) {
        if (std::holds_alternative<CppConstCharPointerType>(shape.form)) {
            return 0u;
        }
        if (const auto* named = std::get_if<CppNamedType>(&shape.form)) {
            for (const auto argument : named->arguments) {
                depend(argument, callable_view_content);
            }
        }
        return native_value_content | value_storage_constraint;
    } else if constexpr (std::same_as<Shape, GenericCppNamedType>) {
        for (const auto argument : shape.arguments) {
            depend(argument, callable_view_content);
        }
        return native_value_content | value_storage_constraint;
    } else if constexpr (std::same_as<Shape, GenericCppQueryType>) {
        return native_value_content | value_storage_constraint;
    } else if constexpr (std::same_as<Shape, GenericTypeParameter>) {
        return parametric_value_content | value_storage_constraint;
    }
    return 0u;
}

// Descriptions contribute direct masks and dependency edges. Returned facts
// include the roots followed by their reachable children.
template<typename Describe>
auto collect_type_contents(std::span<const ConstructionTypeRef> roots, Describe describe) noexcept
    -> std::vector<std::pair<ConstructionTypeRef, TypeContents>> {
    auto indices = std::map<ConstructionTypeRef, std::size_t>();
    auto reachable = std::vector<ConstructionTypeRef>();
    auto masks = std::vector<unsigned>();
    auto dependents = std::vector<std::vector<TypeContentDependent>>();
    const auto intern = [&](ConstructionTypeRef type) noexcept {
        const auto [entry, inserted] = indices.try_emplace(type, reachable.size());
        if (inserted) {
            reachable.push_back(type);
            masks.push_back(0u);
            dependents.emplace_back();
        }
        return entry->second;
    };
    for (const auto type : roots) {
        intern(type);
    }
    for (auto index = 0uz; index < reachable.size(); ++index) {
        const auto depend = [&](ConstructionTypeRef type, unsigned mask) noexcept {
            const auto child = intern(type);
            dependents[child].push_back({index, mask});
        };
        masks[index] = describe(reachable[index], depend);
    }
    const auto solved = solve_type_content_graph(std::move(masks), dependents);
    auto result = std::vector<std::pair<ConstructionTypeRef, TypeContents>>();
    result.reserve(reachable.size());
    for (const auto [index, type] : std::views::enumerate(reachable)) {
        result.emplace_back(type, solved[index]);
    }
    return result;
}
