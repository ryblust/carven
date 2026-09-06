module carven:semantic.analysis.expr.scope;

import :semantic.semir.constant;
import :semantic.semir.type;
import std;

struct ResolvedConstantName final {
    ConstructionTypeRef type;
    std::optional<ConstantID> constant;
};

struct ResolvedEnumCase final {
    EnumCaseID id;
    EnumID owner;
    std::vector<ConstructionTypeRef> payload_types;
    std::optional<ConstantID> constant;
};
