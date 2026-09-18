module carven:semantic.analysis.body.context;

import :diagnostics.builder;
import :diagnostics.code;
import :diagnostics.diagnostic;
import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.resolve;
import :semantic.analysis.construction.requests;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.evaluation.operation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
import :source.text;
import :support.invariant;
import :support.visit;
import std;

enum class BodyLocalRole {
    Local,
    Parameter,
    Capture,
    RangeRead,
};

struct BodyLocalStorage final {
    std::variant<BoundStorage, ConstantID> storage;
    ConstructionTypeRef type;
    bool used;
    bool takeable;
    BodyLocalRole role;
    std::optional<Span> unused_candidate;
};

using BodyLocalNames = std::flat_map<std::string, BodyLocalStorage, std::less<>>;

struct BodyLocalFrame final {
    LifetimeRegionID lifetime;
    BodyLocalNames names;
};

using BodyExpressionStorage = std::variant<SemanticExpression, PlaceExpression>;
using BodyPendingFailureTerms = std::vector<FailureTermID>;

struct BuiltExpression final {
    BodyExpressionStorage storage;
    BodyPendingFailureTerms pending_failures;
    bool takeable;
    bool completes;

    auto is_function_reference() const noexcept -> bool;
    auto expression() const noexcept -> const SemanticExpression&;
    auto type() const noexcept -> const ConstructionTypeRef&;
    auto constant() const noexcept -> std::optional<ConstantID>;
};

struct CppMemberSelection final {
    BuiltExpression receiver;
    std::string member;
};

struct CppSelection final {
    std::variant<CppNameReference, CppMemberSelection> target;
    Span span;
};

enum class BuiltinFunction { Print, Println, Eprint, Eprintln, Check, Require, Fail };

struct BuiltinSelection final {
    BuiltinFunction function;
    Span span;
    std::optional<ProgramSpellingID> condition_source;
    std::optional<std::array<ProgramSpellingID, 2>> operand_sources;
};

using SelectedExpression = std::variant<BuiltExpression, CppSelection, BuiltinSelection>;

struct BuiltCallArgument final {
    SemCallArgument argument;
    BodyPendingFailureTerms pending_failures;
    bool completes;
};

auto does_not_complete(const BuiltExpression& expression) noexcept -> bool {
    return !expression.completes;
}

auto take_pending_failures(BuiltExpression& expression) noexcept -> BodyPendingFailureTerms {
    return std::exchange(expression.pending_failures, {});
}

auto append_pending_failures(
    BodyPendingFailureTerms& destination,
    const BodyPendingFailureTerms& source
) noexcept -> void {
    for (const auto failure_term_id : source) {
        if (std::ranges::contains(destination, failure_term_id)) {
            continue;
        }
        destination.push_back(failure_term_id);
    }
}

struct BodyFailureContext final {
    FailureTermID term;
    bool accepts_catch_residual;
};

struct BodyLoopContext final {
    bool has_break;
    bool has_continue;
};

struct BodyCatchContext final {
    FailureTermID failures;
    BodyFailureContext outward;
};

struct BuiltPattern final {
    PatternID pattern;
    std::vector<LocalBindingID> bindings;
    bool irrefutable;
};

struct BodyPatternBindingStorage final {
    BoundStorage storage;
    ConstructionTypeRef type;
};

class BodyFullExpressionSuspension final {
public:
    explicit BodyFullExpressionSuspension(std::optional<LifetimeRegionID>& active) noexcept;
    BodyFullExpressionSuspension(const BodyFullExpressionSuspension&) = delete;
    BodyFullExpressionSuspension(BodyFullExpressionSuspension&&) = delete;
    auto operator=(const BodyFullExpressionSuspension&) -> BodyFullExpressionSuspension& = delete;
    auto operator=(BodyFullExpressionSuspension&&) -> BodyFullExpressionSuspension& = delete;
    ~BodyFullExpressionSuspension() noexcept;

private:
    std::optional<LifetimeRegionID>* slot;
    std::optional<LifetimeRegionID> saved;
};

