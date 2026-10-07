module carven:semantic.semir.contents;

import :semantic.semir.decl;
import :semantic.semir.type;
import std;

// Containment facts include the type itself and propagate through
// struct fields, array elements, and enum payloads; pointers propagate none.
struct TypeContents final {
    bool contains_closure_owner;
    // Also propagates through slice element types and native C++ template arguments.
    bool contains_callable_view;
    // Owned Array, Sequence or String storage.
    bool contains_storage_owner;
    // Borrowed Str, StrCharsView, or Slice held by value; empty arrays contain none.
    bool contains_storage_view;
    // Native C++ values held by value, including inside Carven aggregates.
    bool contains_native_value;
    // Possible String storage held by value; zero-length arrays stop propagation.
    // Implies contains_storage_owner.
    bool contains_string_storage;

    // Read preserves the identity of owned Array, Sequence, String, or closure storage.
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
