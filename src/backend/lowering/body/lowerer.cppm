module carven:backend.lowering.body.lowerer;

import :backend.generation.names;
import :backend.generation.plan;
import :backend.lowering.body;
import :backend.lowering.context;
import :backend.target.expr;
import :backend.target.ids;
import :backend.target.name;
import :backend.target.origin;
import :backend.target.stmt;
import :semantic.semir;
import std;

namespace body_lowering {

class StatementSequence final {
public:
    auto continues() const noexcept -> bool { return open; }
    auto empty() const noexcept -> bool { return statements.empty(); }
    auto emit(TargetStmt statement, bool continues = true) noexcept -> void {
        if (!open) {
            return;
        }
        statements.push_back(std::move(statement));
        open = continues;
    }
    auto terminate(TargetStmt statement) noexcept -> void { emit(std::move(statement), false); }
    auto append(StatementSequence source) noexcept -> void {
        if (!open) {
            return;
        }
        statements.insert(
            statements.end(),
            std::make_move_iterator(source.statements.begin()),
            std::make_move_iterator(source.statements.end())
        );
        open = source.open;
    }
    auto block(
        StatementSequence source,
        TargetAttribution attribution = TargetGeneratedExpansionAttribution {
            .reason = TargetExpansionReason::LoweringSupport
        }
    ) noexcept -> void {
        const auto continues = source.open;
        emit(
            TargetStmt {
                .value = TargetBlockStmt {.statements = std::move(source).finish(), .scoped = true},
                .attribution = std::move(attribution)
            },
            continues
        );
    }
    auto resume(TargetIdentifier label, TargetJumpRole role) noexcept -> void {
        open = true;
        emit(
            TargetStmt {
                .value = TargetLabelStmt {.label = std::move(label), .role = role},
                .attribution = TargetGeneratedExpansionAttribution {
                    .reason = TargetExpansionReason::LoweringSupport
                }
            }
        );
    }
    auto finish() && noexcept -> std::vector<TargetStmt> { return std::move(statements); }

private:
    std::vector<TargetStmt> statements;
    bool open = true;
};


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
        bool used = false;
    };
    struct CaughtFailure final {
        TargetIdentifier storage;
        FailureSetID failures;
    };

    struct RegionExit final {
        TargetIdentifier label;
        bool used = false;
    };

    auto lower_arm(
        std::vector<PatternSelection> selections,
        std::span<const LocalBindingID> bindings,
        const SemanticRegion& source,
        const std::optional<SemanticExpression>& guard,
        const ResultDestination& result,
        RegionExit& done
    ) noexcept -> StatementSequence;

    auto known_boolean(const SemanticExpression& source) const noexcept -> std::optional<bool>;
    auto condition(const SemanticExpression& source, StatementSequence& destination) noexcept
        -> std::optional<TargetExpr>;
    auto cpp_call(const SemCppCall& call, StatementSequence& destination) noexcept
        -> std::optional<TargetExpr>;
    auto cpp_operation(
        const SemanticExpression& source,
        const SemCpp& operation,
        StatementSequence& destination
    ) noexcept -> std::optional<TargetExpr>;
    auto expression(const SemanticExpression& expression, StatementSequence& destination) noexcept
        -> std::optional<TargetExpr>;
    enum class OperandUse { Direct, Snapshot, Read, Own, Place, ConstPlace };
    auto initializers(
        std::span<const SemanticExpression* const> sources,
        bool ordered,
        StatementSequence& destination
    ) noexcept -> std::optional<std::vector<TargetExpr>>;
    auto operand(
        const SemanticExpression& expression,
        StatementSequence& destination,
        OperandUse use
    ) noexcept -> std::optional<TargetExpr>;
    struct Operand final {
        const SemanticExpression& expression;
        OperandUse use;
    };
    auto operands(std::span<const Operand> sources, StatementSequence& destination) noexcept
        -> std::optional<std::vector<TargetExpr>>;
    auto statement(const SemanticStatement& statement, StatementSequence& destination) noexcept
        -> void;
    auto region(const SemanticRegion& region, ResultDestination result) noexcept
        -> StatementSequence;
    auto result_expression(
        const SemanticExpression& expression,
        ResultDestination result,
        StatementSequence& destination
    ) noexcept -> void;
    auto structured_expression(
        const SemanticExpression& expression,
        ResultDestination result,
        StatementSequence& destination
    ) noexcept -> void;
    auto guarded_region(
        const SemanticRegion& source,
        const std::optional<SemanticExpression>& guard,
        const ResultDestination& result,
        RegionExit& done
    ) noexcept -> StatementSequence;
    auto lower_if(
        const SemIf& value,
        ResultDestination result,
        StatementSequence& destination
    ) noexcept -> void;
    auto lower_match(
        const SemMatch& value,
        const ResultDestination& result,
        StatementSequence& destination
    ) noexcept -> void;
    auto lower_try(
        const SemTry& value,
        const ResultDestination& result,
        StatementSequence& destination
    ) noexcept -> void;
    auto lower_loop(const SemLoop& value, StatementSequence& destination) noexcept -> void;
    auto lower_range(const SemRangeLoop& value, StatementSequence& destination) noexcept -> void;
    auto lower_report(
        const SemTestReport& value,
        ProgramOriginID origin,
        StatementSequence& destination
    ) noexcept -> void;
    auto emit_failure(TargetExpr value, StatementSequence& destination) noexcept -> void;
    auto transfer_failure(
        const TargetIdentifier& storage,
        FailureSetID failures,
        StatementSequence& destination
    ) noexcept -> void;
    auto emit_return(std::optional<TargetExpr> value, StatementSequence& destination) noexcept
        -> void;
    auto binary(TargetExpr left, BinaryOperator operation, TargetExpr right, TypeID type) noexcept
        -> TargetExpr;
    auto field_identifier(StructID owner, std::uint32_t index) noexcept -> TargetIdentifier;
    auto binding_expression(LocalBindingID id) noexcept -> TargetExpr;
    auto declare_binding(
        LocalBindingID id,
        TargetExpr initializer,
        StatementSequence& destination
    ) noexcept -> void;

    auto subject_expression(const PatternSubject& subject) noexcept -> TargetExpr;
    auto lower_pattern(PatternID pattern, PatternSubject subject) noexcept
        -> std::vector<PatternSelection>;
    auto cache_pattern_projections(
        std::vector<PatternSelection>& selections,
        std::vector<PatternProjection>& projections,
        StatementSequence& destination
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
    struct LoopContinuation final {
        std::optional<TargetIdentifier> step;
        bool used = false;
        bool breaks = false;
    };
    LoopContinuation loop_continuation;
    bool uses_test_context = false;
    std::optional<bool> region_return;
};

} // namespace body_lowering
