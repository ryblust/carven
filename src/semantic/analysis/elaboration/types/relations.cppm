module carven:semantic.analysis.elaboration.types.relations;

import :semantic.hir;
import :semantic.hir.ids;
import std;

auto builtin_type_supports_equality(HIRBuiltinType type) noexcept -> bool;

auto type_compatible(const SemanticConstruction& hir, HIRTypeID left, HIRTypeID right) noexcept
    -> bool;

auto callable_adoption_compatible(
    const SemanticConstruction& hir,
    HIRTypeID target,
    HIRTypeID source
) noexcept -> bool;
