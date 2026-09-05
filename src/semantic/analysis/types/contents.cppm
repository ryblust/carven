module carven:semantic.analysis.types.contents;

import :semantic.semir.program;
import std;

struct TypeContents final {
    bool closure_owner;
    bool callable_view;
};

// This query is scoped to analysis after construction types have been solved.
class TypeContentsQuery final {
public:
    explicit TypeContentsQuery(ProgramDraft& program) noexcept;
    auto contents(TypeID type) noexcept -> TypeContents;
    auto contains_owner(std::variant<TypeID, FailureSetID> type) noexcept -> bool;
    auto contains_view(std::variant<TypeID, FailureSetID> type) noexcept -> bool;
    auto contains_view(TypeID type) noexcept -> bool;

private:
    ProgramDraft& draft;
    std::flat_map<TypeID, TypeContents> memo;
    std::flat_set<TypeID> visiting;
};
