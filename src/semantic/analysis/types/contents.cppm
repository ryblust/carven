module carven:semantic.analysis.types.contents;

import :semantic.semir.decl;
import :semantic.semir.type;
import std;

struct TypeContents final {
    bool closure_owner;
    bool callable_view;
};

auto compute_type_contents(
    const CanonicalTypeStore& types,
    const DeclarationStore& declarations
) noexcept -> std::vector<TypeContents>;