class BodyReferencePathGuard final {
public:
    BodyReferencePathGuard(bool& active, bool path_is_reachable) noexcept;
    BodyReferencePathGuard(const BodyReferencePathGuard&) = delete;
    BodyReferencePathGuard(BodyReferencePathGuard&&) = delete;
    auto operator=(const BodyReferencePathGuard&) -> BodyReferencePathGuard& = delete;
    auto operator=(BodyReferencePathGuard&&) -> BodyReferencePathGuard& = delete;
    ~BodyReferencePathGuard() noexcept;

private:
    bool* slot;
    bool saved;
};

auto is_void_type(const ProgramDraft& draft, ConstructionTypeRef type) noexcept -> bool {
    const auto* concrete = std::get_if<TypeID>(&type);
    if (concrete == nullptr) {
        return false;
    }
    return draft.type_copy(*concrete).value
        == CanonicalTypeValue {BuiltinTypeValue {.kind = BuiltinType::Void}};
}

auto is_failure_payload_type(const ProgramDraft& draft, TypeID type) noexcept -> bool {
    const auto& canonical = draft.type_copy(type).value;
    return std::holds_alternative<StructTypeValue>(canonical)
        || std::holds_alternative<EnumTypeValue>(canonical);
}

auto known_boolean_constant(const ProgramDraft& draft, std::optional<ConstantID> constant) noexcept
    -> std::optional<bool> {
    if (!constant.has_value()) {
        return std::nullopt;
    }
    const auto& fact = draft.constant(*constant);
    const auto* boolean = std::get_if<BooleanConstant>(&fact.value);
    return boolean == nullptr ? std::nullopt : std::optional(boolean->value);
}

auto create_body_failure_term(
    ProgramDraft& draft,
    FailureTermID failures,
    FailureContractPolicy policy,
    ProgramOriginID origin
) noexcept -> FailureTermID {
    const auto actual = draft.add_empty_failure_term();
    switch (policy) {
        case FailureContractPolicy::Declared: {
            draft.require_failure_subset(
                actual,
                failures,
                origin,
                FailureSubsetRequirementKind::DeclaredCallable
            );
            break;
        }
        case FailureContractPolicy::Inferred:
        case FailureContractPolicy::UndeclaredExplicit: {
            draft.equate_failures(actual, failures);
            if (policy == FailureContractPolicy::UndeclaredExplicit) {
                draft.require_declared_failure_contract(actual, origin);
            }
            break;
        }
    }
    return actual;
}

class BodyBatchElaborator;

