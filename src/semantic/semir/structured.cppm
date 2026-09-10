module carven:semantic.semir.structured;

import :semantic.semir.body;
import :semantic.semir.ids;
import :semantic.semir.type;
import :support.invariant;
import :support.unique_indirect;
import std;

class BodyType final {
public:
    explicit BodyType(ConstructionTypeRef value) noexcept
        : value(value) {}

    explicit BodyType(TypeID value) noexcept
        : value(value) {}

    explicit BodyType(TypeTermID value) noexcept
        : value(value) {}

    auto construction() const noexcept -> const ConstructionTypeRef& { return value; }

    auto resolved() const noexcept -> TypeID {
        if (const auto* type = std::get_if<TypeID>(&value)) {
            return *type;
        }
        invariant_violation("body type has not been resolved");
    }

private:
    ConstructionTypeRef value;
};

class BodyFailures final {
public:
    explicit BodyFailures(FailureTermID value) noexcept
        : value(value) {}

    explicit BodyFailures(FailureSetID value) noexcept
        : value(value) {}

    auto term() const noexcept -> FailureTermID {
        if (const auto* term = std::get_if<FailureTermID>(&value)) {
            return *term;
        }
        invariant_violation("resolved body failures have no construction term");
    }

    auto resolved() const noexcept -> FailureSetID {
        if (const auto* failures = std::get_if<FailureSetID>(&value)) {
            return *failures;
        }
        invariant_violation("body failures have not been resolved");
    }

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
    ArrayBoundsPolicy bounds;
};

struct SemFormat final {
    ConstantID format_string_id;
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

struct SemTestReport final {
    TestReportKind kind;
    std::optional<SemanticExpression> condition;
    std::optional<SemanticExpression> message;
    std::optional<ProgramSpellingID> condition_source;
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
        SemTestReport,
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

    auto id() const noexcept -> BodyID { return data.id; }

    auto kind() const noexcept -> BodyKind { return data.kind; }

    auto identity() const noexcept -> BodyIdentity { return data.lifetime_regions.owner(); }

    auto provenance_identity() const noexcept -> ProvenanceIdentity {
        return data.provenance_identity;
    }

    auto inputs() const noexcept -> const BodyInputs& { return data.inputs; }

    auto lifetime_regions() const noexcept -> const LifetimeRegionTree& {
        return data.lifetime_regions;
    }

    auto bindings() const noexcept { return data.bindings.entries(); }

    auto patterns() const noexcept { return data.patterns.entries(); }

    auto pattern_table() const noexcept -> const ImmutableBodyTable<Pattern, PatternID>& {
        return data.patterns;
    }

    auto binding(LocalBindingID id) const noexcept -> const LocalBinding& {
        return data.bindings.get(id);
    }

    auto pattern(PatternID id) const noexcept -> const Pattern& { return data.patterns.get(id); }

    auto region() const noexcept -> const SemanticRegion& { return data.region; }

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
