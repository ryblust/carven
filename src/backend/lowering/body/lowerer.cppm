module carven:backend.lowering.body.lowerer;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.body;
import :backend.lowering.context;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.stmt;
import :semantic.semir;
import std;

namespace body_lowering {

auto falls_through(std::span<const TargetStmt> statements) noexcept -> bool;

auto transfer_expression(TargetExpr value) noexcept -> TargetExpr;

auto binary_expression(TargetExpr left, TargetBinaryOperator operation, TargetExpr right) noexcept
    -> TargetExpr;

auto prefix_expression(TargetPrefixOperator operation, TargetExpr operand) noexcept -> TargetExpr;

auto template_call_expression(
    TargetExpr callee,
    std::vector<TargetTypeID> template_arguments,
    std::vector<TargetExpr> arguments
) noexcept -> TargetExpr;

auto call_member(
    TargetExpr owner,
    std::string_view member,
    std::vector<TargetExpr> arguments
) noexcept -> TargetExpr;

auto statement_expression(TargetExpr expression) noexcept -> TargetStmt;

auto constant_expression(ModuleLowering& context, ConstantID constant) noexcept -> TargetExpr;

auto literal_expression(ModuleLowering& context, const LiteralValue& literal, TypeID type) noexcept
    -> TargetExpr;

auto typed_integer_expression(
    ModuleLowering& context,
    const IntegerConstant& value,
    TypeID type
) noexcept -> TargetExpr;

auto enum_case_index(const SemIRProgram& semantic, EnumCaseID case_id) noexcept -> std::size_t;

auto enum_case_expression(
    ModuleLowering& context,
    EnumCaseID case_id,
    std::vector<TargetExpr> payload
) noexcept -> TargetExpr;

class BodyLowerer final {
public:
    BodyLowerer(ModuleLowering& source_context, BodyID id, TargetBodyInputs target_inputs) noexcept;

    auto finish() noexcept -> LoweredBody;

private:
    static constexpr auto callable_scope = TargetScopeID {.ordinal = 0};

    struct PatternPayloadStep final {
        EnumCaseID enum_case;
        std::uint32_t payload_index;
        std::optional<TargetIdentifier> projection;
    };

    struct PatternSubject final {
        TargetIdentifier root;
        bool dereference_root;
        std::vector<PatternPayloadStep> payload_path;
    };

    struct PatternConstraint final {
        PatternSubject subject;
        std::variant<ConstantID, EnumCaseID> value;
        std::optional<TargetIdentifier> projection;
    };

    struct PatternBinding final {
        LocalBindingID binding;
        PatternSubject subject;
    };

    struct PatternSelection final {
        std::vector<PatternConstraint> constraints;
        std::vector<PatternBinding> bindings;
        std::optional<TargetIdentifier> failure_projection;
    };

    struct PatternProjection final {
        PatternSubject subject;
        EnumCaseID enum_case;
        TargetIdentifier name;
    };

    enum class ResultUse { Discard, Return, Store };
    struct ResultDestination final {
        ResultUse use;
        std::optional<TargetIdentifier> storage;
    };
    struct FailureDestination final {
        TargetIdentifier storage;
        TargetIdentifier label;
    };
    struct CaughtFailure final {
        TargetIdentifier storage;
        FailureSetID failures;
    };

    auto lower_arm(
        std::vector<PatternSelection> selections,
        std::span<const LocalBindingID> bindings,
        const SemIRRegion& source,
        const std::optional<SemIRExpression>& guard,
        const ResultDestination& result,
        const TargetIdentifier& done
    ) noexcept -> std::vector<TargetStmt>;

