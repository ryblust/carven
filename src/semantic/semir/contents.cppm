module carven:semantic.semir.contents;

import :semantic.semir.decl;
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
inline constexpr auto all_type_contents = 255u;

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
    // Native C++ values held by value, including inside Carven aggregates.
    bool contains_native_value;
    // Possible String storage held by value; zero-length arrays stop propagation.
    // Implies contains_storage_owner.
    bool contains_string_storage;

    // Rigid definition value with relationships described by its input role.
    bool contains_parametric_value;

    auto mask() const noexcept -> unsigned;

    // Read preserves the identity of owned storage, closures, and opaque native values.
    auto read_borrows_storage() const noexcept -> bool;
    // Read is a value snapshot with no owned Carven storage or native value.
    auto read_is_value_snapshot() const noexcept -> bool;
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

// Dependencies carry only the contents relevant to their enclosing shape.
// The same finite worklist handles concrete and rigid recursive value shapes.
auto solve_type_content_graph(
    std::vector<unsigned> contents,
    std::span<const std::vector<TypeContentDependent>> dependents
) noexcept -> std::vector<TypeContents>;
