module carven:backend.construction;

import :semantic.semir;
import :support.invariant;
import std;

class BodyConstructionBuilder;
class ConstructionTestingFixture;

template<typename Tag>
class ConstructionID final {
public:
    auto owner() const noexcept -> BodyID { return body; }

    auto index() const noexcept -> std::uint32_t { return ordinal; }

    auto operator<=>(const ConstructionID&) const noexcept = default;

private:
    ConstructionID(BodyID body, std::uint32_t ordinal) noexcept
        : body(body),
          ordinal(ordinal) {}

    BodyID body;
    std::uint32_t ordinal;
    friend class BodyConstructionBuilder;
    friend class ConstructionTestingFixture;
};

struct ConstructionExpressionTag final {};

struct ConstructionRegionTag final {};

using ConstructionExpressionID = ConstructionID<ConstructionExpressionTag>;
using ConstructionRegionID = ConstructionID<ConstructionRegionTag>;

// ReadBorrow observes an object; AddressValue reads a pointer slot. Neither
// requires a C++ local declaration. Realization chooses storage at boundaries.
// NativeTake additionally preserves the C++ query contract T&&, including for
// trivial values whose Carven transfer policy otherwise observes const T&.
enum class ConstructionUse {
    ReadBorrow,
    AddressValue,
    OperandValue,
    Place,
    ConstPlace,
    Consume,
    NativeTake
};

struct ConstructionOperand final {
    ConstructionExpressionID expression;
    ConstructionUse use;
};

struct ConstructionFunctionExit final {};

struct ConstructionHandlerExit final {
    ConstructionExpressionID handler;
};

using ConstructionFailureExit = std::variant<ConstructionFunctionExit, ConstructionHandlerExit>;

struct ConstructionCaughtFailure final {
    ConstructionExpressionID handler;
    FailureSetID failures;
};

struct ConstructionFallible final {
    FailureSetID failures;
    ConstructionFailureExit destination;
};

struct ConstructionOperation final {
    std::vector<ConstructionOperand> operands;
    std::optional<ConstructionFallible> failure;
};

struct ConstructionShortCircuit final {
    ConstructionExpressionID condition;
    ShortCircuitOperator operation;
    ConstructionExpressionID selected;
};

struct ConstructionConditionalBranch final {
    ConstructionExpressionID condition;
    ConstructionRegionID body;
};

struct ConstructionConditional final {
    std::vector<ConstructionConditionalBranch> branches;
    std::optional<ConstructionRegionID> otherwise;
};

struct ConstructionMatchArm final {
    PatternID pattern_id;
    std::vector<LocalBindingID> bindings;
    std::optional<ConstructionExpressionID> guard;
    ConstructionRegionID body;
};

struct ConstructionMatch final {
    ConstructionExpressionID subject;
    bool subject_is_place;
    std::vector<ConstructionMatchArm> arms;
};

struct ConstructionTypedCatch final {
    TypeID type;
    PatternID pattern_id;
};

struct ConstructionCatchAlternative final {
    std::variant<CatchAllPattern, ConstructionTypedCatch> pattern;
};

struct ConstructionCatchArm final {
    FailureSetID accepted_failures;
    std::vector<ConstructionCatchAlternative> alternatives;
    std::vector<LocalBindingID> bindings;
    std::optional<ConstructionExpressionID> guard;
    ConstructionRegionID body;
};

struct ConstructionTry final {
    ConstructionRegionID body;
    FailureSetID protected_failures;
    FailureSetID residual_failures;
    ConstructionFailureExit residual_destination;
    std::vector<ConstructionCatchArm> arms;
};

using ConstructionExpressionValue = std::variant<
    ConstructionOperation,
    ConstructionShortCircuit,
    ConstructionConditional,
    ConstructionMatch,
    ConstructionTry>;

struct ConstructionExpression final {
    const SemanticExpression& operation;
    TypeID type;
    LifetimeRegionID lifetime;
    ProgramOriginID origin;
    SemanticValueCategory category;
    std::optional<ConstantID> constant;
    bool executes_operation;
    bool requires_execution;
    bool reads_storage;
    bool exits_test;
    FailureSetID failures;
    ConstructionExpressionValue value;
};

struct ConstructionReturn final {
    std::optional<ConstructionExpressionID> value;
};

struct ConstructionLoopTransfer final {
    ConstructionRegionID loop;
    bool continue_loop;
};

