module carven:semantic.analysis.validation.ownership.impl;

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

template<typename Program>
class OwnershipVerifier final {
    Program program;
    std::vector<std::uint8_t> function_claims;
    std::vector<std::uint8_t> structure_claims;
    std::vector<std::uint8_t> enumeration_claims;
    std::vector<std::uint8_t> enum_case_claims;
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
    explicit OwnershipVerifier(Program source) noexcept
        : program(source),
          function_claims(program.functions().size()),
          structure_claims(program.structures().size()),
          enumeration_claims(program.enumerations().size()),
          enum_case_claims(program.enum_cases().size()),
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
                        [&](StructID id) noexcept {
                            return claim(structure_claims, id, "structure");
                        },
                        [&](EnumID id) noexcept {
                            if (!claim(enumeration_claims, id, "enum")) {
                                return false;
                            }
                            for (const auto enum_case : program.enumeration(id).cases) {
                                if (!claim(enum_case_claims, enum_case, "enum case")) {
                                    return false;
                                }
                            }
                            return true;
                        },
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
            || !every_owned_once(structure_claims, "structure")
            || !every_owned_once(enumeration_claims, "enum")
            || !every_owned_once(enum_case_claims, "enum case")
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

} // namespace

auto validate_ownership(SemanticProgramView program) noexcept
    -> std::expected<void, SemanticProgramError> {
    return OwnershipVerifier(program).run();
}

auto validate_ownership(SemanticDraftView program) noexcept
    -> std::expected<void, SemanticProgramError> {
    return OwnershipVerifier(program).run();
}
