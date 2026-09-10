module carven:semantic.analysis.body.context;

import :diagnostics.builder;
import :diagnostics.code;
import :frontend.ast.control;
import :frontend.ast.decl;
import :frontend.ast.expr;
import :frontend.ast.literal;
import :frontend.ast.pattern;
import :frontend.ast.stmt;
import :frontend.ast.storage;
import :frontend.ast.tree;
import :semantic.analysis.body.builder;
import :semantic.analysis.body.pipeline;
import :semantic.analysis.body.resolve;
import :semantic.analysis.constant.evaluate;
import :semantic.analysis.coverage;
import :semantic.analysis.expr.scope;
import :semantic.analysis.operations;
import :semantic.analysis.program;
import :semantic.analysis.types;
import :semantic.analysis.validation;
import :semantic.semir.decl;
import :semantic.semir.structured;
import :semantic.semir.type;
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
    bool used = false;
    bool takeable = true;
    BodyLocalRole role = BodyLocalRole::Local;
    std::optional<Span> unused_candidate;
};

struct BodyLocalFrame final {
    LifetimeRegionID lifetime;
    std::flat_map<std::string, BodyLocalStorage, std::less<>> names;
};

using BodyExpressionStorage = std::variant<SemanticExpression, PlaceExpression>;
using BodyPendingFailureTerms = std::vector<FailureTermID>;

struct BuiltExpression final {
    BodyExpressionStorage storage;
    BodyPendingFailureTerms pending_failures;
    bool takeable = true;
    bool completes = true;

    auto is_function_reference() const noexcept -> bool {
        const auto* expression = std::get_if<SemanticExpression>(&storage);
        return expression != nullptr && std::holds_alternative<SemCallable>(expression->value);
    }

    auto expression() const noexcept -> const SemanticExpression& {
        if (const auto* value = std::get_if<SemanticExpression>(&storage)) {
            return *value;
        }
        return std::get<PlaceExpression>(storage).expression;
    }

    auto type() const noexcept -> const ConstructionTypeRef& {
        return expression().type.construction();
    }

    auto constant() const noexcept -> std::optional<ConstantID> { return expression().constant; }
};

struct CppMemberSelection final {
    BuiltExpression receiver;
    std::string member;
};

struct CppSelection final {
    std::variant<CppNameReference, CppMemberSelection> target;
    Span span;
};

using SelectedExpression = std::variant<BuiltExpression, CppSelection>;

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
    bool has_break = false;
    bool has_continue = false;
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
    explicit BodyFullExpressionSuspension(std::optional<LifetimeRegionID>& active) noexcept
        : slot(std::addressof(active)),
          saved(std::exchange(active, std::nullopt)) {}

    BodyFullExpressionSuspension(const BodyFullExpressionSuspension&) = delete;
    BodyFullExpressionSuspension(BodyFullExpressionSuspension&&) = delete;
    auto operator=(const BodyFullExpressionSuspension&) -> BodyFullExpressionSuspension& = delete;
    auto operator=(BodyFullExpressionSuspension&&) -> BodyFullExpressionSuspension& = delete;

    ~BodyFullExpressionSuspension() noexcept { *slot = saved; }

private:
    std::optional<LifetimeRegionID>* slot;
    std::optional<LifetimeRegionID> saved;
};

class BodyReferencePathGuard final {
public:
    BodyReferencePathGuard(bool& active, bool path_is_reachable) noexcept
        : slot(std::addressof(active)),
          saved(std::exchange(active, active && path_is_reachable)) {}

    BodyReferencePathGuard(const BodyReferencePathGuard&) = delete;
    BodyReferencePathGuard(BodyReferencePathGuard&&) = delete;
    auto operator=(const BodyReferencePathGuard&) -> BodyReferencePathGuard& = delete;
    auto operator=(BodyReferencePathGuard&&) -> BodyReferencePathGuard& = delete;

    ~BodyReferencePathGuard() noexcept { *slot = saved; }

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
    const auto fact = draft.constant_copy(*constant);
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
    friend class BodyExpressionSite;

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
    auto run(const ASTCallableBody& source_body) noexcept -> AnalysisResult<StructuredBodyDraft>;

private:
    auto draft() const noexcept -> ProgramDraft&;
    auto catalog() const noexcept -> AnalysisCatalogView;
    auto import_usage() const noexcept -> ImportUsage&;
    auto origin(Span span) noexcept -> ProgramOriginID;
    auto expansion(Span span, ProgramExpansionReason reason) noexcept -> ProgramOriginID;
    auto spelling(Span span) const noexcept -> std::string;
    auto fail(Span span, DiagnosticCode code, std::string message) noexcept -> AnalysisFailure;
    auto warn(Span span, DiagnosticCode code, std::string message) noexcept -> void;

