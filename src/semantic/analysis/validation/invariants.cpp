module carven:semantic.analysis.validation.invariants.impl;

import :semantic.analysis.session;
import :semantic.analysis.failures;
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

namespace {

template<typename ID>
auto known(ID id, std::size_t size) noexcept -> bool {
    return id.index() < size;
}

auto match_coverage_aligned(
    std::span<const HIRMatchArm> arms,
    const HIRMatchCoverageFacts& coverage
) noexcept -> bool {
    return arms.size() == coverage.arm_states.size();
}

class StructureVerifier final {
    SemanticDraftView program;
    std::vector<std::uint8_t> function_claims;
    std::vector<std::uint8_t> test_claims;
    std::vector<std::uint8_t> callable_claims;
    std::vector<std::uint8_t> body_claims;
    std::vector<BodyID> body_queue;
    std::vector<std::uint8_t> expression_states;
    std::vector<std::uint8_t> statement_states;
    std::vector<std::uint8_t> pattern_states;
    std::vector<std::uint8_t> block_states;
    std::vector<std::optional<BodyID>> body_root_scopes;
    std::vector<std::pair<SemanticScopeID, BodyID>> body_scope_uses;
    std::vector<std::pair<BodyID, SemanticScopeID>> closure_enclosings;
    std::optional<BodyID> active_body;
    std::optional<SemanticScopeID> active_scope;
    std::optional<SemanticProgramError> failure;

public:
    explicit StructureVerifier(SemanticDraftView source) noexcept
        : program(source),
          function_claims(program.functions().size()),
          test_claims(program.tests().size()),
          callable_claims(program.callables().size()),
          body_claims(program.bodies().size()),
          expression_states(program.expressions().size()),
          statement_states(program.statements().size()),
          pattern_states(program.patterns().size()),
          block_states(program.blocks().size()),
          body_root_scopes(program.scopes().size()) {}

    auto run() noexcept -> std::expected<void, SemanticProgramError> {
        for (auto index = 0uz; index < program.scopes().size(); ++index) {
            const auto parent = program.scopes()[index].parent;
            if (parent.has_value() && parent->index() >= index) {
                static_cast<void>(fail(
                    SemanticProgramErrorKind::InvalidScope,
                    "lexical scope parent is cyclic or not construction-ordered"
                ));
                return std::unexpected(std::move(*failure));
            }
        }
        for (const auto& hir_module : program.modules()) {
            for (const auto& item : hir_module.items) {
                const auto valid = std::visit(
                    Overloaded {
                        [&](FunctionID id) noexcept {
                            return claim(function_claims, id, "function")
                                && claim_callable(program.function(id).callable);
                        },
                        [](StructID) static noexcept { return true; },
                        [](EnumID) static noexcept { return true; },
                        [&](TestID id) noexcept {
                            return claim(test_claims, id, "test")
                                && claim_body(program.test(id).body);
                        },
                        [](const HIRCppRegion&) static noexcept { return true; },
                    },
                    item
                );
                if (!valid) {
                    return std::unexpected(std::move(*failure));
                }
            }
        }
        for (auto cursor = 0uz; cursor < body_queue.size(); ++cursor) {
            const auto body_id = body_queue[cursor];
            const auto& body = program.body(body_id);
            if (!known(body.scope, program.scopes().size())) {
                static_cast<void>(fail(
                    SemanticProgramErrorKind::InvalidScope,
                    "semantic body has an invalid structural root"
                ));
                return std::unexpected(std::move(*failure));
            }
            auto& root_owner = body_root_scopes[body.scope.index()];
            if (root_owner.has_value()) {
                static_cast<void>(fail(
                    SemanticProgramErrorKind::InvalidOwnership,
                    "semantic bodies share one root scope"
                ));
                return std::unexpected(std::move(*failure));
            }
            root_owner = body_id;
            active_body = body_id;
            active_scope = body.scope;
            body_scope_uses.emplace_back(body.scope, body_id);
            if (!visit_block(body.root)) {
                if (!failure.has_value()) {
                    static_cast<void>(fail(
                        SemanticProgramErrorKind::InvalidScope,
                        "semantic body has an invalid structural root"
                    ));
                }
                return std::unexpected(std::move(*failure));
            }
        }
        active_body.reset();
        active_scope.reset();
        if (!verify_body_scope_ownership()) {
            return std::unexpected(std::move(*failure));
        }
        if (!every_owned_once(function_claims, "function")
            || !every_owned_once(test_claims, "test")
            || !every_owned_once(callable_claims, "callable")
            || !every_owned_once(body_claims, "body")
            || !every_owned_once(expression_states, "expression")
            || !every_owned_once(statement_states, "statement")
            || !every_owned_once(pattern_states, "pattern")
            || !every_owned_once(block_states, "block")) {
            return std::unexpected(std::move(*failure));
        }
        return {};
    }

private:
    auto fail(SemanticProgramErrorKind kind, std::string message) noexcept -> bool {
        if (!failure.has_value()) {
            failure = SemanticProgramError {.kind = kind, .message = std::move(message)};
        }
        return false;
    }

    template<typename ID>
    auto begin_occurrence(ID id, std::vector<std::uint8_t>& states, std::string_view kind) noexcept
        -> bool {
        if (!known(id, states.size())) {
            return fail(
                SemanticProgramErrorKind::InvalidReference,
                std::format("semantic {} has an invalid identity", kind)
            );
        }
        auto& state = states[id.index()];
        if (state == 1) {
            return fail(
                SemanticProgramErrorKind::InvalidOwnership,
                std::format("semantic {} participates in a structural cycle", kind)
            );
        }
        if (state == 2) {
            return fail(
                SemanticProgramErrorKind::InvalidOwnership,
                std::format("semantic {} belongs to more than one body position", kind)
            );
        }
        state = 1;
        return true;
    }

    template<typename ID>
    auto finish_occurrence(ID id, std::vector<std::uint8_t>& states) noexcept -> void {
        states[id.index()] = 2;
    }

    auto claim_body(BodyID id) noexcept -> bool {
        if (!program.has_body(id)) {
            return fail(
                SemanticProgramErrorKind::InvalidReference,
                "callable or test references an incomplete semantic body"
            );
        }
        auto& claims = body_claims[id.index()];
        if (claims != 0) {
            return fail(
                SemanticProgramErrorKind::InvalidOwnership,
                "semantic body is claimed by more than one callable or test"
            );
        }
        claims = 2;
        body_queue.push_back(id);
        return true;
    }

    template<typename ID>
    auto claim(std::vector<std::uint8_t>& claims, ID id, std::string_view kind) noexcept -> bool {
        if (!known(id, claims.size())) {
            return fail(
                SemanticProgramErrorKind::InvalidReference,
                std::format("module references an unknown semantic {}", kind)
            );
        }
        if (claims[id.index()] != 0) {
            return fail(
                SemanticProgramErrorKind::InvalidOwnership,
                std::format("semantic {} has more than one structural owner", kind)
            );
        }
        claims[id.index()] = 2;
        return true;
    }

    auto claim_callable(CallableID id) noexcept -> bool {
        return claim(callable_claims, id, "callable") && claim_body(program.callable(id).body);
    }

    auto claim_closure_callable(CallableID id) noexcept -> bool {
        if (!active_scope.has_value() || !claim_callable(id)) {
            return false;
        }
        closure_enclosings.emplace_back(program.callable(id).body, *active_scope);
        return true;
    }

    auto record_body_scope(SemanticScopeID scope) noexcept -> bool {
        if (!known(scope, program.scopes().size()) || !active_body.has_value()) {
            return fail(
                SemanticProgramErrorKind::InvalidScope,
                "semantic body references an invalid lexical scope"
            );
        }
        body_scope_uses.emplace_back(scope, *active_body);
        return true;
    }

    auto verify_body_scope_ownership() noexcept -> bool {
        auto owners = std::vector<std::optional<BodyID>>(program.scopes().size());
        for (auto index = 0uz; index < program.scopes().size(); ++index) {
            const auto& scope = program.scopes()[index];
            if (scope.parent.has_value()) {
                owners[index] = owners[scope.parent->index()];
            }
            if (body_root_scopes[index].has_value()) {
                owners[index] = body_root_scopes[index];
            }
        }
        for (const auto& [scope, body] : body_scope_uses) {
            if (owners[scope.index()] != body) {
                return fail(
                    SemanticProgramErrorKind::InvalidScope,
                    "block, arm, or loop scope belongs to another semantic body"
                );
            }
        }
        for (const auto& [body, enclosing] : closure_enclosings) {
            const auto root = program.body(body).scope;
            if (root == enclosing) {
                return fail(
                    SemanticProgramErrorKind::InvalidScope,
                    "closure body root is not nested below its enclosing scope"
                );
            }
            auto current = std::optional(root);
            auto descendant = false;
            while (current.has_value()) {
                if (*current == enclosing) {
                    descendant = true;
                    break;
                }
                current = program.scope(*current).parent;
            }
            if (!descendant) {
                return fail(
                    SemanticProgramErrorKind::InvalidScope,
                    "closure body root is outside its enclosing scope"
                );
            }
        }
        return true;
    }

