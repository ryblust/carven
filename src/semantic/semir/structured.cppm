module carven:semantic.semir.structured;

import :semantic.semir.body;
import :semantic.semir.format;
import :semantic.semir.ids;
import :semantic.semir.simd;
import :semantic.semir.type;
import :support.invariant;
import :support.tree_value;
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

struct SemDefault final {};

struct SemConstant final {
    ConstantID constant;
};

// Executable projection of an edge proved to have no entry in this stage.
struct SemUnreachable final {};

struct SemBinding final {
    LocalBindingID binding;
};

struct SemCallable final {
    CallableID callable;
};

struct SemEnumConstructor final {
    EnumCaseID enum_case;
};

struct SemRange final {
    OwnedSemanticExpression begin;
    OwnedSemanticExpression end;
    bool inclusive;
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

struct SemAddressOf final {
    OwnedSemanticExpression source;
};

struct SemField final {
    auto consumes_source() const noexcept -> bool;

    OwnedSemanticExpression source;
    FieldProjection field;
};

struct SemIndex final {
    OwnedSemanticExpression source;
    OwnedSemanticExpression index;
    IndexBoundsPolicy bounds;
};

enum class PrintKind { Print, Println, Eprint, Eprintln };

struct SemReport final {
    ReportKind kind;
    std::optional<OwnedSemanticExpression> condition;
    std::optional<OwnedSemanticExpression> message;
    std::optional<ProgramSpellingID> condition_source;
    std::optional<std::array<ProgramSpellingID, 2>> operand_sources;
};

struct SemPrint final {
    PrintKind kind;
    std::vector<SemCallArgument> operands;
};

struct SemFormat final {
    FormatSpec specification;
    // Complete source order, independently of any later implementation selection.
    std::vector<SemCallArgument> operands;
    std::optional<OwnedSemanticExpression> receiver;
};

struct SliceIntrinsicOperation final {
    SliceIntrinsic intrinsic;
    // Length on normal completion does not prove that checked slicing succeeds.
    std::optional<std::uint64_t> result_extent;
};

using IntrinsicOperation = std::variant<SliceIntrinsicOperation, TextIntrinsic, SIMDIntrinsic>;

struct SemIntrinsic final {
    IntrinsicOperation operation;
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
    // A stable function target does not remove evaluation or availability checks of callee.
    std::optional<CallableID> target;
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
    // A const if selects one arm per static instance; every arm is still checked.
    bool is_static;
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

struct SemanticExpressionCleanup;

using SemanticExpressionValue = TreeValue<
    SemanticExpressionCleanup,
    SemDefault,
    SemConstant,
    SemUnreachable,
    SemBinding,
    SemCallable,
    SemEnumConstructor,
    SemCpp,
    SemCppCall,
    SemArray,
    SemRange,
    SemArrayAdopt,
    SemStruct,
    SemEnumCase,
    SemUnary,
    SemBinary,
    SemShortCircuit,
    SemCast,
    SemField,
    SemDereference,
    SemAddressOf,
    SemIndex,
    SemIntrinsic,
    SemPrint,
    SemReport,
    SemFormat,
    SemCall,
    SemClosure,
    SemBorrowCallable,
    SemTake,
    SemPropagate,
    SemIf,
    SemMatch,
    SemTry>;

struct SemanticExpressionCleanup final {
    static constexpr auto copyable = true;
    static auto copy(const SemanticExpressionValue& value) noexcept -> SemanticExpressionValue;
    static auto clear(SemanticExpressionValue& value) noexcept -> void;
};

struct SemanticExpression final {
    BodyType type;
    LifetimeRegionID lifetime;
    ProgramOriginID origin;
    // Known value on normal completion; execution and const admission are separate.
    std::optional<ConstantID> constant;
    BodyFailures failures;
    // Body completion includes callee effects before publication.
    bool exits_test;
    // Whether evaluation reaches this operation after its required operands.
    bool operation_reachable;
    SemanticValueCategory category;
    SemanticExpressionValue value;