struct ConstructionThrow final {
    ConstructionExpressionID value;
    ConstructionFailureExit destination;
};

struct ConstructionRethrow final {
    ConstructionCaughtFailure source;
    ConstructionFailureExit destination;
};

struct ConstructionDiscard final {
    ConstructionExpressionID expression;
};

struct ConstructionInitialize final {
    LocalBindingID binding;
    ConstructionExpressionID initializer;
};

struct ConstructionAssign final {
    ConstructionExpressionID target;
    std::optional<BinaryOperator> compound;
    ConstructionExpressionID value;
};

struct ConstructionScope final {
    ConstructionRegionID region;
};

struct ConstructionLoop final {
    ConstructionRegionID initializer;
    std::optional<ConstructionExpressionID> condition;
    ConstructionRegionID body;
    ConstructionRegionID steps;
};

struct ConstructionIntegerRange final {
    ConstructionExpressionID begin;
    ConstructionExpressionID end;
};

struct ConstructionSequenceRange final {
    ConstructionExpressionID value;
};

struct ConstructionRangeLoop final {
    LifetimeRegionID lifetime;
    AccessMode access;
    std::optional<LocalBindingID> binding;
    std::variant<ConstructionIntegerRange, ConstructionSequenceRange> source;
    ConstructionRegionID body;
};

struct ConstructionTestReport final {
    TestReportKind kind;
    std::optional<ConstructionExpressionID> condition;
    std::optional<ConstructionExpressionID> message;
    std::optional<ProgramSpellingID> condition_source;
};

using ConstructionStatementValue = std::variant<
    ConstructionReturn,
    ConstructionLoopTransfer,
    ConstructionThrow,
    ConstructionRethrow,
    ConstructionDiscard,
    ConstructionInitialize,
    ConstructionAssign,
    ConstructionScope,
    ConstructionLoop,
    ConstructionRangeLoop,
    ConstructionTestReport>;

struct ConstructionStatement final {
    LifetimeRegionID lifetime;
    ProgramOriginID origin;
    ConstructionStatementValue value;
};

struct ConstructionRegion final {
    LifetimeRegionID lifetime;
    ProgramOriginID origin;
    std::vector<ConstructionStatement> statements;
    std::optional<ConstructionExpressionID> result;
};

class BodyConstruction final {
public:
    BodyConstruction(const BodyConstruction&) = delete;
    BodyConstruction(BodyConstruction&&) = default;
    auto operator=(const BodyConstruction&) -> BodyConstruction& = delete;
    auto operator=(BodyConstruction&&) -> BodyConstruction& = delete;

    auto body() const noexcept -> BodyID { return body_id; }

    auto root() const noexcept -> ConstructionRegionID { return root_region; }

    auto expression(ConstructionExpressionID id) const noexcept -> const ConstructionExpression& {
        if (id.owner() != body_id || id.index() >= expressions.size()) {
            invariant_violation(
                "construction expression belongs to another body or is out of range"
            );
        }
        return expressions[id.index()];
    }

    auto region(ConstructionRegionID id) const noexcept -> const ConstructionRegion& {
        if (id.owner() != body_id || id.index() >= regions.size()) {
            invariant_violation("construction region belongs to another body or is out of range");
        }
        return regions[id.index()];
    }

    auto expression_values() const noexcept -> std::span<const ConstructionExpression> {
        return expressions;
    }

    auto region_values() const noexcept -> std::span<const ConstructionRegion> { return regions; }

private:
    BodyConstruction(
        BodyID body,
        ConstructionRegionID root,
        std::vector<ConstructionExpression> expressions,
        std::vector<ConstructionRegion> regions
    ) noexcept
        : body_id(body),
          root_region(root),
          expressions(std::move(expressions)),
          regions(std::move(regions)) {}

    BodyID body_id;
    ConstructionRegionID root_region;
    std::vector<ConstructionExpression> expressions;
    std::vector<ConstructionRegion> regions;
    friend class BodyConstructionBuilder;
    friend class ConstructionTestingFixture;
};

// Semantic IDs remain borrowed from the published program, which outlives the
// construction and its realization, including borrowed source operations.
auto construct_body(const SemIRProgram& semantic, BodyID body_id) noexcept -> BodyConstruction;

// Source-ordered inputs and their access contract. Structured execution edges
// are visited separately; they have no ordinary operands.
auto construction_operands(const ConstructionExpression& source) noexcept
    -> std::span<const ConstructionOperand>;
