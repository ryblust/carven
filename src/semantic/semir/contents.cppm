module carven:semantic.semir.contents;

import :semantic.semir.decl;
import :semantic.semir.type;
import std;

struct TypeContents final {
    bool closure_owner;
    bool callable_view;
    bool storage_owner;

    // Read preserves the identity of Carven-owned storage. Other representations
    // use the native copy/destruction policy without inventing owned contents.
    auto read_borrows_storage() const noexcept -> bool { return storage_owner || closure_owner; }
};

auto compute_type_contents(
    const CanonicalTypeStore& types,
    const DeclarationStore& declarations
) noexcept -> std::vector<TypeContents>;
