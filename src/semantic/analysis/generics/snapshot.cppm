module carven:semantic.analysis.generics.snapshot;

import :semantic.analysis.failure;
import :semantic.semir.generic;
import :semantic.semir.structured;
import std;

// Definition checking and ownership share the same rigid tree. This data is
// consumed before publication; it is not an executable program type store.
struct GenericAnalysisSnapshot final {
    std::vector<GenericDeclarationDefinition> definitions;
    std::vector<GenericTypeExpression> types;
    std::map<GenericDeclarationID, std::unique_ptr<StructuredBodyDraft>> bodies;
    std::map<GenericTypeID, std::vector<ConstructionTypeRef>> records;
    std::map<GenericTypeID, std::vector<std::vector<ConstructionTypeRef>>> enum_payloads;
    std::map<GenericDeclarationID, bool> test_stops;
    std::shared_ptr<const FailureSolution> failures;

    auto generic_type(GenericTypeID type) const noexcept -> const GenericTypeExpression&;
    auto definition(GenericDeclarationID id) const noexcept -> const GenericDeclarationDefinition&;
};
