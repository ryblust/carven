module carven:semantic.semir.structured;

import :semantic.semir.body;
import :semantic.semir.ids;
import :semantic.semir.type;
import :support.invariant;
import :support.unique_indirect;
import std;

class BodyType final {
public:
    explicit BodyType(ConstructionTypeRef value) noexcept;
    explicit BodyType(TypeID value) noexcept;
    explicit BodyType(TypeTermID value) noexcept;
    auto construction() const noexcept -> const ConstructionTypeRef&;
    auto resolved() const noexcept -> TypeID;

private:
    ConstructionTypeRef value;
};

class BodyFailures final {
public:
    explicit BodyFailures(FailureTermID value) noexcept;
    explicit BodyFailures(FailureSetID value) noexcept;
    auto term() const noexcept -> FailureTermID;
    auto resolved() const noexcept -> FailureSetID;

private:
    std::variant<FailureTermID, FailureSetID> value;
};

// A single operation tree completes its type and failure facts in place.
struct SemFieldInitializer;
struct SemCallArgument;
struct SemCapture;
struct SemConditionalBranch;
struct SemMatchArm;
struct SemCatchArm;
struct SemanticExpression;
struct SemanticStatement;
struct SemanticRegion;

using OwnedSemanticExpression = UniqueIndirect<SemanticExpression>;
using OwnedSemanticRegion = UniqueIndirect<SemanticRegion>;

struct SemConstant final {
    ConstantID constant;
};

struct SemBinding final {
    LocalBindingID binding;
};

struct SemCallable final {
    CallableID callable;
};

struct SemEnumConstructor final {
    EnumCaseID enum_case;
};

struct SemArray final {
    std::vector<SemanticExpression> elements;
};

struct SemArrayAdopt final {
    OwnedSemanticExpression source;
};

struct SemStruct final {
    StructID structure;
    std::vector<SemFieldInitializer> fields;
};

struct SemEnumCase final {
    EnumCaseID enum_case;
    std::vector<SemanticExpression> payload;
};

struct SemUnary final {
    UnaryOperator operation;
    OwnedSemanticExpression operand;
};

struct SemBinary final {
    OwnedSemanticExpression left;
    BinaryOperator operation;
    OwnedSemanticExpression right;
};
enum class ShortCircuitOperator { And, Or };

struct SemShortCircuit final {
    OwnedSemanticExpression left;
    ShortCircuitOperator operation;
    OwnedSemanticExpression right;
};

struct SemCast final {
    OwnedSemanticExpression operand;
    CastKind kind;
};

struct SemDereference final {
    OwnedSemanticExpression source;
    ProgramOriginID origin;
};

struct SemField final {
    OwnedSemanticExpression source;
    FieldProjection field;
};

struct SemIndex final {
    OwnedSemanticExpression source;
    OwnedSemanticExpression index;
    IndexBoundsPolicy bounds;
};

enum class PrintKind { Print, Println, Eprint, Eprintln };

struct SemTestReport final {
    TestReportKind kind;
    std::optional<OwnedSemanticExpression> condition;
    std::optional<OwnedSemanticExpression> message;
    std::optional<ProgramSpellingID> condition_source;
};

struct SemPrint final {
    PrintKind kind;
    std::vector<SemCallArgument> operands;
};

struct SemFormat final {
    ConstantID format_string_id;
    std::vector<SemCallArgument> operands;
};

struct SemSliceIntrinsic final {
    SliceIntrinsic intrinsic;
    std::vector<SemCallArgument> operands;
};

struct SemTextIntrinsic final {
    TextIntrinsic intrinsic;
    std::vector<SemCallArgument> operands;
};

struct SemCpp final {
    CppOperation operation;
    std::vector<SemCallArgument> operands;
};

struct SemCppOperand final {
    AccessMode access;
    OwnedSemanticExpression expression;
};

struct SemCppCall final {
    CppCallee<SemCppOperand> callee;
    std::vector<SemCallArgument> arguments;
};

struct SemCall final {
    OwnedSemanticExpression callee;
    std::vector<SemCallArgument> arguments;
    BodyFailures callee_failures;
};

struct SemClosure final {
    CallableID callable;
    std::vector<SemCapture> captures;
};

struct SemBorrowCallable final {
    OwnedSemanticExpression source;
};