class BodyElaborator final {
    friend class BodyExprSite;
    friend class BodyBatchElaborator;

public:
    BodyElaborator(
        BodyBatchElaborator& owner,
        ProgramModuleID source_module_id,
        ModuleID semantic_module_id,
        ASTView source,
        BodyReservation&& reservation,
        std::optional<ConstructionTypeRef> result,
        FailureTermID outward_failure_term_id,
        bool accepts_catch_residual,
        bool test_body
    ) noexcept;
    auto add_parameter(
        const ASTFunctionParameter& source,
        const ConstructionCallableParameter& contract
    ) noexcept -> AnalysisResult<void>;
    auto add_capture(Span name, ConstructionTypeRef type, CaptureMode mode) noexcept
        -> AnalysisResult<void>;
    auto inferred_result_type() const noexcept -> ConstructionTypeRef;
    auto local_was_used(std::string_view name) const noexcept -> bool;
    auto run(const ASTCallableBody& source_body) noexcept -> AnalysisTask<StructuredBodyDraft>;

private:
    auto draft() const noexcept -> ProgramDraft&;
    auto catalog() const noexcept -> AnalysisCatalogView;
    auto import_usage() const noexcept -> ImportUsage&;
    auto origin(Span span) noexcept -> ProgramOriginID;
    auto expansion(Span span, ProgramExpansionReason reason) noexcept -> ProgramOriginID;
    auto spelling(Span span) const noexcept -> std::string;
    auto fail(Span span, DiagnosticCode code, std::string message) noexcept -> AnalysisFailure;
    auto warn(Span span, DiagnosticCode code, std::string message) noexcept -> void;
    auto resolve_type(ASTTypeID type) noexcept -> AnalysisTask<ConstructionTypeRef>;
    auto resolve_construction_type(const ASTConstructionType& type) noexcept
        -> AnalysisTask<ConstructionTypeRef>;
    auto resolve_array_extent(ASTExprID expression) noexcept -> AnalysisTask<std::uint64_t>;
    auto resolve_constant_name(std::string_view name, Span span) noexcept
        -> AnalysisTask<std::optional<ConstantID>>;
    auto resolve_function(std::string_view name, Span span) noexcept
        -> AnalysisTask<std::optional<FunctionID>>;
    auto construction_requests() noexcept -> ConstructionRequests&;
    auto resolve_enum_qualifier(ASTExprID expression) noexcept
        -> AnalysisTask<std::optional<TypeID>>;
    auto resolve_constant_enum_case(TypeID type, std::string_view name, Span span) noexcept
        -> AnalysisTask<ResolvedEnumCase>;
    auto compatible(ConstructionTypeRef left, ConstructionTypeRef right) const noexcept -> bool;
    auto require_adaptation(
        ConstructionTypeRef source,
        ConstructionTypeRef target,
        Span span
    ) noexcept -> AnalysisResult<void>;
    auto require_invariant_type(
        ConstructionTypeRef source,
        ConstructionTypeRef target,
        Span span
    ) noexcept -> AnalysisResult<void>;
    auto require_bool(BuiltExpression& expression, Span span) noexcept
        -> AnalysisResult<SemanticExpression>;
    auto active_builder() noexcept -> BodyBuilder&;
    auto begin_full_expression(Span span) noexcept -> void;
    auto end_full_expression(Span span) noexcept -> void;
    auto ensure_reachable_diagnostics(Span span) noexcept -> void;
    auto empty_region(Span span) noexcept -> SemanticRegion;
    auto take_built(BuiltExpression& value, Span span) noexcept -> SemanticExpression;
    auto make_built(
        ConstructionTypeRef type,
        SemanticExpressionValue value,
        Span span,
        BodyPendingFailureTerms pending = {},
        std::optional<ConstantID> constant = std::nullopt
    ) noexcept -> BuiltExpression;
    auto mark_noncompleting(BuiltExpression value) noexcept -> BuiltExpression;
    auto append_statement(
        decltype(SemanticStatement::value) value,
        ProgramOriginID statement_origin
    ) noexcept -> void;
    auto append_expression(BuiltExpression& expression, Span span) noexcept -> void;
    auto build_branch(
        ASTBranchBlockID id,
        bool value_form,
        std::optional<ConstructionTypeRef>& result_type,
        BodyPendingFailureTerms& pending,
        bool allow_pointer_narrowing
    ) noexcept -> AnalysisTask<SemanticRegion>;
    auto build_arm(
        const ASTMatchArmBody& source,
        bool value_form,
        std::optional<ConstructionTypeRef>& type,
        BodyPendingFailureTerms& pending,
        bool allow_pointer_narrowing
    ) noexcept -> AnalysisTask<SemanticRegion>;
    auto build_if(
        const ASTIfForm& source,
        Span span,
        std::optional<ConstructionTypeRef> expected,
        bool value_form,
        bool allow_pointer_narrowing = true
    ) noexcept -> AnalysisTask<BuiltExpression>;
    auto push_frame(Span span) noexcept -> void;
    auto pop_frame(bool diagnose = true) noexcept -> void;
    auto collect_unused_locals(const BodyLocalFrame& frame) noexcept -> void;
    auto visible_locals() const noexcept -> BodyLocalNames;
    auto bind_local(Span name, BodyLocalStorage storage, DiagnosticCode duplicate_code) noexcept
        -> AnalysisResult<void>;
    auto find_local(std::string_view name) const noexcept -> const BodyLocalStorage*;
    auto use_local(std::string_view name) noexcept -> BodyLocalStorage*;
    auto find_global(std::string_view name, Span span) noexcept
        -> AnalysisTask<const CatalogSymbol*>;
    auto consume_value(BuiltExpression& expression, Span span, AccessMode access) noexcept
        -> AnalysisResult<SemanticExpression>;
    auto dereference_expression(const ASTPrefixExpr& source, Span span) noexcept
        -> AnalysisTask<BuiltExpression>;
    auto consume_place(BuiltExpression& expression, Span span) noexcept
        -> AnalysisResult<PlaceExpression>;
    auto coerce_to(BuiltExpression& expression, ConstructionTypeRef target, Span span) noexcept
        -> AnalysisResult<void>;
    auto infer_value_type(BuiltExpression& expression, Span span) noexcept
        -> AnalysisResult<ConstructionTypeRef>;
    auto consume_pending(BuiltExpression& expression, Span span) noexcept -> AnalysisResult<void>;
    auto propagate_pending(BuiltExpression& expression, Span span) noexcept -> AnalysisResult<void>;
    auto collect_pending(BodyPendingFailureTerms& destination, BuiltExpression& expression) noexcept
        -> void;
    auto discard_pending(BuiltExpression& expression) noexcept -> void;
    auto route_pending(
        BodyPendingFailureTerms failure_term_ids,
        const BodyFailureContext& target,
        std::optional<Span> propagation_span
    ) noexcept -> void;
    auto failure_context_for_current_path() const noexcept -> const BodyFailureContext&;
    auto expression(
        ASTExprID id,
        std::optional<ConstructionTypeRef> expected = std::nullopt,
        bool allow_pointer_narrowing = true
    ) noexcept -> AnalysisTask<BuiltExpression>;
    auto select_expression(
        ASTExprID id,
        std::optional<ConstructionTypeRef> expected = std::nullopt,
        bool allow_pointer_narrowing = true
    ) noexcept -> AnalysisTask<SelectedExpression>;
    auto materialize_selection(
        SelectedExpression selected,
        std::optional<ConstructionTypeRef> expected = std::nullopt
    ) noexcept -> AnalysisResult<BuiltExpression>;
    auto cpp_projection(
        BuiltExpression receiver,
        CppOperation operation,
        std::vector<SemCallArgument> operands,
        Span span
    ) noexcept -> AnalysisResult<BuiltExpression>;
    auto cpp_result_type(
        const CppOperation& operation,
        std::span<const CppTypeOperand> operands
    ) noexcept -> TypeID;
    auto is_cpp_type(ConstructionTypeRef type) const noexcept -> bool;
    auto cpp_expression(
        CppOperation operation,
        std::vector<SemCallArgument> operands,
        Span span,
        std::optional<ConstructionTypeRef> type = std::nullopt
    ) noexcept -> AnalysisResult<BuiltExpression>;