    auto resolve_type(ASTTypeID type) noexcept -> AnalysisResult<ConstructionTypeRef>;
    auto resolve_construction_type(const ASTConstructionType& type) noexcept
        -> AnalysisResult<ConstructionTypeRef>;
    auto resolve_array_extent(ASTExprID expression) noexcept -> AnalysisResult<std::uint64_t>;
    auto resolve_constant_name(std::string_view name, Span span) noexcept
        -> AnalysisResult<ResolvedConstantName>;
    auto resolve_enum_qualifier(ASTExprID expression) noexcept
        -> AnalysisResult<std::optional<TypeID>>;
    auto resolve_constant_enum_case(TypeID type, std::string_view name, Span span) noexcept
        -> AnalysisResult<ResolvedEnumCase>;
    auto compatible(ConstructionTypeRef left, ConstructionTypeRef right) const noexcept -> bool;
    auto require_writable_storage_type(
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
    ) noexcept -> AnalysisResult<SemanticRegion>;
    auto build_arm(
        const ASTMatchArmBody& source,
        bool value_form,
        std::optional<ConstructionTypeRef>& type,
        BodyPendingFailureTerms& pending,
        bool allow_pointer_narrowing
    ) noexcept -> AnalysisResult<SemanticRegion>;
    auto build_if(
        const ASTIfForm& source,
        Span span,
        std::optional<ConstructionTypeRef> expected,
        bool value_form,
        bool allow_pointer_narrowing = true
    ) noexcept -> AnalysisResult<BuiltExpression>;

    auto push_frame(Span span) noexcept -> void;
    auto pop_frame(bool diagnose = true) noexcept -> void;
    auto diagnose_unused(const BodyLocalFrame& frame) noexcept -> void;
    auto bind_local(Span name, BodyLocalStorage storage, DiagnosticCode duplicate_code) noexcept
        -> AnalysisResult<void>;
    auto find_local(std::string_view name) const noexcept -> const BodyLocalStorage*;
    auto use_local(std::string_view name) noexcept -> BodyLocalStorage*;
    auto find_global(std::string_view name, Span span) noexcept
        -> AnalysisResult<const CatalogSymbol*>;