    auto every_owned_once(std::span<const std::uint8_t> states, std::string_view kind) noexcept
        -> bool {
        return std::ranges::all_of(
                   states,
                   [](std::uint8_t state) static noexcept { return state == 2; }
               )
            || fail(
                   SemanticProgramErrorKind::InvalidOwnership,
                   std::format("semantic {} has no structural owner", kind)
            );
    }

    auto visit_pattern(HIRPatternID id) noexcept -> bool {
        if (!begin_occurrence(id, pattern_states, "pattern")) {
            return false;
        }
        const auto valid = std::visit(
            Overloaded {
                [](const HIRWildcardPattern&) static noexcept { return true; },
                [](const HIRLiteralPattern&) static noexcept { return true; },
                [&](const HIROrPattern& pattern) noexcept {
                    return std::ranges::all_of(pattern.alternatives, [&](HIRPatternID child) {
                        return visit_pattern(child);
                    });
                },
                [](const HIRTypeConstraintPattern&) static noexcept { return true; },
                [](const HIRBindingPattern&) static noexcept { return true; },
                [&](const HIRCasePattern& pattern) noexcept {
                    return std::ranges::all_of(pattern.payload, [&](HIRPatternID child) {
                        return visit_pattern(child);
                    });
                },
            },
            program.pattern(id).value
        );
        if (valid) {
            finish_occurrence(id, pattern_states);
        }
        return valid;
    }

    auto visit_match_arm(const HIRMatchArm& arm) noexcept -> bool {
        if (!record_body_scope(arm.scope)) {
            return false;
        }
        const auto previous = active_scope;
        active_scope = arm.scope;
        const auto valid = visit_pattern(arm.pattern)
            && (!arm.guard.has_value() || visit_expression(*arm.guard))
            && visit_block(arm.body);
        active_scope = previous;
        return valid;
    }

    auto visit_catch_arm(const HIRCatchArm& arm) noexcept -> bool {
        if (!record_body_scope(arm.scope)) {
            return false;
        }
        const auto previous = active_scope;
        active_scope = arm.scope;
        for (const auto& alternative : arm.alternatives) {
            if (alternative.inner.has_value() && !visit_pattern(*alternative.inner)) {
                return false;
            }
        }
        const auto valid =
            (!arm.guard.has_value() || visit_expression(*arm.guard)) && visit_block(arm.body);
        active_scope = previous;
        return valid;
    }

    auto visit_expression(HIRExprID id) noexcept -> bool {
        if (!begin_occurrence(id, expression_states, "expression")) {
            return false;
        }
        const auto valid = std::visit(
            Overloaded {
                [](const HIRLiteralExpr&) static noexcept { return true; },
                [](const HIRNameExpr&) static noexcept { return true; },
                [&](const HIRArrayExpr& value) noexcept {
                    return std::ranges::all_of(value.element_ids, [&](HIRExprID child) {
                        return visit_expression(child);
                    });
                },
                [&](const HIRConstructionExpr& value) noexcept {
                    return std::ranges::all_of(value.fields, [&](const auto& field) {
                        return visit_expression(field.value);
                    });
                },
                [&](const HIRCaseConstructionExpr& value) noexcept {
                    return std::ranges::all_of(value.payload, [&](HIRExprID child) {
                        return visit_expression(child);
                    });
                },
                [&](const HIRUnaryExpr& value) noexcept {
                    return visit_expression(value.operand_id);
                },
                [&](const HIRBinaryExpr& value) noexcept {
                    return visit_expression(value.left) && visit_expression(value.right);
                },
                [&](const HIRCastExpr& value) noexcept {
                    return visit_expression(value.operand_id);
                },
                [&](const HIRCallExpr& value) noexcept {
                    if (!visit_expression(value.callee)) {
                        return false;
                    }
                    return std::ranges::all_of(value.arguments, [&](const auto& argument) {
                        return visit_expression(argument.expression);
                    });
                },
                [&](const HIRClosureExpr& value) noexcept {
                    return claim_closure_callable(value.callable);
                },
                [&](const HIRCallableViewExpr& value) noexcept {
                    return visit_expression(value.source);
                },
                [&](const HIRPropagationExpr& value) noexcept {
                    return visit_expression(value.operand_id);
                },
                [&](const HIRTakeExpr& value) noexcept {
                    return visit_expression(value.operand_id);
                },
                [&](const HIRTextIntrinsicExpr& value) noexcept {
                    return visit_expression(value.operand_id);
                },
                [&](const HIRIndexExpr& value) noexcept {
                    return visit_expression(value.operand_id) && visit_expression(value.index);
                },
                [&](const HIRMemberExpr& value) noexcept {
                    return visit_expression(value.operand_id);
                },
                [&](const HIRIfExpr& value) noexcept {
                    for (const auto& branch : value.branches) {
                        if (!visit_expression(branch.condition) || !visit_block(branch.body)) {
                            return false;
                        }
                    }
                    return !value.else_branch.has_value() || visit_block(*value.else_branch);
                },
                [&](const HIRMatchExpr& value) noexcept {
                    if (!match_coverage_aligned(value.arms, value.coverage)) {
                        return fail(
                            SemanticProgramErrorKind::InvalidContract,
                            "match coverage facts do not align with expression arms"
                        );
                    }
                    return visit_expression(value.subject)
                        && std::ranges::all_of(value.arms, [&](const auto& arm) {
                               return visit_match_arm(arm);
                           });
                },
                [&](const HIRTryExpr& value) noexcept {
                    return visit_block(value.body)
                        && std::ranges::all_of(value.arms, [&](const auto& arm) {
                               return visit_catch_arm(arm);
                           });
                },
                [](const HIRCppExpr&) static noexcept { return true; },
            },
            program.expression(id).value
        );
        if (valid) {
            finish_occurrence(id, expression_states);
        }
        return valid;
    }

    auto visit_statement(HIRStmtID id) noexcept -> bool {
        if (!begin_occurrence(id, statement_states, "statement")) {
            return false;
        }
        const auto valid = std::visit(
            Overloaded {
                [&](const HIRReturnStmt& value) noexcept {
                    return !value.value.has_value() || visit_expression(*value.value);
                },
                [](const HIRBreakStmt&) static noexcept { return true; },
                [](const HIRContinueStmt&) static noexcept { return true; },
                [&](const HIRThrowStmt& value) noexcept { return visit_expression(value.value); },
                [](const HIRRethrowStmt&) static noexcept { return true; },
                [&](const HIRExprStmt& value) noexcept {
                    return visit_expression(value.expression);
                },
                [&](const HIRBindingStmt& value) noexcept {
                    return visit_expression(value.initializer);
                },
                [&](const HIRAssignmentStmt& value) noexcept {
                    return visit_expression(value.target) && visit_expression(value.value);
                },
                [&](const HIRUpdateStmt& value) noexcept { return visit_expression(value.target); },
                [&](const HIRIfStmt& value) noexcept {
                    for (const auto& branch : value.branches) {
                        if (!visit_expression(branch.condition) || !visit_block(branch.body)) {
                            return false;
                        }
                    }
                    return !value.else_branch.has_value() || visit_block(*value.else_branch);
                },
                [&](const HIRMatchStmt& value) noexcept {
                    if (!match_coverage_aligned(value.arms, value.coverage)) {
                        return fail(
                            SemanticProgramErrorKind::InvalidContract,
                            "match coverage facts do not align with statement arms"
                        );
                    }
                    return visit_expression(value.subject)
                        && std::ranges::all_of(value.arms, [&](const auto& arm) {
                               return visit_match_arm(arm);
                           });
                },
                [&](const HIRWhileStmt& value) noexcept {
                    return visit_expression(value.condition) && visit_block(value.body);
                },
                [&](const HIRCStyleForStmt& value) noexcept {
                    if (!record_body_scope(value.scope)) {
                        return false;
                    }
                    const auto previous = active_scope;
                    active_scope = value.scope;
                    if (value.initializer.has_value() && !visit_statement(*value.initializer)) {
                        return false;
                    }
                    if (value.condition.has_value() && !visit_expression(*value.condition)) {
                        return false;
                    }
                    const auto valid = std::ranges::all_of(
                                           value.steps,
                                           [&](HIRStmtID step) { return visit_statement(step); }
                                       )
                        && visit_block(value.body);
                    active_scope = previous;
                    return valid;
                },
                [&](const HIRRangeForStmt& value) noexcept {
                    if (!record_body_scope(value.scope)) {
                        return false;
                    }
                    const auto previous = active_scope;
                    active_scope = value.scope;
                    const auto iterable = std::visit(
                        Overloaded {
                            [&](HIRExprID expression) noexcept {
                                return visit_expression(expression);
                            },
                            [&](const HIRHalfOpenRange& range) noexcept {
                                return visit_expression(range.begin) && visit_expression(range.end);
                            },
                        },
                        value.iterable
                    );
                    const auto valid = iterable && visit_block(value.body);
                    active_scope = previous;
                    return valid;
                },
                [&](const HIRTestCheckStmt& value) noexcept {
                    return visit_expression(value.condition)
                        && (!value.message.has_value() || visit_expression(*value.message));
                },
                [&](const HIRTestRequireStmt& value) noexcept {
                    return visit_expression(value.condition)
                        && (!value.message.has_value() || visit_expression(*value.message));
                },
                [&](const HIRTestFailStmt& value) noexcept {
                    return !value.message.has_value() || visit_expression(*value.message);
                },
                [](const HIRCppStmt&) static noexcept { return true; },
            },
            program.statement(id).value
        );
        if (valid) {
            finish_occurrence(id, statement_states);
        }
        return valid;
    }

