module carven:semantic.analysis.expr.scope;

import :semantic.semir.constant;
import :semantic.semir.type;
import std;

struct ResolvedEnumCase final {
    EnumCaseID id;
    EnumID owner;
    std::vector<ConstructionTypeRef> payload_types;
    std::optional<ConstantID> constant;
};

using ResolvedStaticCallable = std::variant<FunctionID, AsyncIntrinsicDeclID>;

enum class ExpressionMode { Body, StaticRoot };
