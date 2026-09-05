module carven:semantic.semir.structured;

import :semantic.semir.body;
import :semantic.semir.ids;
import :semantic.semir.type;
import :support.unique_indirect;
import std;

// The two instantiations distinguish unfinished type/failure constraints from
// published semantic facts. Neither instantiation has expression or block IDs.
template<typename Type, typename Failures>
struct SemanticExpression;
template<typename Type, typename Failures>
struct SemanticStatement;
template<typename Type, typename Failures>
struct SemanticRegion;

template<typename Type, typename Failures>
using OwnedSemanticExpression = UniqueIndirect<SemanticExpression<Type, Failures>>;
template<typename Type, typename Failures>
using OwnedSemanticRegion = UniqueIndirect<SemanticRegion<Type, Failures>>;

struct SemLiteral final {
    LiteralValue value;
};
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

template<typename Type, typename Failures>
struct SemSequence final {
    std::vector<SemanticExpression<Type, Failures>> expressions;
};
template<typename Type, typename Failures>
struct SemArray final {
    std::vector<SemanticExpression<Type, Failures>> elements;
};
template<typename Type, typename Failures>
struct SemArrayAdopt final {
    OwnedSemanticExpression<Type, Failures> source;
};
template<typename Type, typename Failures>
struct SemFieldInitializer final {
    std::uint32_t declaration_index;
    SemanticExpression<Type, Failures> value;
};
template<typename Type, typename Failures>
struct SemStruct final {
    StructID structure;
    std::vector<SemFieldInitializer<Type, Failures>> fields;
};
template<typename Type, typename Failures>
struct SemEnumCase final {
    EnumCaseID enum_case;
    std::vector<SemanticExpression<Type, Failures>> payload;
};
template<typename Type, typename Failures>
struct SemUnary final {
    UnaryOperator operation;
    OwnedSemanticExpression<Type, Failures> operand;
};
template<typename Type, typename Failures>
struct SemBinary final {
    OwnedSemanticExpression<Type, Failures> left;
    BinaryOperator operation;
    OwnedSemanticExpression<Type, Failures> right;
};
enum class ShortCircuitOperator { And, Or };
template<typename Type, typename Failures>
struct SemShortCircuit final {
    OwnedSemanticExpression<Type, Failures> left;
    ShortCircuitOperator operation;
    OwnedSemanticExpression<Type, Failures> right;
};
template<typename Type, typename Failures>
struct SemCast final {
    OwnedSemanticExpression<Type, Failures> operand;
    CastKind kind;
};
template<typename Type, typename Failures>
struct SemField final {
    OwnedSemanticExpression<Type, Failures> source;
    FieldProjection field;
};
template<typename Type, typename Failures>
struct SemIndex final {
    OwnedSemanticExpression<Type, Failures> source;
    OwnedSemanticExpression<Type, Failures> index;
    ArrayBoundsPolicy bounds;
};
template<typename Type, typename Failures>
struct SemTextIntrinsic final {
    OwnedSemanticExpression<Type, Failures> source;
    TextIntrinsic intrinsic;
};
template<typename Type, typename Failures>
struct SemCallArgument final {
    AccessMode access;
    SemanticExpression<Type, Failures> expression;
};
template<typename Type, typename Failures>
struct SemCall final {
    OwnedSemanticExpression<Type, Failures> callee;
    std::vector<SemCallArgument<Type, Failures>> arguments;
    Failures callee_failures;
};
template<typename Type, typename Failures>
struct SemCapture final {
    CaptureMode mode;
    SemanticExpression<Type, Failures> expression;
};
template<typename Type, typename Failures>
struct SemClosure final {
    CallableID callable;
    std::vector<SemCapture<Type, Failures>> captures;
};
template<typename Type, typename Failures>
struct SemBorrowCallable final {
    OwnedSemanticExpression<Type, Failures> source;
    LifetimeRegionID loan_lifetime;
};
template<typename Type, typename Failures>
struct SemTake final {
    OwnedSemanticExpression<Type, Failures> place;
};
template<typename Type, typename Failures>
struct SemPropagate final {
    OwnedSemanticExpression<Type, Failures> operand;
};