    auto known_boolean(const SemIRExpression& source) const noexcept -> std::optional<bool>;
    auto condition(const SemIRExpression& source, std::vector<TargetStmt>& destination) noexcept
        -> TargetExpr;
    auto expression(
        const SemIRExpression& expression,
        std::vector<TargetStmt>& destination
    ) noexcept -> TargetExpr;
    enum class OperandUse { Snapshot, Read, Own, Place, ConstPlace };
    auto initializers(
        std::span<const SemIRExpression* const> sources,
        bool ordered,
        std::vector<TargetStmt>& destination
    ) noexcept -> std::vector<TargetExpr>;
    auto operand(
        const SemIRExpression& expression,
        std::vector<TargetStmt>& destination,
        OperandUse use
    ) noexcept -> TargetExpr;
    auto statement(const SemIRStatement& statement, std::vector<TargetStmt>& destination) noexcept
        -> void;
    auto region(const SemIRRegion& region, ResultDestination result) noexcept
        -> std::vector<TargetStmt>;
    auto result_expression(
        const SemIRExpression& expression,
        ResultDestination result,
        std::vector<TargetStmt>& destination
    ) noexcept -> void;
    auto structured_expression(
        const SemIRExpression& expression,
        ResultDestination result,
        std::vector<TargetStmt>& destination
    ) noexcept -> void;
    auto guarded_region(
        const SemIRRegion& source,
        const std::optional<SemIRExpression>& guard,
        const ResultDestination& result,
        const TargetIdentifier& done
    ) noexcept -> std::vector<TargetStmt>;
    auto lower_if(
        const SemIf<TypeID, FailureSetID>& value,
        ResultDestination result,
        std::vector<TargetStmt>& destination
    ) noexcept -> void;
    auto lower_match(
        const SemMatch<TypeID, FailureSetID>& value,
        const ResultDestination& result,
        std::vector<TargetStmt>& destination
    ) noexcept -> void;
    auto lower_try(
        const SemTry<TypeID, FailureSetID>& value,
        const ResultDestination& result,
        std::vector<TargetStmt>& destination
    ) noexcept -> void;
    auto lower_loop(
        const SemLoop<TypeID, FailureSetID>& value,
        std::vector<TargetStmt>& destination
    ) noexcept -> void;
    auto lower_range(
        const SemRangeLoop<TypeID, FailureSetID>& value,
        std::vector<TargetStmt>& destination
    ) noexcept -> void;
    auto lower_report(
        const SemTestReport<TypeID, FailureSetID>& value,
        ProgramOriginID origin,
        std::vector<TargetStmt>& destination
    ) noexcept -> void;
    auto emit_failure(TargetExpr value, std::vector<TargetStmt>& destination) noexcept -> void;
    auto transfer_failure(
        const TargetIdentifier& storage,
        FailureSetID failures,
        std::vector<TargetStmt>& destination
    ) noexcept -> void;
    auto emit_return(std::optional<TargetExpr> value, std::vector<TargetStmt>& destination) noexcept
        -> void;
    auto binary(TargetExpr left, BinaryOperator operation, TargetExpr right, TypeID type) noexcept
        -> TargetExpr;
    auto field_identifier(StructID owner, std::uint32_t index) noexcept -> TargetIdentifier;
    auto binding_expression(LocalBindingID id) noexcept -> TargetExpr;
    auto declare_binding(
        LocalBindingID id,
        TargetExpr initializer,
        std::vector<TargetStmt>& destination
    ) noexcept -> void;
    auto mark_unused(std::vector<TargetStmt>& statements) noexcept -> void;
    auto mark_unused_expression(TargetExpr& expression) noexcept -> void;

    auto subject_expression(const PatternSubject& subject) noexcept -> TargetExpr;
    auto lower_pattern(PatternID pattern, PatternSubject subject) noexcept
        -> std::vector<PatternSelection>;
    auto cache_pattern_projections(
        std::vector<PatternSelection>& selections,
        std::vector<PatternProjection>& projections,
        std::vector<TargetStmt>& destination
    ) noexcept -> void;
    auto pattern_condition(const PatternSelection& selection) noexcept -> std::optional<TargetExpr>;
    auto pattern_binding_expression(
        const PatternSelection& selection,
        LocalBindingID binding
    ) noexcept -> TargetExpr;
    ModuleLowering& context;
    const SemIRBody& body;
    TargetBodyInputs inputs;
    TargetNameAllocator names;
    std::vector<LocalBindingID> parameter_bindings;
    std::vector<LocalBindingID> capture_bindings;
    std::flat_map<LocalBindingID, TargetIdentifier> binding_names;
    std::flat_set<LocalBindingID> used_bindings;
    std::flat_set<LocalBindingID> taken_bindings;
    std::flat_set<LocalBindingID> delayed_bindings;
    std::optional<FailureDestination> failure_destination;
    std::optional<CaughtFailure> caught_failure;
    bool continue_label_used = false;
    bool uses_test_context = false;
    bool returning_region = false;
};

} // namespace body_lowering