    auto visit_block(HIRBlockID id) noexcept -> bool {
        if (!begin_occurrence(id, block_states, "block")) {
            return false;
        }
        const auto& block = program.block(id);
        if (!record_body_scope(block.scope)) {
            return fail(
                SemanticProgramErrorKind::InvalidScope,
                "semantic block references an invalid scope"
            );
        }
        const auto previous = active_scope;
        active_scope = block.scope;
        for (const auto statement : block.statements) {
            if (!visit_statement(statement)) {
                return false;
            }
        }
        if (block.result.has_value() && !visit_expression(*block.result)) {
            return false;
        }
        finish_occurrence(id, block_states);
        active_scope = previous;
        return true;
    }
};

template<typename Program>
class SemanticVerifier final {
    Program program;
    std::vector<std::uint8_t> function_owners;
    std::vector<std::uint8_t> structure_owners;
    std::vector<std::uint8_t> enumeration_owners;
    std::vector<std::uint8_t> enum_case_owners;
    std::vector<std::uint8_t> test_owners;
    std::vector<std::uint8_t> body_owners;
    std::vector<std::uint8_t> expression_states;
    std::vector<std::uint8_t> statement_states;
    std::vector<std::uint8_t> pattern_states;
    std::vector<std::uint8_t> block_states;
    std::optional<SemanticProgramError> failure;

public:
    explicit SemanticVerifier(Program source) noexcept
        : program(source),
          function_owners(program.functions().size()),
          structure_owners(program.structures().size()),
          enumeration_owners(program.enumerations().size()),
          enum_case_owners(program.enum_cases().size()),
          test_owners(program.tests().size()),
          body_owners(program.bodies().size()),
          expression_states(program.expressions().size()),
          statement_states(program.statements().size()),
          pattern_states(program.patterns().size()),
          block_states(program.blocks().size()) {}

    auto run() noexcept -> std::expected<void, SemanticProgramError> {
        if (program.expression_controls().size() != program.expressions().size()
            || program.evaluation_effects().size() != program.expressions().size()
            || program.place_uses().size() != program.expressions().size()
            || program.try_facts().size() != program.expressions().size()
            || program.block_controls().size() != program.blocks().size()
            || program.callable_flows().size() != program.callables().size()
            || program.structure_capabilities().size() != program.structures().size()
            || program.enumeration_capabilities().size() != program.enumerations().size()
            || program.nominal_dependency_sets().size()
                != program.structures().size() + program.enumerations().size()) {
            static_cast<void>(fail(
                SemanticProgramErrorKind::InvalidContract,
                "semantic flow facts are incomplete"
            ));
        } else {
            static_cast<void>(
                verify_canonical_tables() && visit_modules() && verify_all_occurrences()
            );
        }
        if (failure.has_value()) {
            return std::unexpected(std::move(*failure));
        }
        return {};
    }

private:
    auto fail(SemanticProgramErrorKind kind, std::string message) noexcept -> bool {
        if (!failure.has_value()) {
            failure = SemanticProgramError {.kind = kind, .message = std::move(message)};
        }
        return false;
    }

    auto type_known(HIRTypeID id) const noexcept -> bool {
        return known(id, program.types().size());
    }

    auto symbol_known(SymbolID id) const noexcept -> bool {
        return known(id, program.symbol_count());
    }

    auto scope_known(SemanticScopeID id) const noexcept -> bool {
        return known(id, program.scopes().size());
    }

    auto scope_contains(SemanticScopeID owner, SemanticScopeID nested) const noexcept -> bool {
        if (!scope_known(owner) || !scope_known(nested)) {
            return false;
        }
        auto current = nested;
        while (true) {
            if (current == owner) {
                return true;
            }
            const auto parent = program.scope(current).parent;
            if (!parent.has_value()) {
                return false;
            }
            current = *parent;
        }
    }