template<typename Type, typename Failures>
struct SemConditionalBranch final {
    SemanticExpression<Type, Failures> condition;
    SemanticRegion<Type, Failures> body;
};
template<typename Type, typename Failures>
struct SemIf final {
    std::vector<SemConditionalBranch<Type, Failures>> branches;
    std::optional<OwnedSemanticRegion<Type, Failures>> otherwise;
};
template<typename Type, typename Failures>
struct SemMatchArm final {
    PatternID pattern;
    std::vector<LocalBindingID> bindings;
    std::optional<SemanticExpression<Type, Failures>> guard;
    SemanticRegion<Type, Failures> body;
    bool reachable;
};
template<typename Type, typename Failures>
struct SemMatch final {
    OwnedSemanticExpression<Type, Failures> subject;
    // A place subject is located once and read again after a rejected guard.
    bool subject_is_place;
    std::vector<SemMatchArm<Type, Failures>> arms;
};
template<typename Type>
struct SemTypedCatchPattern final {
    Type type;
    PatternID inner;
};
template<typename Type>
struct SemCatchAlternative final {
    ProgramOriginID origin;
    std::variant<CatchAllPattern, SemTypedCatchPattern<Type>> pattern;
    bool reachable;
};
template<typename Type, typename Failures>
struct SemCatchArm final {
    ProgramOriginID origin;
    Failures accepted_failures;
    std::vector<SemCatchAlternative<Type>> alternatives;
    std::vector<LocalBindingID> bindings;
    std::optional<SemanticExpression<Type, Failures>> guard;
    SemanticRegion<Type, Failures> body;
};
template<typename Type, typename Failures>
struct SemTry final {
    OwnedSemanticRegion<Type, Failures> body;
    Failures protected_failures;
    Failures residual_failures;
    std::vector<SemCatchArm<Type, Failures>> arms;
};

enum class SemanticValueCategory { Value, Place };
template<typename Type, typename Failures>
struct SemanticExpression final {
    Type type;
    LifetimeRegionID lifetime;
    ProgramOriginID origin;
    std::optional<ConstantID> constant;
    Failures failures;
    bool exits_test;
    SemanticValueCategory category;
    std::variant<
        SemLiteral,
        SemConstant,
        SemBinding,
        SemCallable,
        SemEnumConstructor,
        SemSequence<Type, Failures>,
        SemArray<Type, Failures>,
        SemArrayAdopt<Type, Failures>,
        SemStruct<Type, Failures>,
        SemEnumCase<Type, Failures>,
        SemUnary<Type, Failures>,
        SemBinary<Type, Failures>,
        SemShortCircuit<Type, Failures>,
        SemCast<Type, Failures>,
        SemField<Type, Failures>,
        SemIndex<Type, Failures>,
        SemTextIntrinsic<Type, Failures>,
        SemCall<Type, Failures>,
        SemClosure<Type, Failures>,
        SemBorrowCallable<Type, Failures>,
        SemTake<Type, Failures>,
        SemPropagate<Type, Failures>,
        SemIf<Type, Failures>,
        SemMatch<Type, Failures>,
        SemTry<Type, Failures>>
        value;
};