struct SemTake final {
    OwnedSemanticExpression place;
};

struct SemPropagate final {
    OwnedSemanticExpression operand;
};

struct SemIf final {
    std::vector<SemConditionalBranch> branches;
    std::optional<OwnedSemanticRegion> otherwise;
};

struct SemMatch final {
    OwnedSemanticExpression subject;
    // A place subject is located once and read again after a rejected guard.
    bool subject_is_place;
    std::vector<SemMatchArm> arms;
};

struct SemTypedCatchPattern final {
    BodyType type;
    PatternID inner;
};

struct SemCatchAlternative final {
    ProgramOriginID origin;
    std::variant<CatchAllPattern, SemTypedCatchPattern> pattern;
    bool reachable;
};

struct SemTry final {
    OwnedSemanticRegion body;
    BodyFailures protected_failures;
    BodyFailures residual_failures;
    std::vector<SemCatchArm> arms;
};

enum class SemanticValueCategory { Value, Place };

struct SemanticExpression final {
    BodyType type;
    LifetimeRegionID lifetime;
    ProgramOriginID origin;
    // Known value on normal completion; execution and const admission are separate.
    std::optional<ConstantID> constant;
    BodyFailures failures;
    bool exits_test;
    SemanticValueCategory category;
    std::variant<
        SemConstant,
        SemBinding,
        SemCallable,
        SemEnumConstructor,
        SemCpp,
        SemCppCall,
        SemArray,
        SemArrayAdopt,
        SemStruct,
        SemEnumCase,
        SemUnary,
        SemBinary,
        SemShortCircuit,
        SemCast,
        SemField,
        SemDereference,
        SemIndex,
        SemTextIntrinsic,
        SemSliceIntrinsic,
        SemPrint,
        SemTestReport,
        SemFormat,
        SemCall,
        SemClosure,
        SemBorrowCallable,
        SemTake,
        SemPropagate,
        SemIf,
        SemMatch,
        SemTry>
        value;

    // A Read can retain this expression's selected object. Consuming a value
    // still creates independent destination storage through the normal use rules.
    auto selects_storage() const noexcept -> bool;
};

struct SemanticRegion final {
    LifetimeRegionID lifetime;
    ProgramOriginID origin;
    std::vector<SemanticStatement> statements;
    std::optional<SemanticExpression> result;
    BodyFailures failures;
    bool exits_test;
};

struct SemFieldInitializer final {
    std::uint32_t declaration_index;
    SemanticExpression value;
};

struct SemCallArgument final {
    AccessMode access;
    SemanticExpression expression;
};

struct SemCapture final {
    CaptureMode mode;
    SemanticExpression expression;
};

struct SemConditionalBranch final {
    SemanticExpression condition;
    SemanticRegion body;
};

struct SemMatchArm final {
    PatternID pattern;
    std::vector<LocalBindingID> bindings;
    std::optional<SemanticExpression> guard;
    SemanticRegion body;
    bool reachable;
};

struct SemCatchArm final {
    ProgramOriginID origin;
    BodyFailures accepted_failures;
    std::vector<SemCatchAlternative> alternatives;
    std::vector<LocalBindingID> bindings;
    std::optional<SemanticExpression> guard;
    SemanticRegion body;
};

struct SemReturn final {
    std::optional<SemanticExpression> value;
};

struct SemBreak final {};

struct SemContinue final {};

struct SemRethrow final {};

struct SemThrow final {
    SemanticExpression value;
    TypeID failure_type;
};

struct SemExpressionStatement final {
    SemanticExpression expression;
};

struct SemInitialize final {
    LocalBindingID binding;
    SemanticExpression initializer;
};

struct SemAssign final {
    SemanticExpression target;
    std::optional<BinaryOperator> compound;
    SemanticExpression value;
};

struct SemLoop final {
    OwnedSemanticRegion initializer;
    std::optional<SemanticExpression> condition;
    OwnedSemanticRegion body;
    OwnedSemanticRegion steps;
};

struct SemIntegerRange final {
    SemanticExpression begin;
    SemanticExpression end;
};

struct SemSequenceRange final {
    SemanticExpression value;
};

using SemRangeSource = std::variant<SemIntegerRange, SemSequenceRange>;

