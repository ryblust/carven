module carven:semantic.analysis.validation.context;

import :semantic.analysis.failures;
import :semantic.analysis.session;
import :semantic.analysis.session.read;
import :semantic.analysis.validation.invariants;
import :semantic.hir.constant;
import :semantic.hir.decl;
import :semantic.hir.expr;
import :semantic.hir.pattern;
import :semantic.hir.place;
import :semantic.hir.stmt;
import :semantic.hir.symbol;
import :semantic.hir.type;
import :support.visit;
import std;

template<typename ID>
auto semantic_id_known(ID id, std::size_t size) noexcept -> bool {
    return id.index() < size;
}

auto validate_ownership(SemanticProgramView program) noexcept
    -> std::expected<void, SemanticProgramError>;
auto validate_ownership(SemanticDraftView program) noexcept
    -> std::expected<void, SemanticProgramError>;
auto validate_tables(SemanticProgramView program) noexcept
    -> std::expected<void, SemanticProgramError>;
auto validate_tables(SemanticDraftView program) noexcept
    -> std::expected<void, SemanticProgramError>;
auto validate_flow(SemanticProgramView program) noexcept
    -> std::expected<void, SemanticProgramError>;
auto validate_flow(SemanticDraftView program) noexcept -> std::expected<void, SemanticProgramError>;
auto validate_relations(SemanticProgramView program) noexcept
    -> std::expected<void, SemanticProgramError>;
auto validate_relations(SemanticDraftView program) noexcept
    -> std::expected<void, SemanticProgramError>;

struct SemanticCallableContract final {
    std::span<const HIRFunctionParameterType> parameters;
    HIRTypeID result;
    FailureSetID failure_set;
};

template<typename Program>
class SemanticValidationInput final {
public:
    explicit SemanticValidationInput(Program source) noexcept
        : source(source) {}

    auto program() const noexcept -> Program { return source; }

private:
    Program source;
};

template<typename Program>
class SemanticVerifier final {
    SemanticValidationInput<Program> input;
    std::optional<SemanticProgramError> failure;

public:
    explicit SemanticVerifier(Program source) noexcept;

    auto check_tables() noexcept -> std::expected<void, SemanticProgramError>;
    auto check_flow() noexcept -> std::expected<void, SemanticProgramError>;
    auto check_relations() noexcept -> std::expected<void, SemanticProgramError>;

private:
    auto fail(SemanticProgramErrorKind kind, std::string message) noexcept -> bool;

    auto type_known(HIRTypeID id) const noexcept -> bool;

    auto symbol_known(SymbolID id) const noexcept -> bool;

    auto scope_known(SemanticScopeID id) const noexcept -> bool;

    auto scope_contains(SemanticScopeID owner, SemanticScopeID nested) const noexcept -> bool;

    auto failure_members(std::span<const HIRTypeID> values) noexcept -> bool;

    auto failure_set_known(FailureSetID id) noexcept -> bool;

    auto verify_type_value(const HIRTypeValue& value) noexcept -> bool;

    template<typename Callable>
    auto verify_callable_shape(const Callable& callable) noexcept -> bool;

    auto verify_callable_signature(const HIRCallableSignature& callable) noexcept -> bool;

    auto verify_constant(std::size_t index, const HIRConstantFact& fact) noexcept -> bool;

    auto verify_scope_table() noexcept -> bool;

    auto verify_symbol_table() noexcept -> bool;

    auto nominal_index(HIRNominalDeclRef declaration) const noexcept -> std::optional<std::size_t>;

    auto verify_nominal_containment() noexcept -> bool;

    auto verify_canonical_tables() noexcept -> bool;

    auto binding_place(
        const HIRBindingTarget& target,
        HIRTypeID type,
        SemanticScopeID scope
    ) noexcept -> bool;

    auto projected_type(const SemanticPlaceUse& use) noexcept -> std::optional<HIRTypeID>;

    auto verify_place_use(
        const HIRExpr& expression,
        const std::optional<SemanticPlaceUse>& place_use,
        SemanticScopeID scope
    ) noexcept -> bool;

    auto verify_failure_summary(const HIRExpressionControl& control) noexcept -> bool;

    auto verify_effect_roots(std::span<const SymbolID> roots) noexcept -> bool;

    auto verify_evaluation_effect(const EvaluationEffect& effect) noexcept -> bool;

    auto verify_failure_summary(const HIRBlockControl& control) noexcept -> bool;

    auto visit_pattern(HIRPatternID id, SemanticScopeID scope, HIRTypeID expected) noexcept -> bool;

    auto verify_body(
        BodyID id,
        std::optional<CallableID> callable_id,
        SemanticScopeID enclosing
    ) noexcept -> bool;

    auto visit_function(FunctionID id, ProgramModuleID owner) noexcept -> bool;

    auto visit_structure(StructID id, ProgramModuleID owner) noexcept -> bool;

    auto visit_enumeration(EnumID id, ProgramModuleID owner) noexcept -> bool;

    auto visit_modules() noexcept -> bool;

    auto visit_match_arm(
        const HIRMatchArm& arm,
        HIRTypeID subject,
        SemanticScopeID owner_scope
    ) noexcept -> bool;

    auto visit_catch_arm(
        const HIRCatchArm& arm,
        const HIRCatchFacts& facts,
        std::span<const HIRTypeID> protected_failures,
        SemanticScopeID owner_scope
    ) noexcept -> bool;

    auto expression_callable(HIRTypeID type) const noexcept
        -> std::optional<SemanticCallableContract>;

    auto verify_call(
        const HIRExpr& expression,
        const HIRExpressionControl& control,
        const HIRCallExpr& call,
        SemanticScopeID scope
    ) noexcept -> bool;

    auto visit_expression(HIRExprID id, SemanticScopeID scope) noexcept -> bool;

    auto visit_statement(HIRStmtID id, SemanticScopeID scope) noexcept -> bool;

    auto visit_block(HIRBlockID id, SemanticScopeID owner_scope) noexcept -> bool;
};
