module carven:semantic.analysis.expr.scope;

import :semantic.analysis.program;
import :semantic.semir.constant;
import :semantic.semir.type;
import std;

struct ResolvedEnumCase final {
    EnumCaseID id;
    EnumID owner;
    ConstructionTypeRef reference_type;
    std::vector<ConstructionTypeRef> payload_types;
    std::optional<ConstantID> constant;
};

// A checked case reference denotes either its nullary value or its payload
// constructor. Both direct construction and stored constructors consume this type.
auto enum_case_reference_type(
    ProgramDraft& draft,
    TypeID owner,
    std::span<const ConstructionTypeRef> payload
) noexcept -> ConstructionTypeRef;

enum class ExpressionMode { Body, StaticRoot };