struct SemRangeLoop final {
    LifetimeRegionID lifetime;
    AccessMode access;
    std::optional<LocalBindingID> binding;
    SemRangeSource source;
    OwnedSemanticRegion body;
};

struct SemanticStatement final {
    ProgramOriginID origin;
    LifetimeRegionID lifetime;
    std::variant<
        SemReturn,
        SemBreak,
        SemContinue,
        SemRethrow,
        SemThrow,
        SemExpressionStatement,
        SemInitialize,
        SemAssign,
        SemLoop,
        SemRangeLoop,
        OwnedSemanticRegion>
        value;
};

template<typename Visitor>
auto visit_cpp_operands(const SemCpp& operation, Visitor visit) noexcept -> void {
    for (const auto& operand : operation.operands) {
        visit(operand.access, operand.expression);
    }
}

template<typename Visitor>
auto visit_cpp_operands(const SemCppCall& call, Visitor visit) noexcept -> void {
    visit_cpp_callee_operand(call.callee, [&](const SemCppOperand& operand) noexcept {
        visit(operand.access, *operand.expression);
    });
    for (const auto& argument : call.arguments) {
        visit(argument.access, argument.expression);
    }
}

template<typename TypeReader>
auto cpp_call_query(const SemCppCall& call, TypeReader type) noexcept -> CppQueryType {
    const auto operand_type = [&](const SemCppOperand& operand) noexcept {
        return CppTypeOperand {
            .type = type(operand.expression->type.resolved()),
            .access = operand.access
        };
    };
    auto callee = std::visit(
        [&](const auto& value) noexcept -> CppCallee<CppTypeOperand> {
            using Value = std::remove_cvref_t<decltype(value)>;
            if constexpr (std::same_as<Value, CppNameReference>) {
                return value;
            } else if constexpr (std::same_as<Value, CppMemberCallee<SemCppOperand>>) {
                return CppMemberCallee<CppTypeOperand> {
                    .receiver = operand_type(value.receiver),
                    .member = value.member
                };
            } else {
                return operand_type(value);
            }
        },
        call.callee
    );
    auto arguments = std::vector<CppTypeOperand>();
    for (const auto& argument : call.arguments) {
        arguments.push_back(
            {.type = type(argument.expression.type.resolved()), .access = argument.access}
        );
    }
    return {
        .expression = CppCallQuery {.callee = std::move(callee), .arguments = std::move(arguments)}
    };
}

using SemanticExpressionValue = decltype(SemanticExpression::value);

struct SemIRBodyData final {
    BodyID id;
    BodyKind kind;
    ProvenanceIdentity provenance_identity;
    BodyInputs inputs;
    LifetimeRegionTree lifetime_regions;
    ImmutableBodyTable<LocalBinding, LocalBindingID> bindings;
    ImmutableBodyTable<Pattern, PatternID> patterns;
    SemanticRegion region;
};

class SemIRBody final {
public:
    explicit SemIRBody(SemIRBodyData data) noexcept;
    auto id() const noexcept -> BodyID;
    auto kind() const noexcept -> BodyKind;
    auto identity() const noexcept -> BodyIdentity;
    auto provenance_identity() const noexcept -> ProvenanceIdentity;
    auto inputs() const noexcept -> const BodyInputs&;
    auto lifetime_regions() const noexcept -> const LifetimeRegionTree&;
    auto bindings() const noexcept -> IDTableEntries<LocalBindingID, LocalBinding, BodyIdentity>;
    auto patterns() const noexcept -> IDTableEntries<PatternID, Pattern, BodyIdentity>;
    auto pattern_table() const noexcept -> const ImmutableBodyTable<Pattern, PatternID>&;
    auto binding(LocalBindingID id) const noexcept -> const LocalBinding&;
    auto pattern(PatternID id) const noexcept -> const Pattern&;
    auto region() const noexcept -> const SemanticRegion&;

private:
    SemIRBodyData data;
};

struct StructuredBodyDraft final {
    BodyID id;
    BodyKind kind;
    ProvenanceIdentity provenance_identity;
    BodyInputs inputs;
    LifetimeRegionTree lifetime_regions;
    ImmutableBodyTable<ElaboratedLocalBinding, LocalBindingID> bindings;
    ImmutableBodyTable<ElaboratedPattern, PatternID> patterns;
    SemanticRegion region;
};