    struct BuiltCppArgument final {
        SemCallArgument argument;
        bool completes;
    };

    auto build_cpp_argument(ASTExprID source) noexcept -> AnalysisTask<BuiltCppArgument>;
    auto cpp_call(SelectedExpression callee, const ASTCallExpr& source, Span span) noexcept
        -> AnalysisTask<BuiltExpression>;
    auto select_cpp_name(const ASTCppNameExpr& name, Span span) noexcept
        -> AnalysisResult<SelectedExpression>;
    auto validate_builtin(
        BuiltinSelection selection,
        std::span<const ConstructionCallableParameter> parameters,
        std::span<const Span> argument_spans
    ) noexcept -> AnalysisResult<void>;
    auto builtin_operation(
        BodyBuilder& builder,
        BuiltinSelection selection,
        std::vector<SemCallArgument> arguments
    ) noexcept -> SemanticExpression;
    auto builtin_callable(
        BuiltinSelection selection,
        std::span<const ConstructionCallableParameter> parameters,
        std::span<const Span> argument_spans = {}
    ) noexcept -> AnalysisResult<BuiltExpression>;
    auto select_name(const ASTNameExpr& name, Span span) noexcept
        -> AnalysisTask<SelectedExpression>;
    auto cpp_construct(
        const ASTConstructionExpr& source,
        ConstructionTypeRef target,
        Span span
    ) noexcept -> AnalysisTask<BuiltExpression>;
    auto access_expression(const ASTAccessExpr& source, Span span) noexcept
        -> AnalysisTask<BuiltExpression>;
    auto call_expression(
        const ASTCallExpr& source,
        Span span,
        std::optional<SelectedExpression> prepared_callee = std::nullopt
    ) noexcept -> AnalysisTask<BuiltExpression>;
    auto enum_case_reference(
        TypeID enumeration_type,
        std::string_view case_name,
        Span span,
        Span case_span
    ) noexcept -> AnalysisTask<BuiltExpression>;
    auto propagation_expression(const ASTPropagationExpr& source, Span span) noexcept
        -> AnalysisTask<BuiltExpression>;
    auto conditional_expression(
        const ASTIfForm& source,
        Span span,
        std::optional<ConstructionTypeRef> expected,
        bool allow_pointer_narrowing = true
    ) noexcept -> AnalysisTask<BuiltExpression>;
    auto match_expression(
        const ASTMatchForm& source,
        Span span,
        std::optional<ConstructionTypeRef> expected,
        bool allow_pointer_narrowing = true
    ) noexcept -> AnalysisTask<BuiltExpression>;
    auto match_statement(const ASTMatchForm& source, Span span) noexcept -> AnalysisTask<void>;
    auto try_expression(
        const ASTTryForm& source,
        Span span,
        std::optional<ConstructionTypeRef> expected,
        bool allow_pointer_narrowing = true
    ) noexcept -> AnalysisTask<BuiltExpression>;
    auto try_statement(const ASTTryForm& source, Span span) noexcept -> AnalysisTask<void>;
    auto build_try(
        const ASTTryForm& source,
        Span span,
        std::optional<ConstructionTypeRef> expected,
        bool value_form,
        bool allow_pointer_narrowing = true
    ) noexcept -> AnalysisTask<std::optional<BuiltExpression>>;
    auto build_pattern(
        ASTPatternID source,
        ConstructionTypeRef type,
        std::flat_map<std::string, BodyPatternBindingStorage, std::less<>>& bindings,
        bool allow_new_bindings,
        std::flat_set<std::string, std::less<>>& used_bindings,
        std::vector<SemPatternBounds>& pattern_bounds
    ) noexcept -> AnalysisTask<BuiltPattern>;
    auto resolve_pattern_constraint(const ASTConstraintOperand& operand) noexcept
        -> AnalysisTask<ConstructionTypeRef>;
    auto build_match(
        const ASTMatchForm& source,
        Span span,
        std::optional<ConstructionTypeRef> expected,
        bool value_form,
        bool allow_pointer_narrowing = true
    ) noexcept -> AnalysisTask<std::optional<BuiltExpression>>;
    auto lambda_expression(
        const ASTLambdaExpr& source,
        Span span,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisTask<BuiltExpression>;
    auto statement(ASTStmtID id) noexcept -> AnalysisTask<void>;
    auto variable_statement(const ASTVariableDecl& source) noexcept -> AnalysisTask<void>;
    auto assignment_statement(const ASTAssignment& source) noexcept -> AnalysisTask<void>;
    auto update_statement(const ASTUpdate& source) noexcept -> AnalysisTask<void>;
    auto transfer_statement(const ASTControlTransfer& source) noexcept -> AnalysisTask<void>;
    auto while_statement(const ASTWhileStmt& source, Span span) noexcept -> AnalysisTask<void>;
    auto for_statement(const ASTForStmt& source, Span span) noexcept -> AnalysisTask<void>;
    auto c_style_for_statement(
        const ASTForStmt& source,
        const ASTCStyleForHeader& header,
        Span span
    ) noexcept -> AnalysisTask<void>;
    auto range_for_statement(
        const ASTForStmt& source,
        const ASTRangeForHeader& header,
        Span span
    ) noexcept -> AnalysisTask<void>;
    auto if_statement(const ASTIfForm& source, Span span) noexcept -> AnalysisTask<void>;
    auto return_statement(
        std::optional<ASTExprID> operand,
        Span span,
        Span keyword_span,
        bool implicit
    ) noexcept -> AnalysisTask<void>;
    auto block(ASTBlockID id) noexcept -> AnalysisTask<void>;
    auto branch_block(
        ASTBranchBlockID id,
        bool consume_result,
        std::optional<ConstructionTypeRef> expected = std::nullopt,
        bool allow_pointer_narrowing = true
    ) noexcept -> AnalysisTask<std::optional<BuiltExpression>>;
    auto callable_contract(ConstructionTypeRef type, Span span) noexcept
        -> AnalysisResult<ConstructionCallableContract>;
    auto build_call_argument(
        ASTExprID source,
        AccessMode access,
        std::optional<ConstructionTypeRef> expected,
        std::optional<DiagnosticCode> mismatch_code = std::nullopt
    ) noexcept -> AnalysisTask<BuiltCallArgument>;

    BodyLocalNames inherited_locals;
    BodyBatchElaborator* batch;
    ProgramModuleID source_module_id;
    ModuleID semantic_module_id;
    ASTView ast;
    BodyBuilder body_builder;
    std::optional<ConstructionTypeRef> result_type;
    bool infer_result;
    FailureTermID outward_failure_term_id;
    bool is_test;
    std::vector<BodyLocalFrame> frames;
    std::vector<SemanticRegion> regions;
    BodyFailureContext dead_failure_context;
    std::vector<BodyFailureContext> failure_contexts;
    std::vector<BodyCatchContext> catches;
    std::vector<BodyLoopContext> loops;
    std::vector<std::size_t> value_boundary_loop_depths;
    std::optional<LifetimeRegionID> active_full_expression;
    bool reachable;
    bool reference_path_reachable;
    bool reported_unreachable;
};

class BodyBatchElaborator final {
    friend class BodyElaborator;

public:
    BodyBatchElaborator(
        ProgramDraft& builder,
        AnalysisCatalogView catalog_view,
        ImportUsage& usage,
        ConstructionRequests& requests
    ) noexcept;
    auto run() noexcept -> AnalysisTask<void>;
    auto defer_constant_block(
        ProgramModuleID module,
        const ASTConstantBlock& source,
        BodyLocalNames locals = {}
    ) noexcept -> void;
    auto ensure_function_signature(FunctionID id, ProgramModuleID requester, Span span) noexcept
        -> AnalysisTask<void>;
    auto ensure_function_body(FunctionID id, ProgramModuleID requester, Span span) noexcept
        -> AnalysisTask<BodyID>;

    ProgramDraft* draft;
    AnalysisCatalogView catalog_data;
    ImportUsage* imports;
    ConstructionRequests& requests;

private:
    struct PendingConstantBlock final {
        ProgramModuleID module;
        ASTConstantBlock syntax;
        BodyLocalNames locals;
    };

    auto build_constant_block(PendingConstantBlock source) noexcept -> AnalysisTask<void>;

    struct Unvisited final {};

    struct Analyzing final {};

    struct Complete final {};

    struct Failed final {
        AnalysisFailure failure;
    };

    using State = std::variant<Unvisited, Analyzing, Complete, Failed>;
    auto complete_function(FunctionID id) noexcept -> AnalysisTask<void>;
    auto elaborate_function(FunctionID id) noexcept -> AnalysisTask<void>;
    std::vector<const CatalogSymbol*> functions;
    std::vector<State> states;
    std::vector<std::optional<BodyID>> body_ids;
    std::vector<FunctionID> active_path;
    std::vector<BodyID> constant_roots;
    std::vector<PendingConstantBlock> constant_blocks;
    std::map<std::pair<SourceID, Span>, Diagnostic> unused_locals;
    std::set<std::pair<SourceID, Span>> used_locals;
};