    // A Read can retain this expression's selected object. Consuming a value
    // still creates independent destination storage through the normal use rules.
    auto selects_storage() const noexcept -> bool;
};

struct SemanticRegion final {
    LifetimeRegionID lifetime;
    ProgramOriginID origin;
    std::vector<SemanticStatement> statements;
    std::optional<SemanticExpression> result;
    // Whether control can enter the result expression after preceding statements.
    bool result_reachable;
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

struct SemPatternBounds final {
    PatternID pattern;
    std::optional<SemanticExpression> begin;
    std::optional<SemanticExpression> end;
};

struct SemMatchArm final {
    PatternID pattern;
    std::vector<LocalBindingID> bindings;
    std::optional<SemanticExpression> guard;
    SemanticRegion body;
    bool reachable;
    // Normal pattern rejection remains possible after preceding unguarded
    // arms. Required evaluation and the arm's guard remain independent.
    bool pattern_may_reject;
    std::vector<SemPatternBounds> pattern_bounds;
};

struct SemCatchArm final {
    ProgramOriginID origin;
    BodyFailures accepted_failures;
    std::vector<SemCatchAlternative> alternatives;
    std::vector<LocalBindingID> bindings;
    std::optional<SemanticExpression> guard;
    SemanticRegion body;
    std::vector<SemPatternBounds> pattern_bounds;
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

// A local static root in the checked source body. The binding has the
// published frozen type; the initializer retains its execution result type.
// Specialization consumes this operation before residual publication.
struct SemStaticBinding final {
    LocalBindingID binding;
    OwnedSemanticExpression initializer;
};

// A block of the body's static stage. Specialization rewrites and executes it
// once per occurrence, then removes it from the residual region.
struct SemConstBlock final {
    std::optional<ProgramSpellingID> label;
    // The label, or the keyword of an unlabeled block.
    ProgramOriginID source;
    OwnedSemanticRegion region;
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

struct SemRangeLoop final {
    LifetimeRegionID lifetime;
    AccessMode access;
    std::optional<LocalBindingID> binding;
    SemanticExpression source;
    OwnedSemanticRegion body;
    // A const for expands a static integer range; its index is a static binding.
    bool is_static;
};

// A const for in a realized body: one specialized region per element. Continue
// leaves the current iteration and break leaves the expansion.
struct SemExpandedLoop final {
    std::vector<SemanticRegion> iterations;
};

struct SemanticStatementCleanup;
using SemanticStatementValue = TreeValue<
    SemanticStatementCleanup,
    SemReturn,
    SemBreak,
    SemContinue,
    SemRethrow,
    SemThrow,
    SemExpressionStatement,
    SemInitialize,
    SemStaticBinding,
    SemConstBlock,
    SemAssign,
    SemLoop,
    SemRangeLoop,
    SemExpandedLoop,
    OwnedSemanticRegion>;

struct SemanticStatementCleanup final {
    static constexpr auto copyable = true;
    static auto copy(const SemanticStatementValue& value) noexcept -> SemanticStatementValue;
    static auto clear(SemanticStatementValue& value) noexcept -> void;
};

struct SemanticStatement final {
    ProgramOriginID origin;
    LifetimeRegionID lifetime;
    // Whether control can enter this statement in its containing region.
    bool reachable;
    SemanticStatementValue value;
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
auto cpp_construct_query(
    TypeID target,
    std::span<const SemCallArgument> operands,
    TypeReader read_type
) noexcept -> CppQueryType {
    auto arguments = std::vector<CppConstructArgument>();
    for (const auto& argument : operands) {
        const auto& expression = argument.expression;
        const auto type = read_type(expression.type.resolved());
        const auto* builtin = std::get_if<BuiltinTypeValue>(&type.value);
        const auto scalar = builtin != nullptr
            && (builtin_is_numeric(builtin->kind)
                || builtin->kind == BuiltinType::Bool
                || builtin->kind == BuiltinType::Char);
        arguments.push_back(
            {.operand = {.type = expression.type.resolved(), .access = argument.access},
             .constant =
                 argument.access == AccessMode::Read && scalar && !expression.selects_storage()
                 ? expression.constant
                 : std::nullopt}
        );
    }
    return {.expression = CppConstructQuery {.target = target, .arguments = std::move(arguments)}};
}

template<typename TypeReader>
auto cpp_call_query(const SemCppCall& call, TypeReader type) noexcept -> CppQueryType {
    const auto operand_type = [&](const SemCppOperand& operand) noexcept {
        return CppTypeOperand {
            .type = type(operand.expression->type.resolved()),
            .access = operand.access
        };
    };
    auto callee = call.callee.visit([&](const auto& value) noexcept -> CppCallee<CppTypeOperand> {
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
    });
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

struct SemIRBodyData final {
    BodyID id;
    BodyKind kind;
    ProvenanceIdentity provenance_identity;
    BodyInputs inputs;
    LifetimeRegionTree lifetime_regions;
    ImmutableBodyTable<LocalBinding, LocalBindingID> bindings;
    ImmutableBodyTable<Pattern, PatternID> patterns;
    SemanticRegion region;
    // The region with the static stage applied, when that differs from `region`.
    std::optional<SemanticRegion> residual;
    // Analysis-only source-body provenance. An instance keeps the copied local
    // identities; publication clears this link along with source bodies.
    std::optional<BodyID> specialized;
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
    // During analysis this is the checked source region. Final publication
    // replaces it with the executable region and discards source alternatives.
    auto region() const noexcept -> const SemanticRegion&;
    // Analysis selects the executable region before publication discards the
    // checked source. Published consumers use region().
    auto realized_region() const noexcept -> const SemanticRegion&;
    auto specialized() const noexcept -> std::optional<BodyID>;

private:
    auto publish() noexcept -> void;
    SemIRBodyData data;

    friend class SemIRProgram;
};

// Construction transfers one region with the local tables that own its IDs.
struct StructuredRegionDraft final {
    LifetimeRegionTree lifetime_regions;
    ImmutableBodyTable<ElaboratedLocalBinding, LocalBindingID> bindings;
    ImmutableBodyTable<ElaboratedPattern, PatternID> patterns;
    SemanticRegion region;
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
    std::optional<SemanticRegion> residual;
    std::optional<BodyID> specialized;
};
