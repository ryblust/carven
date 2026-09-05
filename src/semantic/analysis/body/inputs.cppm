module carven:semantic.analysis.body.inputs;

import :semantic.semir.body;
import :semantic.semir.ids;
import :semantic.semir.structured;
import std;

struct ExpressionHandleTag final {};
struct PlaceHandleTag final {};
using ExpressionHandle = BodyLocalID<ExpressionHandleTag>;
using PlaceHandle = BodyLocalID<PlaceHandleTag>;

// Short-lived inputs consumed by BodyBuilder into owned draft children.
namespace expression_construction {

struct IndexProjection final {
    ExpressionHandle index;
    ArrayBoundsPolicy bounds;
};
using Projection = std::variant<FieldProjection, IndexProjection>;

struct Array final {
    std::vector<ExpressionHandle> elements;
};
struct StructField final {
    std::uint32_t declaration_index;
    ExpressionHandle value;
};
struct Struct final {
    StructID structure;
    std::vector<StructField> fields;
};
struct EnumCase final {
    EnumCaseID enum_case;
    std::vector<ExpressionHandle> payload;
};
struct Unary final {
    UnaryOperator operation;
    ExpressionHandle operand;
};
struct Binary final {
    ExpressionHandle left;
    BinaryOperator operation;
    ExpressionHandle right;
};
struct Cast final {
    ExpressionHandle operand;
    CastKind kind;
};
struct Project final {
    ExpressionHandle source;
    Projection projection;
};
struct TextOperation final {
    ExpressionHandle source;
    TextIntrinsic intrinsic;
};
struct ValueCapture final {
    ExpressionHandle value;
};
struct WriteCapture final {
    PlaceHandle place;
};
using Capture = std::variant<ValueCapture, WriteCapture>;
struct Closure final {
    CallableID callable;
    std::vector<Capture> captures;
};
using Callee = std::variant<CallableID, ExpressionHandle>;
struct ReadArgument final {
    ExpressionHandle value;
};
struct WriteArgument final {
    PlaceHandle place;
};
struct TakeArgument final {
    ExpressionHandle value;
};
using Argument = std::variant<ReadArgument, WriteArgument, TakeArgument>;
using CallableSource = std::variant<CallableID, ExpressionHandle, PlaceHandle>;
struct Read final {
    PlaceHandle place;
};
struct Take final {
    PlaceHandle place;
};
using Input = std::variant<
    SemLiteral,
    SemConstant,
    SemEnumConstructor,
    Array,
    Struct,
    EnumCase,
    Unary,
    Binary,
    Cast,
    Project,
    TextOperation,
    Closure,
    Read,
    Take>;

}
