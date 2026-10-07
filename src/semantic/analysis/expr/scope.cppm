module carven:semantic.analysis.expr.scope;

import :semantic.semir.async;
import :semantic.semir.constant;
import :semantic.semir.type;
import std;

struct ResolvedEnumCase final {
    EnumCaseID id;
    EnumID owner;
    std::vector<ConstructionTypeRef> payload_types;
    std::optional<ConstantID> constant;
};

struct TimeoutContract final {
    TypeID duration;
    TypeID timeout_failure;
    TypeID range_failure;
};

using ResolvedStaticCallable = std::variant<
    FunctionID,
    GenericDeclarationID,
    AsyncIntrinsic,
    AsyncCompositionPolicy,
    TimeoutContract>;

enum class ExpressionMode { Body, StaticRoot };