    auto consume_value(BuiltExpression& expression, Span span, AccessMode access) noexcept
        -> AnalysisResult<SemanticExpression>;
    auto dereference_expression(const ASTPrefixExpr& source, Span span) noexcept
        -> AnalysisResult<BuiltExpression>;
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
    ) noexcept -> AnalysisResult<BuiltExpression>;
    auto select_expression(
        ASTExprID id,
        std::optional<ConstructionTypeRef> expected = std::nullopt,
        bool allow_pointer_narrowing = true
    ) noexcept -> AnalysisResult<SelectedExpression>;
    auto materialize_selection(SelectedExpression selected) noexcept
        -> AnalysisResult<BuiltExpression>;
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
    auto cpp_call(SelectedExpression callee, const ASTCallExpr& source, Span span) noexcept
        -> AnalysisResult<BuiltExpression>;
    auto select_cpp_name(const ASTCppNameExpr& name, Span span) noexcept
        -> AnalysisResult<SelectedExpression>;
    auto select_name(const ASTNameExpr& name, Span span) noexcept
        -> AnalysisResult<SelectedExpression>;
    auto array_expression(
        const ASTArrayExpr& array,
        Span span,
        std::optional<ConstructionTypeRef> expected,
        bool allow_pointer_narrowing = true
    ) noexcept -> AnalysisResult<BuiltExpression>;
    auto construction_expression(const ASTConstructionExpr& source, Span span) noexcept
        -> AnalysisResult<BuiltExpression>;
    auto access_expression(const ASTAccessExpr& source, Span span) noexcept
        -> AnalysisResult<BuiltExpression>;
    auto call_expression(
        const ASTCallExpr& source,
        Span span,
        std::optional<SelectedExpression> prepared_callee = std::nullopt
    ) noexcept -> AnalysisResult<BuiltExpression>;
    auto enum_case_reference(
        TypeID enumeration_type,
        std::string_view case_name,
        Span span,
        Span case_span
    ) noexcept -> AnalysisResult<BuiltExpression>;
    auto index_expression(const ASTIndexExpr& source, Span span) noexcept
        -> AnalysisResult<BuiltExpression>;
    auto select_member(const ASTMemberExpr& source, Span span, BuiltExpression operand) noexcept
        -> AnalysisResult<SelectedExpression>;
    auto propagation_expression(const ASTPropagationExpr& source, Span span) noexcept
        -> AnalysisResult<BuiltExpression>;
    auto conditional_expression(
        const ASTIfForm& source,
        Span span,
        std::optional<ConstructionTypeRef> expected,
        bool allow_pointer_narrowing = true
    ) noexcept -> AnalysisResult<BuiltExpression>;
    auto match_expression(
        const ASTMatchForm& source,
        Span span,
        std::optional<ConstructionTypeRef> expected,
        bool allow_pointer_narrowing = true
    ) noexcept -> AnalysisResult<BuiltExpression>;
    auto match_statement(const ASTMatchForm& source, Span span) noexcept -> AnalysisResult<void>;
    auto try_expression(
        const ASTTryForm& source,
        Span span,
        std::optional<ConstructionTypeRef> expected,
        bool allow_pointer_narrowing = true
    ) noexcept -> AnalysisResult<BuiltExpression>;
    auto try_statement(const ASTTryForm& source, Span span) noexcept -> AnalysisResult<void>;
    auto build_try(
        const ASTTryForm& source,
        Span span,
        std::optional<ConstructionTypeRef> expected,
        bool value_form,
        bool allow_pointer_narrowing = true
    ) noexcept -> AnalysisResult<std::optional<BuiltExpression>>;
    auto build_pattern(
        ASTPatternID source,
        ConstructionTypeRef type,
        std::flat_map<std::string, BodyPatternBindingStorage, std::less<>>& bindings,
        bool allow_new_bindings,
        std::flat_set<std::string, std::less<>>& used_bindings
    ) noexcept -> AnalysisResult<BuiltPattern>;
    auto resolve_pattern_constraint(const ASTConstraintOperand& operand) noexcept
        -> AnalysisResult<ConstructionTypeRef>;
    auto build_match(
        const ASTMatchForm& source,
        Span span,
        std::optional<ConstructionTypeRef> expected,
        bool value_form,
        bool allow_pointer_narrowing = true
    ) noexcept -> AnalysisResult<std::optional<BuiltExpression>>;
    auto lambda_expression(
        const ASTLambdaExpr& source,
        Span span,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<BuiltExpression>;

    auto statement(ASTStmtID id) noexcept -> AnalysisResult<void>;
    auto variable_statement(const ASTVariableDecl& source) noexcept -> AnalysisResult<void>;
    auto assignment_statement(const ASTAssignment& source) noexcept -> AnalysisResult<void>;
    auto update_statement(const ASTUpdate& source) noexcept -> AnalysisResult<void>;
    auto test_statement(const ASTTestOperationStmt& source, Span span) noexcept
        -> AnalysisResult<void>;
    auto transfer_statement(const ASTControlTransfer& source) noexcept -> AnalysisResult<void>;
    auto while_statement(const ASTWhileStmt& source, Span span) noexcept -> AnalysisResult<void>;
    auto for_statement(const ASTForStmt& source, Span span) noexcept -> AnalysisResult<void>;
    auto c_style_for_statement(
        const ASTForStmt& source,
        const ASTCStyleForHeader& header,
        Span span
    ) noexcept -> AnalysisResult<void>;
    auto range_for_statement(
        const ASTForStmt& source,
        const ASTRangeForHeader& header,
        Span span
    ) noexcept -> AnalysisResult<void>;
    auto if_statement(const ASTIfForm& source, Span span) noexcept -> AnalysisResult<void>;
    auto return_statement(
        std::optional<ASTExprID> operand,
        Span span,
        Span keyword_span,
        bool implicit
    ) noexcept -> AnalysisResult<void>;
    auto block(ASTBlockID id) noexcept -> AnalysisResult<void>;
    auto branch_block(
        ASTBranchBlockID id,
        bool consume_result,
        std::optional<ConstructionTypeRef> expected = std::nullopt,
        bool allow_pointer_narrowing = true
    ) noexcept -> AnalysisResult<std::optional<BuiltExpression>>;

    auto callable_contract(BuiltExpression& callee, Span span) noexcept
        -> AnalysisResult<ConstructionCallableContract>;
    auto build_call_argument(
        ASTExprID source,
        AccessMode access,
        std::optional<ConstructionTypeRef> expected
    ) noexcept -> AnalysisResult<BuiltCallArgument>;

    BodyBatchElaborator* batch;
    ProgramModuleID source_module_id;
    ModuleID semantic_module_id;
    ASTView ast;
    BodyBuilder body_builder;
    std::optional<ConstructionTypeRef> result_type;
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
public:
    BodyBatchElaborator(
        ProgramDraft& builder,
        AnalysisCatalogView catalog_view,
        ImportUsage& usage
    ) noexcept
        : draft(std::addressof(builder)),
          catalog_data(catalog_view),
          imports(std::addressof(usage)),
          functions(catalog_view.function_count()),
          states(catalog_view.function_count(), Unvisited {}) {
        for (const auto& symbol : catalog_view.symbols()) {
            if (const auto* function = std::get_if<CatalogFunctionForm>(&symbol.form)) {
                functions[function->function.index()] = std::addressof(symbol);
            }
        }
    }

    auto run() noexcept -> AnalysisResult<void>;

    auto ensure_function_signature(FunctionID id, ProgramModuleID requester, Span span) noexcept
        -> AnalysisResult<void>;

    ProgramDraft* draft;
    AnalysisCatalogView catalog_data;
    ImportUsage* imports;

private:
    struct Unvisited final {};

    struct Analyzing final {};

    struct Complete final {};

    struct Failed final {
        AnalysisFailure failure;
    };

    using State = std::variant<Unvisited, Analyzing, Complete, Failed>;
    auto complete_function(FunctionID id) noexcept -> AnalysisResult<void>;
    auto elaborate_function(FunctionID id) noexcept -> AnalysisResult<void>;
    std::vector<const CatalogSymbol*> functions;
    std::vector<State> states;
    std::vector<FunctionID> active_path;
};
