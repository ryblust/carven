module carven:semantic.analysis.elaboration.types.relations;

import :semantic.analysis.session.read;
import :semantic.hir;
import :semantic.hir.ids;
import std;

auto builtin_type_supports_equality(HIRBuiltinType type) noexcept -> bool;

auto type_compatible(SemanticDraftView hir, HIRTypeID left, HIRTypeID right) noexcept -> bool;

auto callable_adoption_compatible(
    SemanticDraftView hir,
    HIRTypeID target,
    HIRTypeID source,
    std::span<const std::vector<HIRTypeID>> callable_failures
) noexcept -> bool;