    auto failure_members(std::span<const HIRTypeID> values) noexcept -> bool {
        const auto original = std::vector(values.begin(), values.end());
        if (normalize_failure_members(original) != original) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "failure set storage is not normalized"
            );
        }
        for (const auto type : values) {
            if (!type_known(type)) {
                return fail(
                    SemanticProgramErrorKind::InvalidType,
                    "failure contract references an unknown type"
                );
            }
            const auto& value = program.type(type).value;
            if (!std::holds_alternative<HIRStructTypeValue>(value)
                && !std::holds_alternative<HIREnumTypeValue>(value)) {
                return fail(
                    SemanticProgramErrorKind::InvalidContract,
                    "failure contract contains a non-nominal or unknown type"
                );
            }
        }
        return true;
    }

    auto failure_set_known(FailureSetID id) noexcept -> bool {
        return known(id, program.failure_sets().size())
            || fail(
                   SemanticProgramErrorKind::InvalidContract,
                   "published fact references an unknown failure set"
            );
    }

    auto verify_type_value(const HIRTypeValue& value) noexcept -> bool {
        return std::visit(
            Overloaded {
                [](const HIRBuiltinTypeValue&) static noexcept { return true; },
                [&](const HIRStructTypeValue& nominal) noexcept {
                    return known(nominal.structure, program.structures().size())
                        || fail(
                               SemanticProgramErrorKind::InvalidType,
                               "structure type references an unknown declaration"
                        );
                },
                [&](const HIREnumTypeValue& nominal) noexcept {
                    return known(nominal.enumeration, program.enumerations().size())
                        || fail(
                               SemanticProgramErrorKind::InvalidType,
                               "enum type references an unknown declaration"
                        );
                },
                [&](const HIRArrayTypeValue& array) noexcept {
                    return type_known(array.element_type_id)
                        || fail(
                               SemanticProgramErrorKind::InvalidType,
                               "array type references an unknown element type"
                        );
                },
                [&](const HIRFunctionTypeValue& function) noexcept {
                    return known(function.callable, program.callables().size())
                        || fail(
                               SemanticProgramErrorKind::InvalidType,
                               "function type references an unknown callable"
                        );
                },
                [&](const HIRFunctionRefTypeValue& function) noexcept {
                    return known(function.signature, program.callable_signatures().size())
                        || fail(
                               SemanticProgramErrorKind::InvalidType,
                               "function reference type has an unknown signature"
                        );
                },
                [&](const HIRClosureTypeValue& closure) noexcept {
                    return known(closure.callable, program.callables().size())
                        || fail(
                               SemanticProgramErrorKind::InvalidType,
                               "closure type references an unknown callable"
                        );
                },
                [](const HIRForeignTypeValue&) static noexcept { return true; },
                [&](const HIRErrorTypeValue&) noexcept {
                    return fail(
                        SemanticProgramErrorKind::InvalidType,
                        "published semantic program contains an error type"
                    );
                },
            },
            value
        );
    }

    template<typename Callable>
    auto verify_callable_shape(const Callable& callable) noexcept -> bool {
        if (!type_known(callable.result)) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "callable contract references an unknown result type"
            );
        }
        for (const auto& parameter : callable.parameters) {
            if (!type_known(parameter.type)) {
                return fail(
                    SemanticProgramErrorKind::InvalidContract,
                    "callable parameter references an unknown type"
                );
            }
        }
        return true;
    }

    auto verify_callable_signature(const HIRCallableSignature& callable) noexcept -> bool {
        return verify_callable_shape(callable)
            && (failure_set_known(callable.failure_set)
                || fail(
                    SemanticProgramErrorKind::InvalidContract,
                    "callable signature references an unknown failure set"
                ));
    }

    auto verify_constant(std::size_t index, const HIRConstantFact& fact) noexcept -> bool {
        if (!type_known(fact.type)) {
            return fail(
                SemanticProgramErrorKind::InvalidReference,
                "program constant references an unknown type"
            );
        }
        return std::visit(
            Overloaded {
                [&](const HIRNumericEnumConstant& value) noexcept {
                    return known(value.enum_case, program.enum_cases().size())
                        || fail(
                               SemanticProgramErrorKind::InvalidReference,
                               "enum constant references an unknown case"
                        );
                },
                [&](const HIRPayloadEnumConstant& value) noexcept {
                    if (!known(value.enum_case, program.enum_cases().size())) {
                        return fail(
                            SemanticProgramErrorKind::InvalidReference,
                            "payload enum constant references an unknown case"
                        );
                    }
                    for (const auto payload : value.payload) {
                        if (payload.index() >= index) {
                            return fail(
                                SemanticProgramErrorKind::InvalidOwnership,
                                "constant payload is cyclic or not construction-ordered"
                            );
                        }
                    }
                    return true;
                },
                [](const auto&) static noexcept { return true; },
            },
            fact.value
        );
    }

    auto verify_scope_table() noexcept -> bool {
        for (auto index = 0uz; index < program.scopes().size(); ++index) {
            const auto parent = program.scopes()[index].parent;
            if (parent.has_value() && parent->index() >= index) {
                return fail(
                    SemanticProgramErrorKind::InvalidScope,
                    "lexical scope parent is cyclic or not construction-ordered"
                );
            }
        }
        return true;
    }

    auto verify_symbol_table() noexcept -> bool {
        if (program.bindings().size() != program.symbol_count()) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "semantic binding facts are not aligned with symbols"
            );
        }
        auto positions = std::flat_set<std::pair<SemanticScopeID, std::uint32_t>>();
        for (auto index = 0uz; index < program.symbol_count(); ++index) {
            const auto id = SymbolID::from_index(static_cast<std::uint32_t>(index));
            const auto& symbol = program.symbol(id);
            if ((symbol.type.has_value() && !type_known(*symbol.type))
                || (symbol.module_id.has_value()
                    && !known(*symbol.module_id, program.modules().size()))
                || (symbol.parent.has_value() && !symbol_known(*symbol.parent))) {
                return fail(
                    SemanticProgramErrorKind::InvalidReference,
                    "program symbol contains an invalid reference"
                );
            }
            const auto& binding = program.binding(id);
            if (!binding.has_value()) {
                continue;
            }
            if (!symbol.type.has_value()
                || !scope_known(binding->scope)
                || !positions.emplace(binding->scope, binding->declaration_order).second) {
                return fail(
                    SemanticProgramErrorKind::InvalidPlace,
                    "runtime binding has an invalid type, scope, or declaration order"
                );
            }
            if (binding->storage != SemanticBindingStorage::Owner && binding->capabilities.take) {
                return fail(
                    SemanticProgramErrorKind::InvalidPlace,
                    "borrowed or compiler storage cannot support Take"
                );
            }
            if (binding->storage == SemanticBindingStorage::Owner && !binding->capabilities.take) {
                return fail(
                    SemanticProgramErrorKind::InvalidPlace,
                    "owner storage must support whole-binding Take"
                );
            }
            if (binding->storage == SemanticBindingStorage::Compiler
                && binding->capabilities.write) {
                return fail(
                    SemanticProgramErrorKind::InvalidPlace,
                    "immutable compiler storage cannot support Write"
                );
            }
        }
        return true;
    }

    auto nominal_index(HIRNominalDeclRef declaration) const noexcept -> std::optional<std::size_t> {
        return std::visit(
            Overloaded {
                [&](StructID id) noexcept -> std::optional<std::size_t> {
                    return known(id, program.structures().size())
                        ? std::optional<std::size_t>(id.index())
                        : std::nullopt;
                },
                [&](EnumID id) noexcept -> std::optional<std::size_t> {
                    return known(id, program.enumerations().size())
                        ? std::optional<std::size_t>(program.structures().size() + id.index())
                        : std::nullopt;
                },
            },
            declaration
        );
    }

    auto verify_nominal_containment() noexcept -> bool {
        auto adjacency = std::vector<std::vector<std::size_t>>();
        adjacency.reserve(program.nominal_dependency_sets().size());
        for (const auto& dependencies : program.nominal_dependency_sets()) {
            auto targets = std::vector<std::size_t>();
            targets.reserve(dependencies.size());
            for (const auto dependency : dependencies) {
                const auto index = nominal_index(dependency);
                if (!index.has_value()) {
                    return fail(
                        SemanticProgramErrorKind::InvalidReference,
                        "nominal containment references an unknown declaration"
                    );
                }
                if (std::ranges::contains(targets, *index)) {
                    return fail(
                        SemanticProgramErrorKind::InvalidContract,
                        "nominal containment contains a repeated direct dependency"
                    );
                }
                targets.push_back(*index);
            }
            adjacency.push_back(std::move(targets));
        }
        auto states = std::vector<std::uint8_t>(adjacency.size());
        const auto acyclic = [&](this const auto& self, std::size_t node) noexcept -> bool {
            if (states[node] == 1) {
                return false;
            }
            if (states[node] == 2) {
                return true;
            }
            states[node] = 1;
            for (const auto dependency : adjacency[node]) {
                if (!self(dependency)) {
                    return false;
                }
            }
            states[node] = 2;
            return true;
        };
        for (auto node = 0uz; node < adjacency.size(); ++node) {
            if (!acyclic(node)) {
                return fail(
                    SemanticProgramErrorKind::InvalidContract,
                    "published nominal containment contains a by-value cycle"
                );
            }
        }
        return true;
    }

    auto verify_canonical_tables() noexcept -> bool {
        if (!verify_scope_table() || !verify_symbol_table() || !verify_nominal_containment()) {
            return false;
        }
        for (const auto [index, type] : std::views::enumerate(program.types())) {
            if (!verify_type_value(type.value)) {
                return false;
            }
            for (const auto& previous : program.types().first(static_cast<std::size_t>(index))) {
                if (previous.value == type.value) {
                    return fail(
                        SemanticProgramErrorKind::InvalidContract,
                        "equivalent semantic types have multiple semantic identities"
                    );
                }
            }
        }
        for (auto index = 0uz; index < program.constants().size(); ++index) {
            if (!verify_constant(index, program.constants()[index])) {
                return false;
            }
        }
        for (auto index = 0uz; index < program.failure_sets().size(); ++index) {
            const auto& failure_set = program.failure_sets()[index];
            if (!failure_members(failure_set.members)) {
                return false;
            }
            for (auto previous = 0uz; previous < index; ++previous) {
                if (program.failure_sets()[previous].members == failure_set.members) {
                    return fail(
                        SemanticProgramErrorKind::InvalidContract,
                        "equivalent failure sets have multiple semantic identities"
                    );
                }
            }
        }
        for (const auto [index, callable] : std::views::enumerate(program.callable_signatures())) {
            if (!verify_callable_signature(callable)) {
                return false;
            }
            for (const auto& previous :
                 program.callable_signatures().first(static_cast<std::size_t>(index))) {
                if (previous == callable) {
                    return fail(
                        SemanticProgramErrorKind::InvalidContract,
                        "equivalent callable signatures have multiple semantic identities"
                    );
                }
            }
        }
        for (const auto [index, callable] : std::views::enumerate(program.callables())) {
            const auto id = CallableID::from_index(static_cast<std::uint32_t>(index));
            if (!verify_callable_shape(callable)) {
                return false;
            }
            if (!program.has_body(callable.body)) {
                return fail(
                    SemanticProgramErrorKind::InvalidContract,
                    "callable contract has no published body"
                );
            }
            if (!failure_set_known(program.callable_flow(id).effective_failure_set)) {
                return false;
            }
        }
        return true;
    }

    template<typename ID>
    auto begin_occurrence(ID id, std::vector<std::uint8_t>& states, std::string_view kind) noexcept
        -> bool {
        if (!known(id, states.size())) {
            return fail(
                SemanticProgramErrorKind::InvalidReference,
                std::format("program {} occurrence has an invalid identity", kind)
            );
        }
        const auto index = static_cast<std::size_t>(id.index());
        if (states[index] == 1) {
            return fail(
                SemanticProgramErrorKind::InvalidOwnership,
                std::format("program {} occurrence participates in a cycle", kind)
            );
        }
        if (states[index] == 2) {
            return fail(
                SemanticProgramErrorKind::InvalidOwnership,
                std::format("program {} occurrence has more than one owner", kind)
            );
        }
        states[index] = 1;
        return true;
    }

    template<typename ID>
    auto finish_occurrence(ID id, std::vector<std::uint8_t>& states) noexcept -> void {
        states[id.index()] = 2;
    }

    auto binding_place(
        const HIRBindingTarget& target,
        HIRTypeID type,
        SemanticScopeID scope
    ) noexcept -> bool {
        const auto* named = std::get_if<HIRNamedBindingTarget>(&target);
        if (named == nullptr) {
            return true;
        }
        if (!symbol_known(named->symbol)) {
            return fail(
                SemanticProgramErrorKind::InvalidReference,
                "binding target references an unknown symbol"
            );
        }
        const auto& symbol = program.symbol(named->symbol);
        const auto& binding = program.binding(named->symbol);
        if (symbol.type != type || !binding.has_value()) {
            return fail(
                SemanticProgramErrorKind::InvalidPlace,
                "binding target symbol has no matching runtime binding facts"
            );
        }
        return binding->scope == scope
            || fail(
                   SemanticProgramErrorKind::InvalidScope,
                   "binding is owned by the wrong lexical scope"
            );
    }

    auto projected_type(const SemanticPlaceUse& use) noexcept -> std::optional<HIRTypeID> {
        if (!symbol_known(use.root) || !program.binding(use.root).has_value()) {
            return std::nullopt;
        }
        const auto root_type = program.symbol(use.root).type;
        if (!root_type.has_value()) {
            return std::nullopt;
        }
        auto current = *root_type;
        for (const auto& projection : use.projections) {
            const auto next = std::visit(
                Overloaded {
                    [&](const SemanticFieldProjection& field) noexcept -> std::optional<HIRTypeID> {
                        const auto* nominal =
                            std::get_if<HIRStructTypeValue>(&program.type(current).value);
                        if (nominal == nullptr
                            || nominal->structure != field.owner
                            || !known(field.owner, program.structures().size())) {
                            return std::nullopt;
                        }
                        const auto& structure = program.structure(field.owner);
                        if (field.field_index >= structure.fields.size()) {
                            return std::nullopt;
                        }
                        return structure.fields[field.field_index].type;
                    },
                    [&](const SemanticIndexProjection&) noexcept -> std::optional<HIRTypeID> {
                        const auto* array =
                            std::get_if<HIRArrayTypeValue>(&program.type(current).value);
                        return array != nullptr ? std::optional(array->element_type_id)
                                                : std::nullopt;
                    },
                },
                projection
            );
            if (!next.has_value()) {
                return std::nullopt;
            }
            current = *next;
        }
        return current;
    }

    auto verify_place_use(
        const HIRExpr& expression,
        const std::optional<SemanticPlaceUse>& place_use,
        SemanticScopeID scope
    ) noexcept -> bool {
        if (!place_use.has_value()) {
            return true;
        }
        const auto& use = *place_use;
        if (!symbol_known(use.root) || !program.binding(use.root).has_value()) {
            return fail(
                SemanticProgramErrorKind::InvalidPlace,
                "expression references an unknown runtime binding"
            );
        }
        const auto& binding = *program.binding(use.root);
        if (!scope_contains(binding.scope, scope)) {
            return fail(
                SemanticProgramErrorKind::InvalidScope,
                "expression accesses a binding outside its lexical scope"
            );
        }
        const auto writes = use.access == SemanticPlaceAccess::Write
            || use.access == SemanticPlaceAccess::ReadWrite;
        if (writes && !binding.capabilities.write) {
            return fail(
                SemanticProgramErrorKind::InvalidPlace,
                "expression uses Write without binding capability"
            );
        }
        if (use.access == SemanticPlaceAccess::Take
            && (!binding.capabilities.take || !use.projections.empty())) {
            return fail(
                SemanticProgramErrorKind::InvalidPlace,
                "expression uses Take on a non-owner or projected place"
            );
        }
        const auto result = projected_type(use);
        return result == expression.type
            || fail(
                   SemanticProgramErrorKind::InvalidPlace,
                   "place projection result does not match expression type"
            );
    }

    auto verify_failure_summary(const HIRExpressionControl& control) noexcept -> bool {
        return failure_set_known(control.evaluation_failure_set);
    }

    auto verify_effect_roots(std::span<const SymbolID> roots) noexcept -> bool {
        auto previous = std::optional<SymbolID>();
        for (const auto root : roots) {
            if (!symbol_known(root) || !program.binding(root).has_value()) {
                return fail(
                    SemanticProgramErrorKind::InvalidPlace,
                    "evaluation effect references an unknown runtime binding"
                );
            }
            if (previous.has_value() && previous->index() >= root.index()) {
                return fail(
                    SemanticProgramErrorKind::InvalidContract,
                    "evaluation effect roots are not unique and canonically ordered"
                );
            }
            previous = root;
        }
        return true;
    }

    auto verify_evaluation_effect(const EvaluationEffect& effect) noexcept -> bool {
        return verify_effect_roots(effect.reads)
            && verify_effect_roots(effect.writes)
            && verify_effect_roots(effect.takes);
    }

    auto verify_failure_summary(const HIRBlockControl& control) noexcept -> bool {
        return failure_set_known(control.outward_failure_set);
    }

    auto visit_pattern(HIRPatternID id, SemanticScopeID scope, HIRTypeID expected) noexcept
        -> bool {
        if (!begin_occurrence(id, pattern_states, "pattern")) {
            return false;
        }
        const auto& pattern = program.pattern(id);
        const auto valid = std::visit(
            Overloaded {
                [](const HIRWildcardPattern&) static noexcept { return true; },
                [&](const HIRLiteralPattern& literal) noexcept { return literal.type == expected; },
                [&](const HIROrPattern& alternatives) noexcept {
                    if (alternatives.type != expected || alternatives.alternatives.empty()) {
                        return false;
                    }
                    for (const auto child : alternatives.alternatives) {
                        if (!visit_pattern(child, scope, expected)) {
                            return false;
                        }
                    }
                    return true;
                },
                [&](const HIRTypeConstraintPattern& constraint) noexcept {
                    return type_known(constraint.type);
                },
                [&](const HIRBindingPattern& binding) noexcept {
                    return binding.type == expected
                        && binding_place(binding.target, binding.type, scope);
                },
                [&](const HIRCasePattern& enum_case) noexcept {
                    if (!known(enum_case.enum_case, program.enum_cases().size())) {
                        return false;
                    }
                    const auto& declared_case = program.enum_case(enum_case.enum_case);
                    const auto* nominal =
                        std::get_if<HIREnumTypeValue>(&program.type(expected).value);
                    if (nominal == nullptr || nominal->enumeration != declared_case.owner) {
                        return false;
                    }
                    if (declared_case.payload_types.size() != enum_case.payload.size()) {
                        return false;
                    }
                    for (auto index = 0uz; index < enum_case.payload.size(); ++index) {
                        if (!visit_pattern(
                                enum_case.payload[index],
                                scope,
                                declared_case.payload_types[index]
                            )) {
                            return false;
                        }
                    }
                    return true;
                },
            },
            pattern.value
        );
        if (!valid) {
            return failure.has_value()
                ? false
                : fail(
                      SemanticProgramErrorKind::InvalidType,
                      "typed program pattern is inconsistent with its subject"
                  );
        }
        finish_occurrence(id, pattern_states);
        return true;
    }

    auto verify_body(
        BodyID id,
        std::optional<CallableID> callable_id,
        SemanticScopeID enclosing
    ) noexcept -> bool {
        if (!claim(body_owners, id)) {
            return false;
        }
        const auto& body = program.body(id);
        const auto expected_parameter_count =
            callable_id.has_value() ? program.callable(*callable_id).parameters.size() : 0uz;
        if (!scope_known(body.scope)
            || !scope_contains(enclosing, body.scope)
            || body.parameters.size() != expected_parameter_count) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "program body does not match its callable or lexical owner"
            );
        }
        if (!callable_id.has_value()) {
            return visit_block(body.root, body.scope);
        }
        const auto& contract = program.callable(*callable_id);
        for (auto index = 0uz; index < body.parameters.size(); ++index) {
            const auto& parameter = body.parameters[index];
            if (parameter.access != contract.parameters[index].access
                || parameter.type != contract.parameters[index].type
                || !binding_place(parameter.target, parameter.type, body.scope)) {
                return fail(
                    SemanticProgramErrorKind::InvalidContract,
                    "program body parameter does not match its callable contract"
                );
            }
        }
        return visit_block(body.root, body.scope);
    }

    template<typename ID>
    auto claim(std::vector<std::uint8_t>& owners, ID id) noexcept -> bool {
        if (!known(id, owners.size())) {
            return fail(
                SemanticProgramErrorKind::InvalidReference,
                "module references an unknown program declaration"
            );
        }
        if (owners[id.index()] != 0) {
            return fail(
                SemanticProgramErrorKind::InvalidOwnership,
                "program declaration belongs to more than one module"
            );
        }
        owners[id.index()] = 2;
        return true;
    }

    auto visit_function(FunctionID id, ProgramModuleID owner) noexcept -> bool {
        if (!claim(function_owners, id)) {
            return false;
        }
        const auto& function = program.function(id);
        if (!known(function.callable, program.callables().size())) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "closed function contract has no completed body"
            );
        }
        const auto& callable = program.callable(function.callable);
        if (!program.has_body(callable.body)) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "closed function contract has no completed body"
            );
        }
        const auto& body = program.body(callable.body);
        if (!symbol_known(function.symbol)
            || program.symbol(function.symbol).module_id != owner
            || !program.symbol(function.symbol).type.has_value()
            || !type_known(function.result)
            || callable.result != function.result
            || !scope_known(body.scope)
            || program.scope(body.scope).parent.has_value()) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "function declaration has an inconsistent symbol, body, or contract"
            );
        }
        const auto& symbol_type = program.type(*program.symbol(function.symbol).type);
        const auto* function_type = std::get_if<HIRFunctionTypeValue>(&symbol_type.value);
        return (function_type != nullptr
                && function_type->callable == function.callable
                && verify_body(callable.body, function.callable, body.scope))
            || fail(
                   SemanticProgramErrorKind::InvalidContract,
                   "function symbol type does not identify its program body"
            );
    }

    auto visit_structure(StructID id, ProgramModuleID owner) noexcept -> bool {
        if (!claim(structure_owners, id)) {
            return false;
        }
        const auto& structure = program.structure(id);
        return symbol_known(structure.symbol)
            && program.symbol(structure.symbol).module_id == owner
            && std::ranges::all_of(structure.fields, [&](const auto& field) noexcept {
                   return type_known(field.type);
               });
    }

    auto visit_enumeration(EnumID id, ProgramModuleID owner) noexcept -> bool {
        if (!claim(enumeration_owners, id)) {
            return false;
        }
        const auto& enumeration = program.enumeration(id);
        if (!symbol_known(enumeration.symbol)
            || program.symbol(enumeration.symbol).module_id != owner
            || (enumeration.underlying_type.has_value()
                && !type_known(*enumeration.underlying_type))) {
            return false;
        }
        for (const auto case_id : enumeration.cases) {
            if (!claim(enum_case_owners, case_id)) {
                return false;
            }
            const auto& enum_case = program.enum_case(case_id);
            if (enum_case.owner != id
                || !symbol_known(enum_case.symbol)
                || program.symbol(enum_case.symbol).parent != enumeration.symbol
                || !std::ranges::all_of(enum_case.payload_types, [&](HIRTypeID type) noexcept {
                       return type_known(type);
                   })) {
                return false;
            }
        }
        return true;
    }

    auto visit_modules() noexcept -> bool {
        if (program.modules().size() != program.provenance().module_records().size()) {
            return fail(
                SemanticProgramErrorKind::InvalidReference,
                "program modules are not aligned with provenance"
            );
        }
        for (auto index = 0uz; index < program.modules().size(); ++index) {
            const auto module_id = ProgramModuleID::from_index(static_cast<std::uint32_t>(index));
            const auto& hir_module = program.modules()[index];
            for (const auto& item : hir_module.items) {
                const auto valid = std::visit(
                    Overloaded {
                        [&](FunctionID function) noexcept {
                            return visit_function(function, module_id);
                        },
                        [&](StructID structure) noexcept {
                            return visit_structure(structure, module_id);
                        },
                        [&](EnumID enumeration) noexcept {
                            return visit_enumeration(enumeration, module_id);
                        },
                        [&](TestID id) noexcept {
                            if (!claim(test_owners, id)) {
                                return false;
                            }
                            const auto& test = program.test(id);
                            if (!program.has_body(test.body)) {
                                return false;
                            }
                            const auto& body = program.body(test.body);
                            const auto scope = body.scope;
                            return scope_known(scope)
                                && !program.scope(scope).parent.has_value()
                                && verify_body(test.body, std::nullopt, scope);
                        },
                        [](const HIRCppRegion&) static noexcept { return true; },
                    },
                    item
                );
                if (!valid) {
                    return failure.has_value()
                        ? false
                        : fail(
                              SemanticProgramErrorKind::InvalidContract,
                              "program declaration violates its module contract"
                          );
                }
            }
        }
        return true;
    }

    auto visit_match_arm(
        const HIRMatchArm& arm,
        HIRTypeID subject,
        SemanticScopeID owner_scope
    ) noexcept -> bool {
        if (!scope_known(arm.scope)
            || !scope_contains(owner_scope, arm.scope)
            || !visit_pattern(arm.pattern, arm.scope, subject)) {
            return false;
        }
        return (!arm.guard.has_value() || visit_expression(*arm.guard, arm.scope))
            && visit_block(arm.body, arm.scope);
    }

    auto visit_catch_arm(
        const HIRCatchArm& arm,
        const HIRCatchFacts& facts,
        std::span<const HIRTypeID> protected_failures,
        SemanticScopeID owner_scope
    ) noexcept -> bool {
        if (!scope_known(arm.scope)
            || !scope_contains(owner_scope, arm.scope)
            || arm.origin.index() >= program.provenance().origins().size()
            || !failure_set_known(facts.accepted_failure_set)) {
            return false;
        }
        const auto& accepted = program.failure_set(facts.accepted_failure_set).members;
        for (const auto failure : accepted) {
            if (!std::ranges::contains(protected_failures, failure)) {
                return fail(
                    SemanticProgramErrorKind::InvalidContract,
                    "catch arm accepts a failure absent from its protected body"
                );
            }
        }
        for (auto index = 0uz; index < arm.alternatives.size(); ++index) {
            const auto& alternative = arm.alternatives[index];
            if (alternative.origin.index() >= program.provenance().origins().size()
                || (alternative.type.has_value() && !type_known(*alternative.type))) {
                return false;
            }
            if (alternative.inner.has_value()
                && (!alternative.type.has_value()
                    || !visit_pattern(*alternative.inner, arm.scope, *alternative.type))) {
                return false;
            }
        }
        if (!std::ranges::is_sorted(facts.reachable_alternative_indices)
            || std::ranges::adjacent_find(facts.reachable_alternative_indices)
                != facts.reachable_alternative_indices.end()) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "catch reachable alternatives are not sorted and unique"
            );
        }
        for (const auto index : facts.reachable_alternative_indices) {
            if (index >= arm.alternatives.size()) {
                return fail(
                    SemanticProgramErrorKind::InvalidContract,
                    "catch reachable alternative is out of range"
                );
            }
            const auto& alternative = arm.alternatives[index];
            if (alternative.type.has_value()
                && !std::ranges::contains(accepted, *alternative.type)) {
                return fail(
                    SemanticProgramErrorKind::InvalidContract,
                    "catch reachable alternative does not accept its failure type"
                );
            }
        }
        for (const auto failure : accepted) {
            const auto represented = std::ranges::any_of(
                facts.reachable_alternative_indices,
                [&](std::uint32_t index) noexcept {
                    const auto& alternative = arm.alternatives[index];
                    return !alternative.type.has_value() || *alternative.type == failure;
                }
            );
            if (!represented) {
                return fail(
                    SemanticProgramErrorKind::InvalidContract,
                    "catch accepted failure has no reachable alternative"
                );
            }
        }
        if (accepted.empty() && !facts.reachable_alternative_indices.empty()) {
            return fail(
                SemanticProgramErrorKind::InvalidContract,
                "empty catch arm acceptance has reachable alternatives"
            );
        }
        return (!arm.guard.has_value() || visit_expression(*arm.guard, arm.scope))
            && visit_block(arm.body, arm.scope);
    }

    struct CallableContract final {
        std::span<const HIRFunctionParameterType> parameters;
        HIRTypeID result;
        FailureSetID failure_set;
    };

    auto expression_callable(HIRTypeID type) const noexcept -> std::optional<CallableContract> {
        const auto& value = program.type(type).value;
        if (const auto* function = std::get_if<HIRFunctionTypeValue>(&value)) {
            const auto& callable = program.callable(function->callable);
            return CallableContract {
                .parameters = callable.parameters,
                .result = callable.result,
                .failure_set = program.callable_flow(function->callable).effective_failure_set,
            };
        }
        if (const auto* reference = std::get_if<HIRFunctionRefTypeValue>(&value)) {
            const auto& callable = program.callable_signature(reference->signature);
            return CallableContract {
                .parameters = callable.parameters,
                .result = callable.result,
                .failure_set = callable.failure_set,
            };
        }
        if (const auto* closure = std::get_if<HIRClosureTypeValue>(&value)) {
            const auto& callable = program.callable(closure->callable);
            return CallableContract {
                .parameters = callable.parameters,
                .result = callable.result,
                .failure_set = program.callable_flow(closure->callable).effective_failure_set,
            };
        }
        return std::nullopt;
    }

    auto verify_call(
        const HIRExpr& expression,
        const HIRExpressionControl& control,
        const HIRCallExpr& call,
        SemanticScopeID scope
    ) noexcept -> bool {
        if (!visit_expression(call.callee, scope)) {
            return false;
        }
        const auto contract = expression_callable(program.expression(call.callee).type);
        if (contract.has_value()) {
            if (contract->result != expression.type
                || contract->parameters.size() != call.arguments.size()) {
                return fail(
                    SemanticProgramErrorKind::InvalidContract,
                    "call expression does not match its resolved callable shape"
                );
            }
            const auto arguments_match = std::ranges::equal(
                call.arguments,
                contract->parameters,
                [&](const HIRCallArgument& argument,
                    const HIRFunctionParameterType& parameter) noexcept {
                    return argument.access == parameter.access
                        && known(argument.expression, program.expressions().size())
                        && program.expression(argument.expression).type == parameter.type;
                }
            );
            if (!arguments_match) {
                return fail(
                    SemanticProgramErrorKind::InvalidContract,
                    "call argument does not match its resolved parameter"
                );
            }
            const auto& evaluation = program.failure_set(control.evaluation_failure_set).members;
            for (const auto failure : program.failure_set(contract->failure_set).members) {
                if (!std::ranges::contains(evaluation, failure)) {
                    return fail(
                        SemanticProgramErrorKind::InvalidContract,
                        "call failure contract is absent from its evaluation summary"
                    );
                }
            }
        }
        for (const auto& argument : call.arguments) {
            if (!visit_expression(argument.expression, scope)) {
                return false;
            }
        }
        return true;
    }

    auto visit_expression(HIRExprID id, SemanticScopeID scope) noexcept -> bool {
        if (!begin_occurrence(id, expression_states, "expression")) {
            return false;
        }
        const auto& expression = program.expression(id);
        const auto& control = program.expression_control(id);
        const auto& effect = program.evaluation_effect(id);
        const auto& place_use = program.place_use(id);
        const auto& try_facts = program.try_facts(id);
        if (!type_known(expression.type)
            || (expression.constant.has_value()
                && (!known(*expression.constant, program.constants().size())
                    || program.constant(*expression.constant).type != expression.type))
            || !verify_failure_summary(control)
            || !verify_evaluation_effect(effect)
            || !verify_place_use(expression, place_use, scope)
            || (std::holds_alternative<HIRTryExpr>(expression.value) != try_facts.has_value())) {
            return failure.has_value() ? false
                                       : fail(
                                             SemanticProgramErrorKind::InvalidType,
                                             "program expression has inconsistent published facts"
                                         );
        }
        const auto valid = std::visit(
            Overloaded {
                [](const HIRLiteralExpr&) static noexcept { return true; },
                [&](const HIRNameExpr& name) noexcept {
                    return symbol_known(name.symbol)
                        && program.symbol(name.symbol).type == expression.type;
                },
                [&](const HIRArrayExpr& array) noexcept {
                    const auto* type =
                        std::get_if<HIRArrayTypeValue>(&program.type(expression.type).value);
                    if (type == nullptr || type->extent != array.element_ids.size()) {
                        return false;
                    }
                    for (const auto child : array.element_ids) {
                        if (!known(child, program.expressions().size())
                            || program.expression(child).type != type->element_type_id
                            || !visit_expression(child, scope)) {
                            return false;
                        }
                    }
                    return true;
                },
                [&](const HIRConstructionExpr& construction) noexcept {
                    const auto* nominal =
                        std::get_if<HIRStructTypeValue>(&program.type(expression.type).value);
                    if (nominal == nullptr
                        || !known(nominal->structure, program.structures().size())) {
                        return false;
                    }
                    const auto& structure = program.structure(nominal->structure);
                    if (construction.fields.size() != structure.fields.size()) {
                        return false;
                    }
                    auto initialized = std::flat_set<std::uint32_t>();
                    for (const auto& field : construction.fields) {
                        if (field.declaration_index >= structure.fields.size()
                            || !initialized.insert(field.declaration_index).second
                            || !known(field.value, program.expressions().size())
                            || program.expression(field.value).type
                                != structure.fields[field.declaration_index].type
                            || !visit_expression(field.value, scope)) {
                            return false;
                        }
                    }
                    return true;
                },
                [&](const HIRCaseConstructionExpr& construction) noexcept {
                    if (!known(construction.enum_case, program.enum_cases().size())) {
                        return false;
                    }
                    const auto& enum_case = program.enum_case(construction.enum_case);
                    if (construction.payload.size() != enum_case.payload_types.size()) {
                        return false;
                    }
                    for (auto index = 0uz; index < construction.payload.size(); ++index) {
                        const auto child = construction.payload[index];
                        if (!known(child, program.expressions().size())
                            || program.expression(child).type != enum_case.payload_types[index]
                            || !visit_expression(child, scope)) {
                            return false;
                        }
                    }
                    return true;
                },
                [&](const HIRUnaryExpr& unary) noexcept {
                    return visit_expression(unary.operand_id, scope);
                },
                [&](const HIRBinaryExpr& binary) noexcept {
                    return visit_expression(binary.left, scope)
                        && visit_expression(binary.right, scope);
                },
                [&](const HIRCastExpr& cast) noexcept {
                    return visit_expression(cast.operand_id, scope);
                },
                [&](const HIRCallExpr& call) noexcept {
                    return verify_call(expression, control, call, scope);
                },
                [&](const HIRClosureExpr& closure) noexcept {
                    if (!known(closure.callable, program.callables().size())) {
                        return false;
                    }
                    const auto& callable = program.callable(closure.callable);
                    if (!program.has_body(callable.body)) {
                        return false;
                    }
                    const auto& body = program.body(callable.body);
                    if (!scope_contains(scope, body.scope) || callable.result != closure.result) {
                        return false;
                    }
                    for (const auto& capture : closure.captures) {
                        if (!symbol_known(capture.source) || !symbol_known(capture.local)) {
                            return false;
                        }
                        const auto& source_binding = program.binding(capture.source);
                        const auto& local_binding = program.binding(capture.local);
                        if (!source_binding.has_value()
                            || !local_binding.has_value()
                            || !scope_contains(source_binding->scope, scope)
                            || local_binding->scope != body.scope) {
                            return false;
                        }
                    }
                    const auto* closure_type =
                        std::get_if<HIRClosureTypeValue>(&program.type(expression.type).value);
                    return closure_type != nullptr
                        && closure_type->callable == closure.callable
                        && closure_type->capturing == !closure.captures.empty()
                        && verify_body(callable.body, closure.callable, scope);
                },
                [&](const HIRCallableViewExpr& view) noexcept {
                    return visit_expression(view.source, scope);
                },
                [&](const HIRPropagationExpr& propagation) noexcept {
                    return visit_expression(propagation.operand_id, scope);
                },
                [&](const HIRTakeExpr& take) noexcept {
                    return visit_expression(take.operand_id, scope)
                        && (!place_use.has_value()
                            || place_use->access == SemanticPlaceAccess::Take);
                },
                [&](const HIRTextIntrinsicExpr& intrinsic) noexcept {
                    return visit_expression(intrinsic.operand_id, scope);
                },
                [&](const HIRIndexExpr& index) noexcept {
                    if (!known(index.operand_id, program.expressions().size())) {
                        return false;
                    }
                    const auto& operand_type =
                        program.type(program.expression(index.operand_id).type).value;
                    const auto* array = std::get_if<HIRArrayTypeValue>(&operand_type);
                    const auto valid_result =
                        (array != nullptr && array->element_type_id == expression.type)
                        || std::holds_alternative<HIRForeignTypeValue>(operand_type);
                    return valid_result
                        && visit_expression(index.operand_id, scope)
                        && visit_expression(index.index, scope);
                },
                [&](const HIRMemberExpr& member) noexcept {
                    return visit_expression(member.operand_id, scope);
                },
                [&](const HIRIfExpr& conditional) noexcept {
                    for (const auto& branch : conditional.branches) {
                        if (!visit_expression(branch.condition, scope)
                            || !visit_block(branch.body, scope)) {
                            return false;
                        }
                    }
                    return !conditional.else_branch.has_value()
                        || visit_block(*conditional.else_branch, scope);
                },
                [&](const HIRMatchExpr& match) noexcept {
                    if (!match_coverage_aligned(match.arms, match.coverage)) {
                        return fail(
                            SemanticProgramErrorKind::InvalidContract,
                            "match coverage facts do not align with expression arms"
                        );
                    }
                    if (!visit_expression(match.subject, scope)) {
                        return false;
                    }
                    const auto subject = program.expression(match.subject).type;
                    for (const auto& arm : match.arms) {
                        if (!visit_match_arm(arm, subject, scope)) {
                            return false;
                        }
                    }
                    return true;
                },
                [&](const HIRTryExpr& attempt) noexcept {
                    if (!visit_block(attempt.body, scope)) {
                        return false;
                    }
                    if (!try_facts.has_value() || try_facts->arms.size() != attempt.arms.size()) {
                        return false;
                    }
                    const auto outward_failure_set =
                        program.block_control(attempt.body).outward_failure_set;
                    if (!failure_set_known(outward_failure_set)
                        || !failure_set_known(try_facts->unhandled_failure_set)) {
                        return false;
                    }
                    const auto& failures = program.failure_set(outward_failure_set).members;
                    for (const auto failure :
                         program.failure_set(try_facts->unhandled_failure_set).members) {
                        if (!std::ranges::contains(failures, failure)) {
                            return fail(
                                SemanticProgramErrorKind::InvalidContract,
                                "try exposes an unhandled failure absent from its protected body"
                            );
                        }
                    }
                    for (auto index = 0uz; index < attempt.arms.size(); ++index) {
                        if (!visit_catch_arm(
                                attempt.arms[index],
                                try_facts->arms[index],
                                failures,
                                scope
                            )) {
                            return false;
                        }
                    }
                    return true;
                },
                [](const HIRCppExpr&) static noexcept { return true; },
            },
            expression.value
        );
        if (!valid) {
            return failure.has_value() ? false
                                       : fail(
                                             SemanticProgramErrorKind::InvalidType,
                                             "program expression form violates its typed shape"
                                         );
        }
        finish_occurrence(id, expression_states);
        return true;
    }

    auto visit_statement(HIRStmtID id, SemanticScopeID scope) noexcept -> bool {
        if (!begin_occurrence(id, statement_states, "statement")) {
            return false;
        }
        const auto& statement = program.statement(id);
        const auto valid = std::visit(
            Overloaded {
                [&](const HIRReturnStmt& returned) noexcept {
                    return !returned.value.has_value() || visit_expression(*returned.value, scope);
                },
                [](const HIRBreakStmt&) static noexcept { return true; },
                [](const HIRContinueStmt&) static noexcept { return true; },
                [&](const HIRThrowStmt& thrown) noexcept {
                    return type_known(thrown.failure_type)
                        && visit_expression(thrown.value, scope)
                        && program.expression(thrown.value).type == thrown.failure_type;
                },
                [](const HIRRethrowStmt&) static noexcept { return true; },
                [&](const HIRExprStmt& value) noexcept {
                    return visit_expression(value.expression, scope);
                },
                [&](const HIRBindingStmt& binding) noexcept {
                    if (!type_known(binding.type)) {
                        return fail(
                            SemanticProgramErrorKind::InvalidType,
                            "binding statement references an unknown type"
                        );
                    }
                    if (!binding_place(binding.target, binding.type, scope)
                        || !visit_expression(binding.initializer, scope)) {
                        return false;
                    }
                    return program.expression(binding.initializer).type == binding.type
                        || fail(
                               SemanticProgramErrorKind::InvalidType,
                               "binding initializer does not have the binding type"
                        );
                },
                [&](const HIRAssignmentStmt& assignment) noexcept {
                    return visit_expression(assignment.target, scope)
                        && visit_expression(assignment.value, scope)
                        && program.expression(assignment.target).type
                        == program.expression(assignment.value).type;
                },
                [&](const HIRUpdateStmt& update) noexcept {
                    return visit_expression(update.target, scope);
                },
                [&](const HIRIfStmt& conditional) noexcept {
                    for (const auto& branch : conditional.branches) {
                        if (!visit_expression(branch.condition, scope)
                            || !visit_block(branch.body, scope)) {
                            return false;
                        }
                    }
                    return !conditional.else_branch.has_value()
                        || visit_block(*conditional.else_branch, scope);
                },
                [&](const HIRMatchStmt& match) noexcept {
                    if (!match_coverage_aligned(match.arms, match.coverage)) {
                        return fail(
                            SemanticProgramErrorKind::InvalidContract,
                            "match coverage facts do not align with statement arms"
                        );
                    }
                    if (!visit_expression(match.subject, scope)) {
                        return false;
                    }
                    const auto subject = program.expression(match.subject).type;
                    for (const auto& arm : match.arms) {
                        if (!visit_match_arm(arm, subject, scope)) {
                            return false;
                        }
                    }
                    return true;
                },
                [&](const HIRWhileStmt& loop) noexcept {
                    return visit_expression(loop.condition, scope) && visit_block(loop.body, scope);
                },
                [&](const HIRCStyleForStmt& loop) noexcept {
                    if (!scope_known(loop.scope) || !scope_contains(scope, loop.scope)) {
                        return false;
                    }
                    if (loop.initializer.has_value()
                        && !visit_statement(*loop.initializer, loop.scope)) {
                        return false;
                    }
                    if (loop.condition.has_value()
                        && !visit_expression(*loop.condition, loop.scope)) {
                        return false;
                    }
                    for (const auto step : loop.steps) {
                        if (!visit_statement(step, loop.scope)) {
                            return false;
                        }
                    }
                    return visit_block(loop.body, loop.scope);
                },
                [&](const HIRRangeForStmt& loop) noexcept {
                    if (!scope_known(loop.scope)
                        || !scope_contains(scope, loop.scope)
                        || !type_known(loop.type)
                        || !binding_place(loop.target, loop.type, loop.scope)) {
                        return false;
                    }
                    const auto iterable = std::visit(
                        Overloaded {
                            [&](HIRExprID expression) noexcept {
                                return visit_expression(expression, loop.scope);
                            },
                            [&](const HIRHalfOpenRange& range) noexcept {
                                return visit_expression(range.begin, loop.scope)
                                    && visit_expression(range.end, loop.scope)
                                    && program.expression(range.begin).type
                                    == program.expression(range.end).type;
                            },
                        },
                        loop.iterable
                    );
                    return iterable && visit_block(loop.body, loop.scope);
                },
                [&](const HIRTestCheckStmt& test) noexcept {
                    return visit_expression(test.condition, scope)
                        && (!test.message.has_value() || visit_expression(*test.message, scope));
                },
                [&](const HIRTestRequireStmt& test) noexcept {
                    return visit_expression(test.condition, scope)
                        && (!test.message.has_value() || visit_expression(*test.message, scope));
                },
                [&](const HIRTestFailStmt& test) noexcept {
                    return !test.message.has_value() || visit_expression(*test.message, scope);
                },
                [](const HIRCppStmt&) static noexcept { return true; },
            },
            statement.value
        );
        if (!valid) {
            return failure.has_value() ? false
                                       : fail(
                                             SemanticProgramErrorKind::InvalidContract,
                                             "program statement form violates its structured shape"
                                         );
        }
        finish_occurrence(id, statement_states);
        return true;
    }

    auto visit_block(HIRBlockID id, SemanticScopeID owner_scope) noexcept -> bool {
        if (!begin_occurrence(id, block_states, "block")) {
            return false;
        }
        const auto& block = program.block(id);
        if (!scope_known(block.scope)
            || !scope_contains(owner_scope, block.scope)
            || !verify_failure_summary(program.block_control(id))) {
            return fail(
                SemanticProgramErrorKind::InvalidScope,
                "program block enters an invalid lexical scope"
            );
        }
        for (const auto statement : block.statements) {
            if (!visit_statement(statement, block.scope)) {
                return false;
            }
        }
        if (block.result.has_value() && !visit_expression(*block.result, block.scope)) {
            return false;
        }
        finish_occurrence(id, block_states);
        return true;
    }

    auto every_owned_once(std::span<const std::uint8_t> states, std::string_view kind) noexcept
        -> bool {
        const auto missing = std::ranges::find_if(states, [](std::uint8_t state) static noexcept {
            return state != 2;
        });
        return missing == states.end()
            || fail(
                   SemanticProgramErrorKind::InvalidOwnership,
                   std::format("program {} occurrence has no structural owner", kind)
            );
    }

    auto verify_all_occurrences() noexcept -> bool {
        return every_owned_once(function_owners, "function declaration")
            && every_owned_once(structure_owners, "structure declaration")
            && every_owned_once(enumeration_owners, "enum declaration")
            && every_owned_once(enum_case_owners, "enum case declaration")
            && every_owned_once(test_owners, "test declaration")
            && every_owned_once(body_owners, "body")
            && every_owned_once(expression_states, "expression")
            && every_owned_once(statement_states, "statement")
            && every_owned_once(pattern_states, "pattern")
            && every_owned_once(block_states, "block");
    }
};

} // namespace

auto verify_semantic_program(SemanticProgramView program) noexcept
    -> std::expected<void, SemanticProgramError> {
    return SemanticVerifier(program).run();
}

auto verify_semantic_draft_for_testing(SemanticDraftView program) noexcept
    -> std::expected<void, SemanticProgramError> {
    return SemanticVerifier(program).run();
}

auto verify_semantic_structure(SemanticDraftView program) noexcept
    -> std::expected<void, SemanticProgramError> {
    return StructureVerifier(program).run();
}
