module carven:semantic.semir.contents;

import :semantic.semir.decl;
import :semantic.semir.type;
import std;

struct TypeContents final {
    bool closure_owner;
    bool callable_view;
    bool storage_owner;
    // A value contains a native C++ value by value. Its Read ABI must retain
    // native copy/destruction behavior even inside Carven aggregates.
    bool contains_native_value;

    // Read preserves the identity of Carven-owned storage. Native value
    // containment is queried separately and does not imply Carven-owned storage.
    auto read_borrows_storage() const noexcept -> bool;
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