template<typename Type, typename Failures>
struct SemReturn final {
    std::optional<SemanticExpression<Type, Failures>> value;
};
struct SemBreak final {};
struct SemContinue final {};
struct SemRethrow final {};
template<typename Type, typename Failures>
struct SemThrow final {
    SemanticExpression<Type, Failures> value;
    TypeID failure_type;
};
template<typename Type, typename Failures>
struct SemExpressionStatement final {
    SemanticExpression<Type, Failures> expression;
};
template<typename Type, typename Failures>
struct SemInitialize final {
    LocalBindingID binding;
    SemanticExpression<Type, Failures> initializer;
};
template<typename Type, typename Failures>
struct SemAssign final {
    SemanticExpression<Type, Failures> target;
    std::optional<BinaryOperator> compound;
    SemanticExpression<Type, Failures> value;
};
template<typename Type, typename Failures>
struct SemLoop final {
    OwnedSemanticRegion<Type, Failures> initializer;
    std::optional<SemanticExpression<Type, Failures>> condition;
    OwnedSemanticRegion<Type, Failures> body;
    OwnedSemanticRegion<Type, Failures> steps;
};
template<typename Type, typename Failures>
struct SemRangeLoop final {
    ScopeID scope;
    LifetimeRegionID lifetime;
    AccessMode access;
    std::optional<LocalBindingID> binding;
    SemanticExpression<Type, Failures> begin;
    // Present for an integer half-open range; absent for array/text iteration.
    std::optional<SemanticExpression<Type, Failures>> end;
    OwnedSemanticRegion<Type, Failures> body;
};
template<typename Type, typename Failures>
struct SemTestReport final {
    TestReportKind kind;
    std::optional<SemanticExpression<Type, Failures>> condition;
    std::optional<SemanticExpression<Type, Failures>> message;
    std::optional<ProgramSpellingID> condition_source;
};
template<typename Type, typename Failures>
struct SemanticStatement final {
    ProgramOriginID origin;
    LifetimeRegionID lifetime;
    std::variant<
        SemReturn<Type, Failures>,
        SemBreak,
        SemContinue,
        SemRethrow,
        SemThrow<Type, Failures>,
        SemExpressionStatement<Type, Failures>,
        SemInitialize<Type, Failures>,
        SemAssign<Type, Failures>,
        SemLoop<Type, Failures>,
        SemRangeLoop<Type, Failures>,
        SemTestReport<Type, Failures>,
        OwnedSemanticRegion<Type, Failures>>
        value;
};
template<typename Type, typename Failures>
struct SemanticRegion final {
    ScopeID scope;
    LifetimeRegionID lifetime;
    ProgramOriginID origin;
    std::vector<SemanticStatement<Type, Failures>> statements;
    std::optional<SemanticExpression<Type, Failures>> result;
    Failures failures;
    bool exits_test;
};

using SemIRExpression = SemanticExpression<TypeID, FailureSetID>;
using SemIRStatement = SemanticStatement<TypeID, FailureSetID>;
using SemIRRegion = SemanticRegion<TypeID, FailureSetID>;
using DraftExpression = SemanticExpression<ConstructionTypeRef, FailureTermID>;
using DraftExpressionValue = decltype(DraftExpression::value);
using DraftStatement = SemanticStatement<ConstructionTypeRef, FailureTermID>;
using DraftRegion = SemanticRegion<ConstructionTypeRef, FailureTermID>;

struct SemIRBodyData final {
    BodyID id;
    BodyKind kind;
    ProvenanceIdentity provenance_identity;
    BodyInputs inputs;
    ScopeTree scopes;
    LifetimeRegionTree lifetime_regions;
    ImmutableBodyTable<LocalBinding, LocalBindingID> bindings;
    ImmutableBodyTable<Pattern, PatternID> patterns;
    SemIRRegion region;
};

class SemIRBody final {
public:
    explicit SemIRBody(SemIRBodyData data) noexcept
        : data(std::move(data)) {}
    auto id() const noexcept -> BodyID { return data.id; }
    auto kind() const noexcept -> BodyKind { return data.kind; }
    auto identity() const noexcept -> BodyIdentity { return data.scopes.owner(); }
    auto provenance_identity() const noexcept -> ProvenanceIdentity {
        return data.provenance_identity;
    }
    auto inputs() const noexcept -> const BodyInputs& { return data.inputs; }
    auto scopes() const noexcept -> const ScopeTree& { return data.scopes; }
    auto lifetime_regions() const noexcept -> const LifetimeRegionTree& {
        return data.lifetime_regions;
    }
    auto bindings() const noexcept { return data.bindings.entries(); }
    auto patterns() const noexcept { return data.patterns.entries(); }
    auto binding(LocalBindingID id) const noexcept -> const LocalBinding& {
        return data.bindings.get(id);
    }
    auto pattern(PatternID id) const noexcept -> const Pattern& { return data.patterns.get(id); }
    auto region() const noexcept -> const SemIRRegion& { return data.region; }

private:
    SemIRBodyData data;
};

struct StructuredBodyDraft final {
    BodyID id;
    BodyKind kind;
    ProvenanceIdentity provenance_identity;
    BodyInputs inputs;
    ScopeTree scopes;
    LifetimeRegionTree lifetime_regions;
    ImmutableBodyTable<ElaboratedLocalBinding, LocalBindingID> bindings;
    ImmutableBodyTable<ElaboratedPattern, PatternID> patterns;
    DraftRegion region;
};
